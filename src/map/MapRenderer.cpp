// MapRenderer.cpp -- 剧场素材定位 + TMP 渲染 + 等距铺图。

#include "map/MapRenderer.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string.h>
#include <string_view>
#include <utility>
#include <unordered_map>

#include "gfx/ShpFile.h"
#include "map/LatTiles.h"

namespace ra2 {
namespace {

struct TheaterInfo {
    const char* name;
    const char* ext;
    const char* palette;
};

// 剧场名 -> 瓦片扩展名 / 调色板。全部经 CRC 实测命中（tools/theaterprobe.py）。
const TheaterInfo kTheaters[] = {
    {"TEMPERATE", "tem", "isotem.pal"},
    {"SNOW", "sno", "isosno.pal"},
    {"URBAN", "urb", "isourb.pal"},
    {"DESERT", "des", "isodes.pal"},
    {"LUNAR", "lun", "isolun.pal"},
    {"NEWURBAN", "ubn", "isoubn.pal"},
};

const TheaterInfo* Find_Theater(const std::string& name) {
    for (const TheaterInfo& t : kTheaters) {
        if (_stricmp(name.c_str(), t.name) == 0) {
            return &t;
        }
    }
    return nullptr;
}

constexpr int kTopMargin = 96;      ///< 给"探出到上一格"的 extra 图形留的余量
constexpr int kBottomMargin = 64;

uint64_t Pack(int a, int b) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(a)) << 32) |
           static_cast<uint32_t>(b);
}

uint32_t Pack_RGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8) |
           (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(a) << 24);
}

/// NewTheater=yes：文件名第 2 个字母换成剧场字母（NAWALL→NUWALL）。
char Theater_New_Letter(const std::string& theater) {
    if (_stricmp(theater.c_str(), "TEMPERATE") == 0) {
        return 'T';
    }
    if (_stricmp(theater.c_str(), "SNOW") == 0) {
        return 'A';
    }
    if (_stricmp(theater.c_str(), "URBAN") == 0) {
        return 'U';
    }
    if (_stricmp(theater.c_str(), "DESERT") == 0) {
        return 'D';
    }
    if (_stricmp(theater.c_str(), "LUNAR") == 0) {
        return 'L';
    }
    if (_stricmp(theater.c_str(), "NEWURBAN") == 0) {
        return 'N';
    }
    return 'T';
}

std::string New_Theater_Name(const std::string& image, const std::string& theater) {
    if (image.size() < 2) {
        return image;
    }
    std::string out = image;
    out[1] = Theater_New_Letter(theater);
    return out;
}

int Overlay_Byte(const MapFile& map, int dx, int row, const std::vector<uint8_t>& ov) {
    if (ov.empty() || dx < 0 || row < 0) {
        return 0xFF;
    }
    const int W = map.Width();
    const int H = map.Height();
    const int iso_w = map.Iso_Width();
    const size_t n = ov.size();
    // 完整 dy；反解 IsoMapPack (X,Y)：X=(dx+dy)/2+1，Y=(dy-dx)/2+W
    const int dy = row * 2 + (dx & 1);
    const int iso_x = (dx + dy) / 2 + 1;
    const int iso_y = (dy - dx) / 2 + W;
    if (W > 0 && iso_w > 0 &&
        n == static_cast<size_t>(iso_w) * static_cast<size_t>(H)) {
        const size_t i = static_cast<size_t>(row) * static_cast<size_t>(iso_w) +
                         static_cast<size_t>(dx);
        return (i < n) ? ov[i] : 0xFF;
    }
    // RA2 CellClass：按等距 (X,Y) 排，宽 512。
    if (iso_x < 0 || iso_y < 0) {
        return 0xFF;
    }
    size_t stride = 512;
    if (n < 512ull * 512ull) {
        stride = static_cast<size_t>(W + H);
        if (stride == 0) {
            return 0xFF;
        }
    }
    const size_t i = static_cast<size_t>(iso_x) + static_cast<size_t>(iso_y) * stride;
    return (i < n) ? ov[i] : 0xFF;
}

}  // namespace

bool MapRenderer::Bind(const std::vector<MixFileClass*>& roots, MapFile& map,
                       std::string* err) {
    roots_ = roots;
    subs_.clear();
    index_.clear();
    cache_.clear();
    tiles_loaded_ = 0;
    tiles_missing_ = 0;
    cells_drawn_ = 0;
    cells_empty_ = 0;

    theater_ = map.Theater();
    const TheaterInfo* ti = Find_Theater(theater_);
    if (ti == nullptr) {
        if (err) *err = "不认识的剧场: " + theater_;
        return false;
    }
    ext_ = ti->ext;
    pal_name_ = ti->palette;

    if (!Build_Index()) {
        if (err) *err = "没有可用的子归档";
        return false;
    }

    // 剧场调色板
    std::vector<uint8_t> pal_bytes;
    for (MixFileClass* r : roots_) {
        pal_bytes = r->Read_Deep(pal_name_.c_str());
        if (pal_bytes.size() >= 768) {
            break;
        }
    }
    if (pal_bytes.size() < 768) {
        if (err) *err = "找不到调色板 " + pal_name_;
        return false;
    }
    palette_.Load(pal_bytes.data(), pal_bytes.size());

    if (!Pick_Theater_Ini(map, err)) {
        return false;
    }
    // IsoMapPack5 是"编辑器底图"；gamemd 进图后跑 LAT 才换成过渡瓦。
    // A/B：设 RA2_SKIP_LAT=1 可跳过，用来确认锯齿是不是 LAT 位权/邻接搞错。
    if (theater_ini_) {
        const char* skip = std::getenv("RA2_SKIP_LAT");
        if (skip == nullptr || skip[0] != '1') {
            Apply_Lat(&map, *theater_ini_);
        } else {
            std::printf("  LAT 过渡：跳过（RA2_SKIP_LAT=1）\n");
        }
        cache_.clear();
    }
    return true;
}

bool MapRenderer::Build_Index() {
    for (MixFileClass* r : roots_) {
        for (const MixEntry& e : r->Entries()) {
            std::unique_ptr<MixFileClass> sub = r->Open_Sub(e);
            if (!sub) {
                continue;
            }
            const int idx = static_cast<int>(subs_.size());
            for (const MixEntry& se : sub->Entries()) {
                index_.emplace(se.id, std::make_pair(idx, &se));
            }
            Sub s;
            s.mix = std::move(sub);
            subs_.push_back(std::move(s));
        }
    }
    return !subs_.empty();
}

bool MapRenderer::Pick_Theater_Ini(const MapFile& map, std::string* err) {
    // 地图真正用到的瓦片下标（去重）。
    std::vector<int> used;
    {
        std::vector<bool> seen(static_cast<size_t>(map.Max_Tile_Index() + 2), false);
        for (const IsoCell& c : map.Cells()) {
            if (c.tile < 0) {
                continue;
            }
            if (static_cast<size_t>(c.tile) >= seen.size()) {
                seen.resize(static_cast<size_t>(c.tile) + 2, false);
            }
            seen[static_cast<size_t>(c.tile)] = true;
        }
        for (size_t i = 0; i < seen.size(); ++i) {
            if (seen[i]) {
                used.push_back(static_cast<int>(i));
            }
        }
    }
    if (used.empty()) {
        if (err) *err = "地图里没有有效瓦片";
        return false;
    }

    // 【必须先按剧场名锁定 INI】temperat / urban 前 574 个下标 FileName 相同，
    // 只靠"扩展名命中数"会把 temperat.ini 当成 URBAN 图的最优解，574+ 全错
    // （Arena 实测 MDrodc 变成 Wcave，路面长出洞穴/警戒条纹）。
    // MIX 只存 CRC，所以用已知文件名算 CRC 去撞。
    const char* prefer[4] = {nullptr, nullptr, nullptr, nullptr};
    if (_stricmp(theater_.c_str(), "URBAN") == 0) {
        prefer[0] = "urbanmd.ini";
        prefer[1] = "urban.ini";
    } else if (_stricmp(theater_.c_str(), "TEMPERATE") == 0) {
        prefer[0] = "temperatmd.ini";
        prefer[1] = "temperat.ini";
    } else if (_stricmp(theater_.c_str(), "SNOW") == 0) {
        prefer[0] = "snowmd.ini";
        prefer[1] = "snow.ini";
    } else if (_stricmp(theater_.c_str(), "DESERT") == 0) {
        prefer[0] = "desertmd.ini";
        prefer[1] = "desert.ini";
    } else if (_stricmp(theater_.c_str(), "LUNAR") == 0) {
        prefer[0] = "lunarmd.ini";
        prefer[1] = "lunar.ini";
    } else if (_stricmp(theater_.c_str(), "NEWURBAN") == 0) {
        prefer[0] = "urbannmd.ini";
        prefer[1] = "urbann.ini";
    }

    auto try_load = [&](const std::vector<uint8_t>& data, int* score_out) -> bool {
        if (data.size() < 64) {
            return false;
        }
        const std::string_view sv(reinterpret_cast<const char*>(data.data()),
                                  data.size());
        if (sv.find("TilesInSet") == std::string_view::npos ||
            sv.find("[TileSet") == std::string_view::npos) {
            return false;
        }
        TheaterFile tf;
        if (!tf.Load(data.data(), data.size())) {
            return false;
        }
        int score = 0;
        for (int idx : used) {
            if (idx >= tf.Tile_Count()) {
                continue;
            }
            const std::string name = tf.Tile_Name(idx) + "." + ext_;
            if (index_.count(MixFileClass::CRC_Of(name.c_str())) != 0) {
                ++score;
            }
        }
        if (score_out) {
            *score_out = score;
        }
        return score > 0;
    };

    for (int pi = 0; prefer[pi] != nullptr; ++pi) {
        const uint32_t id = MixFileClass::CRC_Of(prefer[pi]);
        int si = -1;
        const MixEntry* e = Find_Entry(id, &si);
        if (e == nullptr || si < 0) {
            continue;
        }
        std::vector<uint8_t> data = subs_[static_cast<size_t>(si)].mix->Read_Entry(*e);
        int score = 0;
        if (!try_load(data, &score)) {
            continue;
        }
        theater_ini_.reset(new TheaterFile());
        if (theater_ini_->Load(data.data(), data.size())) {
            std::printf("  剧场 INI %s（命中 %d/%d 瓦片）Tile[0]=%s Tile[2]=%s "
                        "Tile[22]=%s 共 %d\n",
                        prefer[pi], score, static_cast<int>(used.size()),
                        theater_ini_->Tile_Name(0).c_str(),
                        theater_ini_->Tile_Name(2).c_str(),
                        theater_ini_->Tile_Name(22).c_str(),
                        theater_ini_->Tile_Count());
            return true;
        }
        theater_ini_.reset();
    }

    int best_score = -1;
    std::vector<uint8_t> best_data;
    for (const Sub& s : subs_) {
        for (const MixEntry& e : s.mix->Entries()) {
            if (e.size < 4096 || e.size > 2u * 1024u * 1024u) {
                continue;
            }
            std::vector<uint8_t> data = s.mix->Read_Entry(e);
            int score = 0;
            if (!try_load(data, &score)) {
                continue;
            }
            if (score > best_score) {
                best_score = score;
                best_data = std::move(data);
            }
        }
    }

    if (best_score <= 0 || best_data.empty()) {
        if (err) *err = "没找到匹配本图的剧场 INI";
        return false;
    }
    theater_ini_.reset(new TheaterFile());
    if (!theater_ini_->Load(best_data.data(), best_data.size())) {
        if (err) *err = "剧场 INI 解析失败";
        return false;
    }
    std::printf("  剧场 INI 模糊匹配（命中 %d/%d 瓦片）\n", best_score,
                static_cast<int>(used.size()));
    return true;
}

std::string MapRenderer::Tile_File_Name(int index) const {
    if (!theater_ini_) {
        return std::string();
    }
    const std::string base = theater_ini_->Tile_Name(index);
    if (base.empty()) {
        return std::string();
    }
    return base + "." + ext_;
}

const MixEntry* MapRenderer::Find_Entry(uint32_t id, int* sub_index) const {
    const auto it = index_.find(id);
    if (it == index_.end()) {
        return nullptr;
    }
    *sub_index = it->second.first;
    return it->second.second;
}

std::vector<uint8_t> MapRenderer::Read_Asset(const char* filename) const {
    int si = -1;
    const MixEntry* e = Find_Entry(MixFileClass::CRC_Of(filename), &si);
    if (e != nullptr && si >= 0 && static_cast<size_t>(si) < subs_.size()) {
        std::vector<uint8_t> raw = subs_[static_cast<size_t>(si)].mix->Read_Entry(*e);
        if (!raw.empty()) {
            return raw;
        }
    }
    for (MixFileClass* r : roots_) {
        std::vector<uint8_t> raw = r->Read_Deep(filename);
        if (!raw.empty()) {
            return raw;
        }
    }
    return {};
}

void MapRenderer::Blit_Overlays(const MapFile& map, std::vector<uint32_t>* out,
                                int width, int height) {
    overlays_drawn_ = 0;
    overlays_missing_ = 0;
    if (out == nullptr || overlay_images_.empty() || map.Overlay().empty()) {
        return;
    }
    const int H = map.Height();
    const int iso_w = map.Iso_Width();
    const std::vector<IsoCell>& cells = map.Cells();
    std::unordered_map<int, ShpFile> shps;
    std::unordered_map<int, bool> tried;

    for (int cy = 0; cy < H; ++cy) {
        for (int cx = 0; cx < iso_w; ++cx) {
            const int ov = Overlay_Byte(map, cx, cy, map.Overlay());
            if (ov == 0xFF || ov < 0) {
                continue;
            }
            if (static_cast<size_t>(ov) >= overlay_images_.size()) {
                continue;
            }
            const std::string& image = overlay_images_[static_cast<size_t>(ov)];
            if (image.empty()) {
                continue;
            }
            // FENCE20/21 官方素材是西木 DEMO 占位（品红 "replace fence"），
            // 画出来只会污染场面；跳过，等有真栅栏 SHP 再开。
            if (_stricmp(image.c_str(), "FENCE20") == 0 ||
                _stricmp(image.c_str(), "FENCE21") == 0) {
                continue;
            }
            if (!tried[ov]) {
                tried[ov] = true;
                std::string stem = image;
                const bool nt =
                    static_cast<size_t>(ov) < overlay_new_theater_.size() &&
                    overlay_new_theater_[static_cast<size_t>(ov)] != 0;
                if (nt) {
                    stem = New_Theater_Name(image, theater_);
                }
                std::string fname = stem + ".SHP";
                std::vector<uint8_t> raw = Read_Asset(fname.c_str());
                if (raw.empty()) {
                    fname = stem + "." + ext_;
                    raw = Read_Asset(fname.c_str());
                }
                // NewTheater 没命中再退回原名（少数素材两边都有）。
                if (raw.empty() && nt && stem != image) {
                    fname = image + ".SHP";
                    raw = Read_Asset(fname.c_str());
                    if (raw.empty()) {
                        fname = image + "." + ext_;
                        raw = Read_Asset(fname.c_str());
                    }
                }
                ShpFile shp;
                if (!raw.empty() && shp.Load(raw.data(), raw.size()) &&
                    shp.Frame_Count() > 0) {
                    shps[ov] = std::move(shp);
                } else {
                    ++overlays_missing_;
                }
            }
            const auto it = shps.find(ov);
            if (it == shps.end()) {
                continue;
            }
            const ShpFile& shp = it->second;
            int frame = Overlay_Byte(map, cx, cy, map.Overlay_Data());
            if (frame == 0xFF) {
                frame = 0;
            }
            if (frame < 0) {
                frame = 0;
            }
            if (frame >= shp.Frame_Count()) {
                // LOBRDG*.URB 实测一律 6 帧；Arena OverlayData 对 LOBRDG26 出现 6..11。
                // OpenRA/桥损毁研究：高 6 档是同造型的损毁态 → frame % 6 落到 0..5。
                // FENCE20/21 只有 2 帧但 Data 可到 12（占位 DEMO 图），同样取模避免越界。
                frame %= shp.Frame_Count();
            }
            const ShpFrameInfo& fi = shp.Frame_Info(frame);
            const std::vector<uint8_t>& px = shp.Frame_Pixels(frame);
            if (fi.w <= 0 || fi.h <= 0 ||
                px.size() < static_cast<size_t>(fi.w) * fi.h) {
                continue;
            }
            const size_t cidx = static_cast<size_t>(cy) * static_cast<size_t>(iso_w) +
                                static_cast<size_t>(cx);
            const int level = (cidx < cells.size()) ? cells[cidx].level : 0;
            const int cell_dy = cy * 2 + (cx & 1);
            // 与对象精灵 / CC_Draw_Shape 一致：整幅 SHP 画布中心落在格心。
            const int sx = origin_x_ + cx * kCellHalfW + kCellHalfW
                           - shp.Width() / 2 + static_cast<int>(fi.x);
            const int sy = origin_y_ + (cell_dy - level) * kCellHalfH + kCellHalfH
                           - shp.Height() / 2 + static_cast<int>(fi.y);
            int painted = 0;
            for (int y = 0; y < fi.h; ++y) {
                const int dy = sy + y;
                if (dy < 0 || dy >= height) {
                    continue;
                }
                for (int x = 0; x < fi.w; ++x) {
                    const uint8_t idx = px[static_cast<size_t>(y) * fi.w + x];
                    if (idx == 0) {
                        continue;
                    }
                    const int dx = sx + x;
                    if (dx < 0 || dx >= width) {
                        continue;
                    }
                    const Palette::Color c = palette_.Map(idx);
                    (*out)[static_cast<size_t>(dy) * width + dx] =
                        Pack_RGBA(c.r, c.g, c.b, c.a);
                    ++painted;
                }
            }
            if (painted > 0) {
                ++overlays_drawn_;
            }
        }
    }
}

const TerrainTileRGBA* MapRenderer::Tile_RGBA(int index, int sub) {
    const uint64_t key = Pack(index, sub);
    const auto it = cache_.find(key);
    if (it != cache_.end()) {
        return it->second.ok ? &it->second : nullptr;
    }

    TerrainTileRGBA out;
    const std::string fname = Tile_File_Name(index);
    if (!fname.empty()) {
        int si = -1;
        const MixEntry* e = Find_Entry(MixFileClass::CRC_Of(fname.c_str()), &si);
        if (e != nullptr && si >= 0) {
            std::vector<uint8_t> raw = subs_[si].mix->Read_Entry(*e);
            TmpFile tmp;
            if (tmp.Load(raw.data(), raw.size())) {
                // 【sub 是变体/朝向，不是可有可无的】实测 Arena 里同一个
                // TileIndex 下挂了 0..7 共八种 SubTile（461 个 (tile,sub) 组合里
                // 有 335 个 sub>0）。所以必须按 sub 取对应那份图，
                // 一律取 0 会让全图每块地都朝同一个方向 —— 这是用户一眼看出来的
                // "地板连不起来 / 城市是错的"。
                int cell = sub;
                if (cell < 0 || cell >= tmp.Tile_Count()) {
                    // 越界才退 0；但要计数，因为"经常退 0"说明 TMP 的变体数
                    // 没读对，是另一个 bug，不能默默吞掉。
                    cell = 0;
                    ++sub_clamped_;
                }
                if (cell >= 0 && cell < tmp.Tile_Count() &&
                    tmp.Tiles()[static_cast<size_t>(cell)].present) {
                    out.land = tmp.Tiles()[static_cast<size_t>(cell)].header.land_type;
                }
                // 外扩 64 像素，保住探到上一格的树冠/岩壁（见 TmpFile.h 关键发现 3）。
                // 同步写出 TMP Z，铺图时走 ZBuffer（CNCMaps TmpRenderer /
                // gamemd "ZBuffer (%dx%d)"），否则 Extra 探出菱形后会锯齿撕碎路面。
                out.pixels = tmp.Render_Cell_Padded_RGBA(cell, palette_, kCellPad,
                                                         &out.origin_x, &out.origin_y,
                                                         &out.width, &out.height,
                                                         &out.z);
                out.ok = !out.pixels.empty();
            }
        }
    }

    if (out.ok) {
        ++tiles_loaded_;
    } else {
        ++tiles_missing_;
    }
    return &cache_.emplace(key, std::move(out)).first->second;
}

bool MapRenderer::Dump_Tile_RGBA(int tile, int sub, const char* path) {
    // 走和 Tile_RGBA 完全一样的取图路径，再把结果落盘成原生尺寸的裸 RGBA。
    // 调用方拿 tools/rawdump.py 转 PNG 就能看。
    const TerrainTileRGBA* img = Tile_RGBA(tile, sub);
    if (img == nullptr || !img->ok || img->width <= 0 || img->height <= 0) {
        std::printf("[x] 瓦片 (tile=%d, sub=%d) 取不到\n", tile, sub);
        return false;
    }
    FILE* f = std::fopen(path, "wb");
    if (f == nullptr) {
        return false;
    }
    std::fwrite(img->pixels.data(), 4, img->pixels.size(), f);
    std::fclose(f);
    std::printf("[OK] 瓦片 (tile=%d, sub=%d) %dx%d -> %s\n", tile, sub,
                img->width, img->height, path);
    return true;
}

bool MapRenderer::Render(const MapFile& map, int rect_x, int rect_y, int rect_w, int rect_h,
                         std::vector<uint32_t>* out, int* out_w, int* out_h,
                         std::string* err) {
    if (!theater_ini_) {
        if (err) *err = "还没绑定剧场";
        return false;
    }
    const int W = map.Width();
    const int H = map.Height();
    const int iso_w = map.Iso_Width();

    int max_level = 0;
    for (const IsoCell& c : map.Cells()) {
        max_level = std::max(max_level, static_cast<int>(c.level));
    }

    // CNCMaps DrawingSurface：W*TileWidth × H*TileHeight，原点在 (0,0)。
    // dx 最大 2W-2 → sx=(2W-2)*30，再加一格宽 60 → 恰 W*60。
    const int origin_x = 0;
    const int origin_y = kTopMargin + max_level * kLevelHeightPx;
    const int width = W * kCellW;
    const int height = H * kCellH + kTopMargin + kBottomMargin
                       + max_level * kLevelHeightPx;

    out->assign(static_cast<size_t>(width) * static_cast<size_t>(height), 0u);
    // ZBuffer：zBase = (Rx+Ry)*(BlockHeight/2)；Rx+Ry = dy + W + 1。
    std::vector<int16_t> zbuf(static_cast<size_t>(width) * static_cast<size_t>(height),
                              static_cast<int16_t>(-32768));
    *out_w = width;
    *out_h = height;
    origin_x_ = origin_x;
    origin_y_ = origin_y;

    const bool clip = (rect_w > 0 && rect_h > 0);
    // rect 仍按 Map Size 的“逻辑”裁剪时，转成 iso dx 范围粗滤。
    const int x0 = clip ? std::max(0, rect_x) : 0;
    const int y0 = clip ? std::max(0, rect_y) : 0;
    const int x1 = clip ? std::min(iso_w, rect_x + rect_w) : iso_w;
    const int y1 = clip ? std::min(H, rect_y + rect_h) : H;

    // 画家序：按完整 dy 升序（屏幕 y），再按 dx。
    std::vector<int> order;
    order.reserve(map.Cells().size());
    for (size_t i = 0; i < map.Cells().size(); ++i) {
        const IsoCell& c = map.Cells()[i];
        if (c.tile < 0) {
            continue;
        }
        if (c.cx < x0 || c.cx >= x1 || c.cy < y0 || c.cy >= y1) {
            continue;
        }
        order.push_back(static_cast<int>(i));
    }
    const std::vector<IsoCell>& all = map.Cells();
    std::sort(order.begin(), order.end(), [&all](int a, int b) {
        const int da = all[a].Dy();
        const int db = all[b].Dy();
        if (da != db) {
            return da < db;
        }
        if (all[a].level != all[b].level) {
            return all[a].level < all[b].level;
        }
        return all[a].cx < all[b].cx;
    });

    int drawn = 0;
    for (int idx : order) {
        const IsoCell& c = all[idx];
        const TerrainTileRGBA* img = Tile_RGBA(c.tile, c.sub);
        if (img == nullptr) {
            continue;
        }
        const int dy = c.Dy();
        const int sx = origin_x + c.cx * kCellHalfW - img->origin_x;
        const int sy = origin_y + (dy - static_cast<int>(c.level)) * kCellHalfH
                       - img->origin_y;
        // Rx+Ry = dy + W + 1（由 dx=X-Y+W-1, dy=X+Y-W-1 推出）。
        const int z_base = (dy + W + 1) * (kCellH / 2);
        int painted = 0;
        const bool have_z = img->z.size() == img->pixels.size();
        for (int y = 0; y < img->height; ++y) {
            const int out_y = sy + y;
            if (out_y < 0 || out_y >= height) {
                continue;
            }
            for (int x = 0; x < img->width; ++x) {
                const int out_x = sx + x;
                if (out_x < 0 || out_x >= width) {
                    continue;
                }
                const size_t pi =
                    static_cast<size_t>(y) * static_cast<size_t>(img->width) +
                    static_cast<size_t>(x);
                const uint32_t px = img->pixels[pi];
                if ((px & 0xFF000000u) == 0) {
                    continue;
                }
                const size_t di =
                    static_cast<size_t>(out_y) * static_cast<size_t>(width) +
                    static_cast<size_t>(out_x);
                const uint8_t zd = have_z ? img->z[pi] : 0;
                const int16_t z_val =
                    static_cast<int16_t>(z_base - static_cast<int>(zd));
                static const bool kNoZ = [] {
                    const char* e = std::getenv("RA2_NO_Z");
                    return e != nullptr && e[0] == '1';
                }();
                if (!kNoZ && z_val < zbuf[di]) {
                    continue;
                }
                (*out)[di] = px;
                if (!kNoZ) {
                    zbuf[di] = z_val;
                }
                ++painted;
            }
        }
        ++drawn;
        if (painted == 0) {
            ++cells_empty_;
        }
    }
    cells_drawn_ = drawn;
    Blit_Overlays(map, out, width, height);
    if (drawn == 0) {
        if (err) *err = "一个瓦片都没画上";
        return false;
    }
    return true;
}

}  // namespace ra2
