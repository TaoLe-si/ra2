// MapFile.cpp -- .map 解析：section 切分 -> base64 -> 分块 LZO -> 瓦片记录。

#include "map/MapFile.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "data/Ini.h"
#include "io/FileSystem.h"
#include "io/Format80.h"
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
        // OverlayPack 外壳和 IsoMapPack5 一样是分块，块内是 Format80（LCW），
        // 不是 LZO。Arena 首块 9 字节解出 8192 个 0xFF，整段 512×512。
        if (!Format80_Decompress_Chunks(raw.data(), raw.size(), &overlay_, &e)) {
            const char* e80 = e;
            overlay_.clear();
            if (!Lzo1x_Decompress_Chunks(raw.data(), raw.size(), &overlay_, &e)) {
                std::printf("[!] OverlayPack 解压失败: Format80=%s LZO=%s（%zu 字节）\n",
                            e80 ? e80 : "?", e ? e : "?", raw.size());
                overlay_.clear();
            }
        }
    }
    if (!overlay_data_pack.empty()) {
        const std::vector<uint8_t> raw = Base64_Decode(overlay_data_pack);
        const char* e = nullptr;
        if (!Format80_Decompress_Chunks(raw.data(), raw.size(), &overlay_data_, &e)) {
            const char* e80 = e;
            overlay_data_.clear();
            if (!Lzo1x_Decompress_Chunks(raw.data(), raw.size(), &overlay_data_,
                                         &e)) {
                std::printf("[!] OverlayDataPack 解压失败: Format80=%s LZO=%s\n",
                            e80 ? e80 : "?", e ? e : "?");
                overlay_data_.clear();
            }
        }
    }

    Parse_Objects();
    Parse_Triggers();
    Parse_Team_Types();
    Parse_Base_Nodes();

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

    // 【裁剪过的地图包】modenc：高度 0 的 Clear 可省略，游戏读时补。
    // 网格是 (2W-1)×H，与 CNCMaps tiles[,] / ModEnc 单元数一致。
    // 旧实现只收 X+Y 奇数 → 丢掉半数瓦片 → 菱形之间留空成棋盘锯齿。
    const int W = rect_[2] > 0 ? rect_[2] : 0;
    const int H = rect_[3] > 0 ? rect_[3] : 0;
    const int iso_w = (W > 0) ? (W * 2 - 1) : 0;
    cells_.assign(static_cast<size_t>(iso_w) * static_cast<size_t>(H), IsoCell{});
    for (int cy = 0; cy < H; ++cy) {
        for (int cx = 0; cx < iso_w; ++cx) {
            IsoCell& c = cells_[static_cast<size_t>(cy) * iso_w + cx];
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

        // CNCMaps：dx = X-Y+W-1，dy = X+Y-W-1；存 tiles[dx, dy/2]
        const int dx = static_cast<int>(x) - static_cast<int>(y) + W - 1;
        const int dy = static_cast<int>(x) + static_cast<int>(y) - W - 1;
        if (dx < 0 || dy < 0 || dx >= iso_w || (dy / 2) >= H) {
            continue;
        }
        IsoCell c;
        c.cx = dx;
        c.cy = dy / 2;
        // 0xFFFF = 未存瓦片 → Clear（与裁剪省略规则同一条）。
        c.tile = (tile == 0xFFFF || tile < 0) ? 0 : tile;
        c.sub = p[8];
        c.level = p[9];
        c.ice = p[10];
        cells_[static_cast<size_t>(c.cy) * static_cast<size_t>(iso_w) +
               static_cast<size_t>(c.cx)] = c;
        ++stored_;
        if (c.tile > max_tile_) {
            max_tile_ = c.tile;
        }
    }
    return true;
}

// ---------------------------------------------------------------- 对象段
namespace {

/// 取逗号分隔的第 i 个字段（0 基）。越界返回空串。
std::string Field(const std::string& s, int i) {
    size_t pos = 0;
    for (int k = 0; k < i; ++k) {
        const size_t c = s.find(',', pos);
        if (c == std::string::npos) {
            return std::string();
        }
        pos = c + 1;
    }
    const size_t c = s.find(',', pos);
    return (c == std::string::npos) ? s.substr(pos) : s.substr(pos, c - pos);
}

/// 按行切开，去掉空行和注释行（`;` / `//` 开头）。
/// INI 的行尾可能是 CRLF，尾部的 '\r' 要吃掉，否则字段名会粘上回车。
std::vector<std::string> Split_Lines(const std::string& text) {
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) {
            nl = text.size();
        }
        std::string line = text.substr(pos, nl - pos);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::string t = Trim(line);
        if (!t.empty() && t[0] != ';' && !(t.size() >= 2 && t[0] == '/' && t[1] == '/')) {
            out.push_back(t);
        }
        if (nl >= text.size()) {
            break;
        }
        pos = nl + 1;
    }
    return out;
}

}  // namespace

void MapFile::Parse_Objects() {
    objects_.clear();
    objects_dropped_ = 0;
    waypoints_.clear();
    houses_.clear();

    const int W = rect_[2];
    const int H = rect_[3];
    const int iso_w = (W > 0) ? (W * 2 - 1) : 0;

    // [Houses]：编号 -> 阵营名
    for (const auto& kv : raw_sections_) {
        if (_stricmp(kv.first.c_str(), "Houses") != 0) {
            continue;
        }
        for (const std::string& line : Split_Lines(kv.second)) {
            const size_t eq = line.find('=');
            if (eq == std::string::npos) {
                continue;
            }
            // 编号是纯数字键，且**不保证连续**（实测有的地图从 1 开始）
            const int idx = std::atoi(line.substr(0, eq).c_str());
            const std::string name = Trim(line.substr(eq + 1));
            if (name.empty()) {
                continue;
            }
            if (idx >= static_cast<int>(houses_.size())) {
                houses_.resize(static_cast<size_t>(idx) + 1);
            }
            houses_[static_cast<size_t>(idx)] = name;
        }
        break;
    }

    auto to_iso = [&](int x, int y, int* out_dx, int* out_row) -> bool {
        const int dx = x - y + W - 1;
        const int dy = x + y - W - 1;
        if (dx < 0 || dy < 0 || dx >= iso_w || (dy / 2) >= H) {
            return false;
        }
        *out_dx = dx;
        *out_row = dy / 2;
        return true;
    };

    // [Waypoints]：值是 X*1000+Y 的打包坐标（键是编号）
    for (const auto& kv : raw_sections_) {
        if (_stricmp(kv.first.c_str(), "Waypoints") != 0) {
            continue;
        }
        for (const std::string& line : Split_Lines(kv.second)) {
            const size_t eq = line.find('=');
            if (eq == std::string::npos) {
                continue;
            }
            const int packed = std::atoi(line.substr(eq + 1).c_str());
            const int x = packed / 1000;
            const int y = packed % 1000;
            MapWaypoint wp;
            wp.index = std::atoi(line.substr(0, eq).c_str());
            if (!to_iso(x, y, &wp.cx, &wp.cy)) {
                continue;
            }
            waypoints_.push_back(wp);
        }
        break;
    }

    // 四个对象段。字段位置见 MapObject 的注释（实测 14 字段 / Structures 17 字段）。
    static const struct { const char* section; MapObjectKind kind; } kSections[] = {
        {"Terrain",    MapObjectKind::Terrain},
        {"Units",      MapObjectKind::Unit},
        {"Infantry",   MapObjectKind::Infantry},
        {"Structures", MapObjectKind::Building},
        {"Aircraft",   MapObjectKind::Aircraft},
    };

    for (const auto& def : kSections) {
        // 同名段只认第一个（Split_Sections 不合并，实测地图里也不重复）
        const std::string* body = nullptr;
        for (const auto& kv : raw_sections_) {
            if (_stricmp(kv.first.c_str(), def.section) == 0) {
                body = &kv.second;
                break;
            }
        }
        if (body == nullptr) {
            continue;
        }
        for (const std::string& line : Split_Lines(*body)) {
            const size_t eq = line.find('=');
            if (eq == std::string::npos) {
                continue;
            }
            MapObject o;
            o.kind = def.kind;
            int x = 0, y = 0;
            if (def.kind == MapObjectKind::Terrain) {
                // 键就是打包坐标，值只有名字
                const int packed = std::atoi(line.substr(0, eq).c_str());
                x = packed / 1000;
                y = packed % 1000;
                o.type = Trim(line.substr(eq + 1));
                o.owner = "Neutral";
                o.hp = 256;
            } else {
                const std::string v = line.substr(eq + 1);
                o.owner = Trim(Field(v, 0));
                o.type = Trim(Field(v, 1));
                o.hp = std::atoi(Field(v, 2).c_str());
                x = std::atoi(Field(v, 3).c_str());
                y = std::atoi(Field(v, 4).c_str());
                if (def.kind == MapObjectKind::Infantry) {
                    o.subcell = std::atoi(Field(v, 5).c_str());
                    o.mission = Trim(Field(v, 6));
                    o.facing = std::atoi(Field(v, 7).c_str());
                    o.group = std::atoi(Field(v, 10).c_str());
                } else {
                    o.facing = std::atoi(Field(v, 5).c_str());
                    o.mission = Trim(Field(v, 6));
                    o.group = std::atoi(Field(v, 9).c_str());
                }
            }
            if (!to_iso(x, y, &o.cx, &o.cy)) {
                ++objects_dropped_;    // 地图自带的越界摆件，丢掉（官方图里也有）
                continue;
            }
            objects_.push_back(std::move(o));
        }
    }
}

std::string MapFile::Raw_Section(const char* name) const {
    for (const auto& kv : raw_sections_) {
        if (_stricmp(kv.first.c_str(), name) == 0) {
            return kv.second;
        }
    }
    return std::string();
}

void MapFile::Parse_Triggers() {
    triggers_.clear();
    auto section = [this](const char* name) -> std::string {
        return Raw_Section(name);
    };
    const std::string trig = section("Triggers");
    const std::string ev = section("Events");
    const std::string act = section("Actions");
    const std::string tags = section("Tags");
    if (trig.empty()) {
        return;
    }
    std::unordered_map<std::string, std::string> events, actions, tag_rep;
    auto fill = [](const std::string& body,
                   std::unordered_map<std::string, std::string>* out) {
        std::istringstream in(body);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            const size_t eq = line.find('=');
            if (eq == std::string::npos || eq == 0) {
                continue;
            }
            (*out)[line.substr(0, eq)] = line.substr(eq + 1);
        }
    };
    fill(ev, &events);
    fill(act, &actions);
    {
        std::istringstream in(tags);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            const size_t eq = line.find('=');
            if (eq == std::string::npos) {
                continue;
            }
            // Tags: ID=repeat,Name,TriggerID
            const std::string val = line.substr(eq + 1);
            std::vector<std::string> f;
            size_t start = 0;
            while (start <= val.size()) {
                size_t c = val.find(',', start);
                if (c == std::string::npos) {
                    c = val.size();
                }
                f.push_back(val.substr(start, c - start));
                if (c >= val.size()) {
                    break;
                }
                start = c + 1;
            }
            if (f.size() >= 3) {
                tag_rep[f[2]] = f[0];  // TriggerID -> repeat
            }
        }
    }
    {
        std::istringstream in(trig);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            const size_t eq = line.find('=');
            if (eq == std::string::npos || eq == 0) {
                continue;
            }
            MapTrigger t;
            t.id = line.substr(0, eq);
            const std::string val = line.substr(eq + 1);
            // House,AttachedTag,Name,...
            size_t c1 = val.find(',');
            size_t c2 = c1 == std::string::npos ? std::string::npos
                                                : val.find(',', c1 + 1);
            size_t c3 = c2 == std::string::npos ? std::string::npos
                                                : val.find(',', c2 + 1);
            if (c1 != std::string::npos) {
                t.house = val.substr(0, c1);
            }
            if (c2 != std::string::npos && c3 != std::string::npos) {
                t.name = val.substr(c2 + 1, c3 - c2 - 1);
            }
            const auto eit = events.find(t.id);
            if (eit != events.end()) {
                t.events_raw = eit->second;
                // TEvent::Parse @0x0071F4E0：count, type, kind, payload...
                std::vector<std::string> tok;
                {
                    std::string cur;
                    for (char ch : t.events_raw) {
                        if (ch == ',') {
                            tok.push_back(cur);
                            cur.clear();
                        } else {
                            cur.push_back(ch);
                        }
                    }
                    if (!cur.empty() || !t.events_raw.empty()) {
                        tok.push_back(cur);
                    }
                }
                size_t ti = 0;
                if (ti < tok.size()) {
                    const int count = std::atoi(tok[ti++].c_str());
                    for (int e = 0; e < count && ti < tok.size(); ++e) {
                        MapTriggerEvent ev;
                        ev.type = std::atoi(tok[ti++].c_str());
                        if (ti >= tok.size()) {
                            break;
                        }
                        ev.kind = std::atoi(tok[ti++].c_str());
                        if (ti >= tok.size()) {
                            break;
                        }
                        if (ev.kind == 1) {
                            ev.type_name = tok[ti++];
                        } else if (ev.kind == 2) {
                            ev.param = std::atoi(tok[ti++].c_str());
                            if (ti < tok.size()) {
                                ev.house_name = tok[ti++];
                            }
                        } else {
                            ev.param = std::atoi(tok[ti++].c_str());
                        }
                        t.events.push_back(std::move(ev));
                    }
                }
            }
            const auto ait = actions.find(t.id);
            if (ait != actions.end()) {
                t.actions_raw = ait->second;
            }
            const auto rit = tag_rep.find(t.id);
            if (rit != tag_rep.end()) {
                t.repeat = std::atoi(rit->second.c_str());
            }
            triggers_.push_back(std::move(t));
        }
    }
    if (!triggers_.empty()) {
            std::printf("  触发器 %zu 条（TEvent 0/8/13/14/23/27/28/30/32/36/37/45-47/51/57/58/60/61；"
                    "TAction 1-4/6-7/9/12-13/15-16/19-21/23-30/35/37-39/53-54/56/57/67-69/99/102/108/113）\n",
                    triggers_.size());
    }
}


void MapFile::Parse_Team_Types() {
    team_types_.clear();
    auto section = [this](const char* name) -> std::string {
        return Raw_Section(name);
    };
    // Waypoint letter → index（gamemd 0x763690）：A..Z = 0..25；AA.. = 26+。
    auto wp_letter = [](const std::string& s) -> int {
        std::string t = s;
        while (!t.empty() && (t.back() == ' ' || t.back() == '\t' || t.back() == '\r')) {
            t.pop_back();
        }
        size_t a = 0;
        while (a < t.size() && (t[a] == ' ' || t[a] == '\t')) {
            ++a;
        }
        t = t.substr(a);
        if (t.empty()) {
            return -1;
        }
        auto up = [](char c) -> char {
            return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
        };
        if (t.size() >= 2 &&
            ((t[0] >= 'A' && t[0] <= 'Z') || (t[0] >= 'a' && t[0] <= 'z')) &&
            ((t[1] >= 'A' && t[1] <= 'Z') || (t[1] >= 'a' && t[1] <= 'z'))) {
            const char c0 = up(t[0]);
            const char c1 = up(t[1]);
            return (c0 - 'A') * 26 + (c1 - 'A') + 26;
        }
        const char c0 = up(t[0]);
        if (c0 >= 'A' && c0 <= 'Z' && t.size() == 1) {
            return c0 - 'A';
        }
        return std::atoi(t.c_str());
    };
    auto ini_get = [](const std::string& body, const char* key) -> std::string {
        const size_t klen = std::strlen(key);
        for (size_t p = 0; p < body.size(); ++p) {
            if (_strnicmp(body.c_str() + p, key, static_cast<unsigned>(klen)) != 0) {
                continue;
            }
            if (p > 0) {
                const char prev = body[p - 1];
                if (prev != '\n' && prev != '\r') {
                    continue;
                }
            }
            const char* v = body.c_str() + p + klen;
            while (*v == ' ' || *v == '\t') {
                ++v;
            }
            if (*v != '=') {
                continue;
            }
            ++v;
            while (*v == ' ' || *v == '\t') {
                ++v;
            }
            std::string out;
            while (*v && *v != '\n' && *v != '\r') {
                out.push_back(*v++);
            }
            while (!out.empty() && (out.back() == ' ' || out.back() == '\t')) {
                out.pop_back();
            }
            return out;
        }
        return {};
    };
    auto parse_tf = [&](const std::string& tf_id) -> std::vector<MapTaskForceEntry> {
        std::vector<MapTaskForceEntry> out;
        if (tf_id.empty()) {
            return out;
        }
        const std::string body = section(tf_id.c_str());
        for (const std::string& line : Split_Lines(body)) {
            const size_t eq = line.find('=');
            if (eq == std::string::npos || eq == 0) {
                continue;
            }
            bool digits = true;
            for (size_t i = 0; i < eq; ++i) {
                if (line[i] < '0' || line[i] > '9') {
                    digits = false;
                    break;
                }
            }
            if (!digits) {
                continue;
            }
            const std::string val = line.substr(eq + 1);
            const size_t c = val.find(',');
            MapTaskForceEntry e;
            if (c == std::string::npos) {
                e.count = 1;
                e.type = Trim(val);
            } else {
                e.count = std::atoi(val.substr(0, c).c_str());
                e.type = Trim(val.substr(c + 1));
            }
            if (e.count > 0 && !e.type.empty()) {
                out.push_back(std::move(e));
            }
        }
        return out;
    };

    const std::string list = section("TeamTypes");
    if (list.empty()) {
        return;
    }
    std::vector<std::string> ids;
    for (const std::string& line : Split_Lines(list)) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string id = Trim(line.substr(eq + 1));
        if (!id.empty()) {
            ids.push_back(id);
        }
    }
    for (const std::string& id : ids) {
        MapTeamType tt;
        tt.id = id;
        const std::string body = section(id.c_str());
        if (body.empty()) {
            continue;
        }
        tt.name = ini_get(body, "Name");
        tt.house = ini_get(body, "House");
        tt.taskforce_id = ini_get(body, "TaskForce");
        tt.script_id = ini_get(body, "Script");
        const std::string wp = ini_get(body, "Waypoint");
        if (!wp.empty()) {
            tt.waypoint = wp_letter(wp);
        }
        const std::string twp = ini_get(body, "TransportWaypoint");
        if (!twp.empty()) {
            tt.transport_waypoint = wp_letter(twp);
        }
        const std::string drop = ini_get(body, "Droppod");
        tt.droppod = (!_stricmp(drop.c_str(), "yes") || drop == "1");
        const std::string reinf = ini_get(body, "Reinforce");
        tt.reinforce = (!_stricmp(reinf.c_str(), "yes") || reinf == "1");
        tt.members = parse_tf(tt.taskforce_id);
        team_types_.push_back(std::move(tt));
    }
    if (!team_types_.empty()) {
        std::printf(
            "  TeamTypes %zu 条（TAction 4 Create@0x6F09C0 / 7 Reinforce@0x65D8E0）\n",
            team_types_.size());
    }
}

namespace {

size_t Overlay_Pack_Index(const MapFile& map, int dx, int row,
                          const std::vector<uint8_t>& ov) {
    if (ov.empty() || dx < 0 || row < 0) {
        return static_cast<size_t>(-1);
    }
    const int W = map.Width();
    const int H = map.Height();
    const int iso_w = map.Iso_Width();
    const size_t n = ov.size();
    const int dy = row * 2 + (dx & 1);
    const int iso_x = (dx + dy) / 2 + 1;
    const int iso_y = (dy - dx) / 2 + W;
    if (W > 0 && iso_w > 0 &&
        n == static_cast<size_t>(iso_w) * static_cast<size_t>(H)) {
        const size_t i = static_cast<size_t>(row) * static_cast<size_t>(iso_w) +
                         static_cast<size_t>(dx);
        return (i < n) ? i : static_cast<size_t>(-1);
    }
    if (iso_x < 0 || iso_y < 0) {
        return static_cast<size_t>(-1);
    }
    size_t stride = 512;
    if (n < 512ull * 512ull) {
        stride = static_cast<size_t>(W + H);
        if (stride == 0) {
            return static_cast<size_t>(-1);
        }
    }
    const size_t i = static_cast<size_t>(iso_x) + static_cast<size_t>(iso_y) * stride;
    return (i < n) ? i : static_cast<size_t>(-1);
}

}  // namespace

uint8_t MapFile::Overlay_At(int cx, int cy) const {
    const size_t i = Overlay_Pack_Index(*this, cx, cy, overlay_);
    return (i == static_cast<size_t>(-1)) ? 0xFF : overlay_[i];
}

uint8_t MapFile::Overlay_Data_At(int cx, int cy) const {
    const size_t i = Overlay_Pack_Index(*this, cx, cy, overlay_data_);
    return (i == static_cast<size_t>(-1)) ? 0xFF : overlay_data_[i];
}

void MapFile::Set_Overlay_At(int cx, int cy, uint8_t v) {
    const size_t i = Overlay_Pack_Index(*this, cx, cy, overlay_);
    if (i != static_cast<size_t>(-1)) {
        overlay_[i] = v;
    }
}

void MapFile::Set_Overlay_Data_At(int cx, int cy, uint8_t v) {
    const size_t i = Overlay_Pack_Index(*this, cx, cy, overlay_data_);
    if (i != static_cast<size_t>(-1)) {
        overlay_data_[i] = v;
    }
}

void MapFile::Parse_Base_Nodes() {
    // [Base] 段一行：`id=x,y,Building,Refinery,Owner,Weapon,WeaponCount`
    // 原版 BaseNodeClass @0x006DF100 / BaseClass @0x0069E8A0：
    //   先 [Base] → BaseNode 链表，再 BaseNode::Read_INI 每行 tokenize 7 段。
    // 这里我们只解数据；真让 HouseClass::AI 拿这个去 Queue_Build 是后续工作。
    base_nodes_.clear();
    const std::string body = Raw_Section("Base");
    if (body.empty()) {
        return;
    }
    size_t pos = 0;
    while (pos < body.size()) {
        size_t nl = body.find('\n', pos);
        if (nl == std::string::npos) nl = body.size();
        std::string line = body.substr(pos, nl - pos);
        // 去注释 / 前后空白
        const size_t sc = line.find(';');
        if (sc != std::string::npos) line = line.substr(0, sc);
        // Trim
        size_t a = 0;
        while (a < line.size() && (line[a] == ' ' || line[a] == '\t' || line[a] == '\r')) ++a;
        size_t b = line.size();
        while (b > a && (line[b-1] == ' ' || line[b-1] == '\t' || line[b-1] == '\r')) --b;
        line = line.substr(a, b - a);
        if (!line.empty()) {
            const size_t eq = line.find('=');
            if (eq != std::string::npos) {
                std::string val = line.substr(eq + 1);
                // 切 7 段
                std::vector<std::string> tok;
                std::string cur;
                for (char c : val) {
                    if (c == ',') { tok.push_back(cur); cur.clear(); }
                    else { cur += c; }
                }
                tok.push_back(cur);
                while (tok.size() < 7) tok.push_back(std::string());
                MapBaseNode n;
                n.cx = std::atoi(tok[0].c_str());
                n.cy = std::atoi(tok[1].c_str());
                n.building = tok[2];
                n.refinery = tok[3];
                n.owner_house = tok[4];
                n.weapon = tok[5];
                n.weapon_count = std::atoi(tok[6].c_str());
                base_nodes_.push_back(n);
            }
        }
        pos = nl + 1;
    }
    std::printf("  [Map] [Base] %zu 条\n", base_nodes_.size());
}

}  // namespace ra2
