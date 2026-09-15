// TheaterFile.h -- 剧场控制 INI（temperat.ini / urban.ini / snow.ini ...）
//
// 这个文件是 TileNum -> TMP 文件名 的唯一权威来源。
// IsoMapPack5 里的 TileIndex 是"跨瓦片集累加"的全局序号：
//   [TileSet0000] FileName=Clear  TilesInSet=1   -> 0    = Clear01
//   [TileSet0002] FileName=mslop  TilesInSet=8   -> 1..8 = mslop01..mslop08
// 集内文件名是 FileName + **两位序号，从 01 开始**（不是 00）。
//
// 【怎么认出哪个 INI 属于哪个剧场】
//   MIX 只存 CRC，文件名反查不全，剧场 INI 一个都没撞上名字。
//   所以改用**数据判据**：拿地图真正用到的 TileIndex 去问
//   "这个名字 + 该剧场扩展名在不在 isoXXX.mix 里"，命中率最高者胜出。
//   实测 ARENA.map(URBAN)：候选 1077 瓦片那份 162/162 全中，
//   838 瓦片那份只有 149/162（有 13 个下标直接越界）—— 判据干净。
//
// 实测三个 ra2.mix 里的剧场 INI（tools/theaterlocate.py）：
//   0x091D1025 1077 个瓦片 -> URBAN（ARENA 用到最大下标 1076，正好铺满）
//   0x785DAEFF  838 个瓦片 -> TEMPERATE
//   0x80DE7BF7  798 个瓦片 -> SNOW（[TileSet0000] SetName = "LAT Snow"）

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ra2 {

/// 一个 [TileSetnnnn]。
struct TheaterTileSet {
    std::string section;    ///< 原始段名，如 TileSet0002
    std::string set_name;   ///< SetName（编辑器里显示的名字）
    std::string file_name;  ///< FileName（模板基名，如 mslop）
    int count = 0;          ///< TilesInSet
    int base = 0;           ///< 本集第一个瓦片的全局下标
};

class TheaterFile {
public:
    /// 解析剧场 INI 文本。返回 false 表示里面没有 [TileSet] 段。
    bool Load(const uint8_t* data, size_t size);

    int Set_Count() const noexcept { return static_cast<int>(sets_.size()); }
    int Tile_Count() const noexcept { return tile_count_; }
    const std::vector<TheaterTileSet>& Sets() const noexcept { return sets_; }

    /// 全局下标 -> 模板基名（不含扩展名）。越界返回空串。
    std::string Tile_Name(int index) const;

    /// 全局下标 -> 所属集（用于按集做统计）。越界返回 nullptr。
    const TheaterTileSet* Set_Of(int index) const;

    /// [General] 里的关键项：ClearTile / RoughTile / HeightBase / CliffSet ...
    int General_Int(const char* key, int def = 0) const;

private:
    std::vector<TheaterTileSet> sets_;
    int tile_count_ = 0;
    std::vector<std::pair<std::string, int>> general_;
};

}  // namespace ra2
