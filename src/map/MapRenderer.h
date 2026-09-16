// MapRenderer.h -- 把 MapFile + 剧场素材合成一张战场图
//
// 这一层干三件事，都是"让游戏跑起来"绕不过去的：
//   1. 从剧场 INI 把 TileIndex 翻成 TMP 文件名（并挑对剧场 INI）
//   2. 从 iso*.mix / isogen.mix 里把 TMP 取出来，用剧场调色板渲成 RGBA
//   3. 按画家算法（cx+cy 升序，正好是 IsoMapPack5 的存储顺序）铺到等距画布上
//
// 【剧场名 -> 扩展名 / 调色板】实测 CRC 命中（tools/theaterprobe.py）：
//   TEMPERATE .tem  isotem.pal     SNOW .sno  isosno.pal    URBAN .urb isourb.pal
//   DESERT    .des  isodes.pal     LUNAR .lun isolun.pal    NEWURBAN .ubn isoubn.pal
//
// 【剧场 INI 怎么挑】
//   先按 Theater= 用已知文件名 CRC 撞 urbanmd.ini / temperat.ini …；
//   撞不上再退回"TilesInSet 候选 + 瓦片命中数"模糊匹配。
//   绝不能只靠命中数：temperat/urban 前 574 项 FileName 相同，模糊匹配会选错。

#pragma once

#include <cstdint>
#include <cmath>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "gfx/Palette.h"
#include "gfx/TmpFile.h"
#include "io/FileSystem.h"
#include "map/MapFile.h"
#include "map/TheaterFile.h"

namespace ra2 {

/// 一块已经渲好、可以直接贴的地形图。
struct TerrainTileRGBA {
    int width = 0;
    int height = 0;
    int origin_x = 0;   ///< cell 左上角在这张图里的坐标（外扩边距）
    int origin_y = 0;
    std::vector<uint32_t> pixels;   ///< RGBA8，行优先
    /// 与 pixels 平行的 TMP Z 值（CNCMaps TmpRenderer / gamemd ZBuffer）。
    /// 透明像素可任意；不透明且无 Z 数据时为 0（zBufVal = zBase - 0）。
    std::vector<uint8_t> z;
    bool ok = false;
    uint8_t land = 0;               ///< TMP TileCellHeader.LandType
};

/// cell 位图四边外扩的像素数：extra 常见 ExtraY=-60，64 足够。
constexpr int kCellPad = 64;

/// 等距菱形的几何。原版 RA2 一格就是 60×30 的菱形，半个格子 30×15。
/// 高度每升一级，画面向上抬 15 像素（见 MapFile.h kLevelHeightPx）。
constexpr int kCellW = 60;
constexpr int kCellH = 30;
constexpr int kCellHalfW = 30;   ///< kCellW / 2
constexpr int kCellHalfH = 15;   ///< kCellH / 2
// kLevelHeightPx（每级高度抬升的像素）在 MapFile.h 里，别在这儿重复定义。

class MapRenderer {
public:
    /// 绑定素材：顶层 MIX 列表 + 已解析的地图。
    /// 会把所有嵌套子归档打开一遍建索引，之后取瓦片就是查表。
    bool Bind(const std::vector<MixFileClass*>& roots, MapFile& map,
              std::string* err = nullptr);

    const std::string& Theater() const noexcept { return theater_; }
    const std::string& Extension() const noexcept { return ext_; }
    const std::string& Palette_Name() const noexcept { return pal_name_; }
    int Tile_Count() const noexcept { return theater_ini_ ? theater_ini_->Tile_Count() : 0; }

    /// TileIndex -> 完整文件名（含扩展名）。越界返回空串。
    std::string Tile_File_Name(int index) const;

    /// 取（缓存）某个瓦片某个子格的 RGBA。失败返回 nullptr。
    const TerrainTileRGBA* Tile_RGBA(int index, int sub);

    /// 铺整张地图（或其中一块矩形）。out_w/out_h 是画布尺寸。
    /// rect 全 0 时铺全图。
    bool Render(const MapFile& map, int rect_x, int rect_y, int rect_w, int rect_h,
                std::vector<uint32_t>* out, int* out_w, int* out_h,
                std::string* err = nullptr);

    /// 诊断计数：命中 / 缺失 的瓦片数。
    int Tiles_Loaded() const noexcept { return tiles_loaded_; }
    int Tiles_Missing() const noexcept { return tiles_missing_; }

    /// 上一次 Render 的覆盖统计。
    ///
    /// 为什么需要：`Tiles_Missing() == 0` 只证明"瓦片都取到了"，
    /// 证明不了"画布被填满了"。悬崖这类多格模板如果 sub 取错，
    /// 瓦片明明在、画上去却是空的 —— 画布上会留下一排排菱形黑洞。
    /// 所以必须独立统计"有多少格真的画上了不透明像素"。
    int Cells_Drawn() const noexcept { return cells_drawn_; }
    int Cells_Empty() const noexcept { return cells_empty_; }
    /// SubTile 越界退 0 的次数（0 才是正常）。见 sub_clamped_ 的注释。
    int Sub_Clamped() const noexcept { return sub_clamped_; }

    /// 把指定 (TileIndex, SubTile) 的瓦片渲成带 padding 的 RGBA 落盘。
    /// "地图瓦块方向错"这种问题，光看整图分不清是哪一格、哪个变体散了；
    /// 把单个变体抠出来和原版 [PreviewPack] 缩略图里同一块地比，才能定位。
    /// 非 const：要走和铺图一样的取图路径，而 Tile_RGBA 会写缓存。
    bool Dump_Tile_RGBA(int tile, int sub, const char* path);

    /// ---- 等距几何：给游戏层做"屏幕 <-> 格子"换算用 ----
    ///
    /// 上次 Render 用的画布原点（格子 (0,0) 在画布里的像素位置）。
    /// 游戏层要拿它把鼠标点击换算成格子，所以必须能问出来，不能自己去猜
    /// kTopMargin 那些内部常量。
    int Origin_X() const noexcept { return origin_x_; }
    int Origin_Y() const noexcept { return origin_y_; }

    /// 格子 -> 画布像素（菱形左上角）。cx=dx，cy=dy/2；完整 dy=2*cy+(cx&1)。
    static void Cell_To_Canvas(int origin_x, int origin_y, int cx, int cy, int level,
                               int* out_x, int* out_y) {
        const int dy = cy * 2 + (cx & 1);
        *out_x = origin_x + cx * kCellHalfW;
        *out_y = origin_y + (dy - level) * kCellHalfH;
    }

    /// OverlayPack 画上的格数 / 缺 SHP 的类型数（上次 Render）。
    int Overlays_Drawn() const noexcept { return overlays_drawn_; }
    int Overlays_Missing() const noexcept { return overlays_missing_; }

    /// [OverlayTypes] 编号 -> Image=。Render 之前设好，空表就跳过覆盖物。
    void Set_Overlay_Images(std::vector<std::string> names) {
        overlay_images_ = std::move(names);
        overlay_new_theater_.assign(overlay_images_.size(), 0);
    }
    void Set_Overlay_Images(std::vector<std::string> names,
                            std::vector<uint8_t> new_theater) {
        overlay_images_ = std::move(names);
        overlay_new_theater_ = std::move(new_theater);
        if (overlay_new_theater_.size() < overlay_images_.size()) {
            overlay_new_theater_.resize(overlay_images_.size(), 0);
        }
    }

    /// 画布像素 -> 格子（level=0）。CNCMaps：dx = x/30，dy = y/15；
    /// 行 = dy/2，列 = dx。
    static void Canvas_To_Cell(int origin_x, int origin_y, int px, int py,
                               int* out_cx, int* out_cy) {
        const int dx = static_cast<int>(std::floor(
            static_cast<float>(px - origin_x) / kCellHalfW));
        const int dy = static_cast<int>(std::floor(
            static_cast<float>(py - origin_y) / kCellHalfH));
        *out_cx = dx;
        *out_cy = dy / 2;
    }

private:
    struct Sub {
        std::unique_ptr<MixFileClass> mix;
    };

    bool Build_Index();
    bool Pick_Theater_Ini(const MapFile& map, std::string* err);
    const MixEntry* Find_Entry(uint32_t id, int* sub_index) const;
    std::vector<uint8_t> Read_Asset(const char* filename) const;
    void Blit_Overlays(const MapFile& map, std::vector<uint32_t>* out, int width,
                       int height);

    std::vector<MixFileClass*> roots_;
    std::vector<Sub> subs_;                                        ///< 所有嵌套子归档
    std::unordered_map<uint32_t, std::pair<int, const MixEntry*>> index_;

    std::string theater_;
    std::string ext_;
    std::string pal_name_;
    std::unique_ptr<TheaterFile> theater_ini_;
    Palette palette_;

    std::unordered_map<uint64_t, TerrainTileRGBA> cache_;
    int tiles_loaded_ = 0;
    int tiles_missing_ = 0;
    int origin_x_ = 0;      ///< 上次 Render 用的画布原点
    int origin_y_ = 0;
    int cells_drawn_ = 0;   ///< 上次 Render 里真的画上像素的格数
    int cells_empty_ = 0;   ///< 取了瓦片但一个不透明像素都没画出来的格数
    /// 因为 SubTile 越界而退回变体 0 的次数。这个数大就说明 TMP 的变体数没读对
    /// （SubTile 在 RA2 里是"同一块地的第几种朝向/变体"，不能一律取 0）。
    int sub_clamped_ = 0;
    std::vector<std::string> overlay_images_;
    std::vector<uint8_t> overlay_new_theater_;
    int overlays_drawn_ = 0;
    int overlays_missing_ = 0;
};

}  // namespace ra2
