// MapFile.cpp -- .map 解析：section 切分 -> base64 -> 分块 LZO -> 瓦片记录。

#include "map/MapFile.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <vector>

#include "data/Ini.h"
#include "io/FileSystem.h"
#include "io/Lzo1x.h"

namespace ra2 {
namespace {

// ---------------------------------------------------------------- base64

int B64_Value(uint8_t c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

/// 忽略空白与 '='；遇到非法字符就停（RA2 的段里行尾干净，稳妥起见不报错）。
std::vector<uint8_t> Base64_Decode(const std::string& s) {
    std::vector<uint8_t> out;
    out.reserve(s.size() * 3 / 4);
    uint32_t acc = 0;
    int bits = 0;
    for (char ch : s) {
        const int v = B64_Value(static_cast<uint8_t>(ch));
        if (v < 0) {
            continue;                    // '='、CR、LF 都跳过
        }
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((acc >> bits) & 0xFF));
        }
    }
    return out;
}

// ---------------------------------------------------------------- 段切分

/// 按 `^\[Name\]` 切开，返回 (段名, 段体)。段体不含段头那一行。
std::vector<std::pair<std::string, std::string>> Split_Sections(const std::string& text) {
    std::vector<std::pair<std::string, std::string>> out;
    size_t pos = 0;
    const size_t n = text.size();
    // 找所有段头位置
    std::vector<std::pair<size_t, std::string>> marks;
    while (pos < n) {
        size_t nl = text.find('\n', pos);
        size_t line_end = (nl == std::string::npos) ? n : nl;
        size_t i = pos;
        while (i < line_end && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r')) {
            ++i;
        }
        if (i < line_end && text[i] == '[') {
            size_t j = text.find(']', i);
            if (j != std::string::npos && j < line_end) {
                marks.emplace_back(pos, text.substr(i + 1, j - i - 1));
                pos = (nl == std::string::npos) ? n : nl + 1;
                continue;
            }
        }
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
    for (size_t k = 0; k < marks.size(); ++k) {
        const size_t start = marks[k].first;
        const size_t end = (k + 1 < marks.size()) ? marks[k + 1].first : n;
        // 去掉段头行
        size_t nl = text.find('\n', start);
        size_t body = (nl == std::string::npos || nl >= end) ? end : nl + 1;
        out.emplace_back(marks[k].second, text.substr(body, end - body));
    }
    return out;
}

/// 把段体里的 `序号=base64` 行拼成一整串。
std::string Collect_B64(const std::string& body) {
    std::string s;
    s.reserve(body.size());
    size_t pos = 0;
    while (pos < body.size()) {
        size_t nl = body.find('\n', pos);
        if (nl == std::string::npos) nl = body.size();
        std::string line = body.substr(pos, nl - pos);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }
        const size_t eq = line.find('=');
        if (eq != std::string::npos) {
            s += line.substr(eq + 1);
        }
        pos = nl + 1;
    }
    return s;
}

std::vector<int> Parse_Int4(const std::string& v) {
    std::vector<int> out;
    size_t pos = 0;
    while (out.size() < 4) {
        const size_t c = v.find(',', pos);
        const std::string tok = v.substr(pos, (c == std::string::npos) ? std::string::npos : c - pos);
        out.push_back(atoi(tok.c_str()));
        if (c == std::string::npos) break;
        pos = c + 1;
    }
    while (out.size() < 4) out.push_back(0);
    return out;
}

std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return s.substr(a, b - a);
}

}  // namespace

bool MapFile::Load_Path(const char* path, std::string* err) {
    FILE* f = fopen(path, "rb");
    if (f == nullptr) {
        if (err) *err = std::string("打不开 ") + path;
        return false;
    }
    fseek(f, 0, SEEK_END);
    const long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> whole(static_cast<size_t>(sz > 0 ? sz : 0));
    if (sz > 0) {
        const size_t got = fread(whole.data(), 1, whole.size(), f);
        whole.resize(got);
    }
    fclose(f);

    name_ = path;
    const size_t slash = name_.find_last_of("\\/");
    if (slash != std::string::npos) {
        name_ = name_.substr(slash + 1);
    }

    // 先试着当 MIX 剥一层（.mmx / .yro 就是加密 MIX）。
    //
    // 【坑】MIX 里的条目顺序是 CRC 序，不是"地图在前"。
    //   Arena.mmx  第一条就是 ARENA.MAP，所以按"第一个以 [ 开头的"能中；
    //   Ice_Age.yro 第一条是 116 字节的 [MultiMaps] 描述，真正的 ICE_AGE.MAP
    //   排在第二 —— 于是加载的是那份 116 字节的 INI，一个瓦片都没有。
    //   所以判据必须是**内容里有没有 [IsoMapPack5]**，而不是"以 [ 开头"。
    if (whole.size() > 96) {
        MixFileClass mix;
        if (mix.Open_Memory(whole.data(), whole.size()) && mix.Count() > 0 &&
            mix.Count() < 64) {
            const MixEntry* fallback = nullptr;
            for (const MixEntry& e : mix.Entries()) {
                if (e.size < 64 || e.size > 64u * 1024u * 1024u) {
                    continue;
                }
                std::vector<uint8_t> body = mix.Read_Entry(e);
                if (body.size() < 32 || body[0] != '[') {
                    continue;
                }
                const std::string_view sv(reinterpret_cast<const char*>(body.data()),
                                          body.size());
                if (sv.find("[IsoMapPack5") != std::string_view::npos) {
                    return Load_Data(body.data(), body.size(), err);
                }
                if (fallback == nullptr || e.size > fallback->size) {
                    fallback = &e;      // 退路：取最大的那份文本
                }
            }
            if (fallback != nullptr) {
                const std::vector<uint8_t> body = mix.Read_Entry(*fallback);
                return Load_Data(body.data(), body.size(), err);
            }
        }
    }
    if (err && whole.size() && whole[0] != '[') {
        *err = "既不是 .map 文本，也不是装着 .map 的 MIX 包";
    }
    return Load_Data(whole.data(), whole.size(), err);
}

bool MapFile::Load_Data(const uint8_t* data, size_t size, std::string* err) {
    cells_.clear();
    overlay_.clear();
    overlay_data_.clear();
    raw_sections_.clear();
    max_tile_ = -1;
    stored_ = 0;
    theater_.clear();
    for (int i = 0; i < 4; ++i) {
        rect_[i] = 0;
        local_[i] = 0;
    }

    const std::string text(reinterpret_cast<const char*>(data), size);
    raw_sections_ = Split_Sections(text);

    std::string iso_pack;
    std::string overlay_pack;
    std::string overlay_data_pack;
    for (const auto& kv : raw_sections_) {
        if (_stricmp(kv.first.c_str(), "Map") == 0) {
            // 逐行找 Theater= / Size= / LocalSize=
            size_t pos = 0;
            while (pos < kv.second.size()) {
                size_t nl = kv.second.find('\n', pos);
                if (nl == std::string::npos) nl = kv.second.size();
                std::string line = Trim(kv.second.substr(pos, nl - pos));
                const size_t sc = line.find(';');
                if (sc != std::string::npos) line = Trim(line.substr(0, sc));
                const size_t eq = line.find('=');
                if (eq != std::string::npos) {
                    const std::string key = Trim(line.substr(0, eq));
                    const std::string val = Trim(line.substr(eq + 1));
                    if (_stricmp(key.c_str(), "Theater") == 0) {
                        theater_ = val;
                    } else if (_stricmp(key.c_str(), "Size") == 0) {
                        const std::vector<int> v = Parse_Int4(val);
                        for (int i = 0; i < 4; ++i) rect_[i] = v[i];
                    } else if (_stricmp(key.c_str(), "LocalSize") == 0) {
                        const std::vector<int> v = Parse_Int4(val);
                        for (int i = 0; i < 4; ++i) local_[i] = v[i];
                    }
                }
                pos = nl + 1;
            }
        } else if (_stricmp(kv.first.c_str(), "IsoMapPack5") == 0) {
            iso_pack = Collect_B64(kv.second);
        } else if (_stricmp(kv.first.c_str(), "OverlayPack") == 0) {
            overlay_pack = Collect_B64(kv.second);
        } else if (_stricmp(kv.first.c_str(), "OverlayDataPack") == 0) {
            overlay_data_pack = Collect_B64(kv.second);
        }
    }

    if (local_[2] <= 0 || local_[3] <= 0) {
        local_[0] = rect_[0];
        local_[1] = rect_[1];
        local_[2] = rect_[2];
        local_[3] = rect_[3];
    }

    if (!iso_pack.empty()) {
        if (!Decode_Iso_Pack(iso_pack, err)) {
            return false;
        }
    }

    if (!overlay_pack.empty()) {
        const std::vector<uint8_t> raw = Base64_Decode(overlay_pack);
        const char* e = nullptr;
        if (Lzo1x_Decompress_Chunks(raw.data(), raw.size(), &overlay_, &e) && err) {
            // 解不开不算致命（有些地图这段是空的），记下来即可
        }
    }
    if (!overlay_data_pack.empty()) {
        const std::vector<uint8_t> raw = Base64_Decode(overlay_data_pack);
        const char* e = nullptr;
        Lzo1x_Decompress_Chunks(raw.data(), raw.size(), &overlay_data_, &e);
    }

    return !cells_.empty();
}

bool MapFile::Decode_Iso_Pack(const std::string& b64, std::string* err) {
    const std::vector<uint8_t> raw = Base64_Decode(b64);
    packed_bytes_ = raw.size();

    std::vector<uint8_t> flat;
    const char* e = nullptr;
    if (!Lzo1x_Decompress_Chunks(raw.data(), raw.size(), &flat, &e)) {
        if (err) *err = std::string("IsoMapPack5 解压失败: ") + (e ? e : "?");
        return false;
    }
    unpacked_bytes_ = flat.size();

    // 末尾 4 字节是终止记录，不参与。
    const size_t usable = (flat.size() >= 4) ? flat.size() - 4 : 0;
    const size_t n = usable / 11;

    // 【裁剪过的地图包】modenc 写明：高度 0 的 Clear 瓦片可以不存，
    // 游戏读到时自己补。实测 53 张官方地图里有 3 张（EB3 / NewHghts / RiverRam）
    // 记录条数少于 (2W-1)*H —— 那就是被裁过的。
    // 所以先铺一张 W*H 的网格，缺省 tile=0（Clear01）、level=0，再用记录覆盖；
    // 不补的话地图上会留下透明窟窿。
    const int W = rect_[2] > 0 ? rect_[2] : 0;
    const int H = rect_[3] > 0 ? rect_[3] : 0;
    cells_.assign(static_cast<size_t>(W) * static_cast<size_t>(H), IsoCell{});
    for (int cy = 0; cy < H; ++cy) {
        for (int cx = 0; cx < W; ++cx) {
            IsoCell& c = cells_[static_cast<size_t>(cy) * W + cx];
            c.cx = cx;
            c.cy = cy;
            c.tile = 0;
        }
    }

    for (size_t i = 0; i < n; ++i) {
        const uint8_t* p = flat.data() + i * 11;
        const int16_t x = static_cast<int16_t>(p[0] | (p[1] << 8));
        const int16_t y = static_cast<int16_t>(p[2] | (p[3] << 8));
        const int32_t tile = static_cast<int32_t>(
            static_cast<uint32_t>(p[4]) | (static_cast<uint32_t>(p[5]) << 8) |
            (static_cast<uint32_t>(p[6]) << 16) | (static_cast<uint32_t>(p[7]) << 24));

        // 只有 X+Y 为奇数的才是真单元（实测 80x80 -> 6400 个）。
        if (((x + y) & 1) == 0) {
            continue;
        }
        const int w = rect_[2];
        IsoCell c;
        c.cx = (x + y - 1 - w) / 2;
        c.cy = (y - 1 - (x - w)) / 2;
        c.tile = (tile == 0xFFFF || tile < 0) ? -1 : tile;
        c.sub = p[8];
        c.level = p[9];
        c.ice = p[10];
        if (c.cx < 0 || c.cx >= W || c.cy < 0 || c.cy >= H) {
            continue;                     // 越界记录：宁可丢，不要踩坏邻居
        }
        cells_[static_cast<size_t>(c.cy) * W + c.cx] = c;
        ++stored_;
        if (c.tile > max_tile_) {
            max_tile_ = c.tile;
        }
    }
    return true;
}

std::string MapFile::Raw_Section(const char* name) const {
    for (const auto& kv : raw_sections_) {
        if (_stricmp(kv.first.c_str(), name) == 0) {
            return kv.second;
        }
    }
    return std::string();
}

}  // namespace ra2
