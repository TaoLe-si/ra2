// MapRenderer.cpp -- 剧场素材定位 + TMP 渲染 + 等距铺图。

#include "map/MapRenderer.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string.h>
#include <string_view>
#include <utility>

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

}  // namespace

bool MapRenderer::Bind(const std::vector<MixFileClass*>& roots, const MapFile& map,
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

    return Pick_Theater_Ini(map, err);
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

    int best_score = -1;
    std::vector<uint8_t> best_data;
    for (const Sub& s : subs_) {
        for (const MixEntry& e : s.mix->Entries()) {
            if (e.size < 4096 || e.size > 2u * 1024u * 1024u) {
                continue;
            }
            std::vector<uint8_t> data = s.mix->Read_Entry(e);
            if (data.size() < 64) {
                continue;
            }
            // 剧场 INI 的指纹：有 TilesInSet 也有 [TileSet
            const std::string_view sv(reinterpret_cast<const char*>(data.data()),
                                      data.size());
            if (sv.find("TilesInSet") == std::string_view::npos ||
                sv.find("[TileSet") == std::string_view::npos) {
                continue;
            }
            std::unique_ptr<TheaterFile> tf(new TheaterFile());
            if (!tf->Load(data.data(), data.size())) {
                continue;
            }
            int score = 0;
            for (int idx : used) {
                if (idx >= tf->Tile_Count()) {
                    continue;                 // 下标越界，这份 INI 覆盖不了
                }
                const std::string name = tf->Tile_Name(idx) + "." + ext_;
                if (index_.count(MixFileClass::CRC_Of(name.c_str())) != 0) {
                    ++score;
                }
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
                // sub 是模板里的第几个 cell（多格模板才 >0）。
                int cell = sub;
                if (cell < 0 || cell >= tmp.Tile_Count()) {
                    cell = 0;
                }
                // 外扩 64 像素，保住探到上一格的树冠/岩壁（见 TmpFile.h 关键发现 3）。
                out.pixels = tmp.Render_Cell_Padded_RGBA(cell, palette_, kCellPad,
                                                         &out.origin_x, &out.origin_y,
                                                         &out.width, &out.height);
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

bool MapRenderer::Render(const MapFile& map, int rect_x, int rect_y, int rect_w, int rect_h,
                         std::vector<uint32_t>* out, int* out_w, int* out_h,
                         std::string* err) {
    if (!theater_ini_) {
        if (err) *err = "还没绑定剧场";
        return false;
    }
    const int W = map.Width();
    const int H = map.Height();

    int max_level = 0;
    for (const IsoCell& c : map.Cells()) {
        max_level = std::max(max_level, static_cast<int>(c.level));
    }

    const int origin_x = 30 * (H - 1);
    const int origin_y = kTopMargin + max_level * kLevelHeightPx;
    const int width = 30 * (W + H);
    const int height = 15 * (W + H - 2) + 30 + kTopMargin + kBottomMargin
                       + max_level * kLevelHeightPx;

    out->assign(static_cast<size_t>(width) * static_cast<size_t>(height), 0u);
    *out_w = width;
    *out_h = height;
    // 存下来给游戏层做屏幕 <-> 格子换算（鼠标拾取、相机定位都要）
    origin_x_ = origin_x;
    origin_y_ = origin_y;

    const bool clip = (rect_w > 0 && rect_h > 0);
    const int x0 = clip ? rect_x : 0;
    const int y0 = clip ? rect_y : 0;
    const int x1 = clip ? rect_x + rect_w : W;
    const int y1 = clip ? rect_y + rect_h : H;

    // 【画家序】等距投影下屏幕 y = 15*(cx+cy)，所以必须按 (cx+cy) 升序落格，
    // 同一条对角线上再按 cx 升序（同一斜列的格子不重叠，但外扩 64 像素会碰到）。
    //
    // Cells() 是按 (cy, cx) **行优先**存的，直接顺序遍历会出事：
    // (5,0)（cx+cy=5，靠前）会排在 (0,1)（cx+cy=1，靠后）**之前**画，
    // 于是远处的格子后画、把近处的盖掉 —— 地图整片错乱。
    // 这不是观感问题，是"谁该压住谁"的硬错误。
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
        const int sa = all[a].cx + all[a].cy;
        const int sb = all[b].cx + all[b].cy;
        if (sa != sb) {
            return sa < sb;
        }
        if (all[a].level != all[b].level) {
            return all[a].level < all[b].level;   // 同一斜列里高的后画
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
        const int sx = origin_x + 30 * (c.cx - c.cy) - img->origin_x;
        const int sy = origin_y + 15 * (c.cx + c.cy)
                       - static_cast<int>(c.level) * kLevelHeightPx - img->origin_y;
        int painted = 0;
        for (int y = 0; y < img->height; ++y) {
            const int dy = sy + y;
            if (dy < 0 || dy >= height) {
                continue;
            }
            for (int x = 0; x < img->width; ++x) {
                const int dx = sx + x;
                if (dx < 0 || dx >= width) {
                    continue;
                }
                const uint32_t px = img->pixels[static_cast<size_t>(y) * img->width + x];
                if ((px & 0xFF000000u) == 0) {
                    continue;
                }
                (*out)[static_cast<size_t>(dy) * width + dx] = px;
                ++painted;
            }
        }
        ++drawn;
        // 画布上留下菱形黑洞的元凶就是"瓦片取到了、画上去却是空的"，
        // 所以这里分开记：取到瓦片不算数，真的糊上像素才算。
        if (painted == 0) {
            ++cells_empty_;
        }
    }
    cells_drawn_ = drawn;
    if (drawn == 0) {
        if (err) *err = "一个瓦片都没画上";
        return false;
    }
    return true;
}

}  // namespace ra2
