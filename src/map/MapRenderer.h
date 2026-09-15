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
// 【为什么剧场 INI 要"挑"而不是按名字找】
//   MIX 只存 CRC，temperat.ini / urban.ini 这些名字一个都没撞上，
//   但内容里都带 TilesInSet。所以把所有候选 INI 都解出来，
//   拿地图真正用到的 TileIndex 去问"名字+扩展名在不在包里"，命中多者胜。

#pragma once

#include <cstdint>
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
    bool ok = false;
};

/// cell 位图四边外扩的像素数：extra 常见 ExtraY=-60，64 足够。
constexpr int kCellPad = 64;

class MapRenderer {
public:
    /// 绑定素材：顶层 MIX 列表 + 已解析的地图。
    /// 会把所有嵌套子归档打开一遍建索引，之后取瓦片就是查表。
    bool Bind(const std::vector<MixFileClass*>& roots, const MapFile& map,
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

private:
    struct Sub {
        std::unique_ptr<MixFileClass> mix;
    };

    bool Build_Index();
    bool Pick_Theater_Ini(const MapFile& map, std::string* err);
    const MixEntry* Find_Entry(uint32_t id, int* sub_index) const;

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
};

}  // namespace ra2
