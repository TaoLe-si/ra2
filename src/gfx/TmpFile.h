// TmpFile.h -- TMP(TS) 等距地形瓦片
//
// RA2/YR 的地形不是位图而是"等距菱形瓦片"：每个 .TMP 是一个瓦片模板，
// 模板里有一个或多个 cell，每个 cell 画一张 60x30 的菱形，拼起来就是地砖。
//
// 格式（moddingwiki + 本项目实测，工具见 tools/tmpdump.py、tools/tmppara.py）：
//   FileHeader 16 字节：
//     +0  int32 BlockWidth        横向 cell 数
//     +4  int32 BlockHeight       纵向 cell 数
//     +8  int32 BlockImageWidth   每个 cell 的画布宽（RA2 恒为 60）
//     +12 int32 BlockImageHeight  每个 cell 的画布高（RA2 恒为 30）
//   偏移表：BlockWidth*BlockHeight 个 u32，相对 FileHeader 起点，0 = 空 cell
//   每个非空 cell 的 52 字节 TileCellHeader：
//     +0  int32 TileX           +4  int32 TileY
//     +8  u32 ExtraDataOffset   +12 u32 ZDataOffset    +16 u32 ExtraZDataOffset
//     +20 int32 ExtraX          +24 int32 ExtraY
//     +28 u32 ExtraWidth        +32 u32 ExtraHeight
//     +36 u8  Bitfield（bit0=HasExtraData, bit1=HasZData, bit2=HasDamagedData）
//     +37 u8[3] padding（0xCD = MSVC 未初始化填充；bit0 为 0 时 Extra* 字段就是垃圾）
//     +40 u8 Height  +41 u8 LandType  +42 u8 SlopeType
//     +43 u8[3] TopLeftRadarColor    +46 u8[3] BottomRightRadarColor
//     +49 u8[3] padding
//   三个偏移都相对该 cell 的 TileCellHeader 起点。cell 内区段顺序：
//     iso 像素（cw*ch/2 字节） -> z 数据（同长） -> extra 像素（ExtraW*ExtraH）
//     -> extra-z（同长）。实测 extra_off - z_off = z_off - 52 = 900，自洽。
//
// 【关键发现 1】多 cell 是"等距倾斜铺排"，不是矩形堆叠。
//   每个 cell 的 TileX/TileY 是它在模板画布里的**像素偏移**，相邻 cell 的偏移是
//   (±cw/2, ch/2) —— 也就是说 bx 方向往右下走、by 方向往左下走，菱形密铺。
//   实测 2x5 模板：Tx/Ty = (30*bx - 30*by, 15*bx + 15*by)。
//   一开始按 (bx*cw, by*ch) 堆叠渲染，和参考实现差了 60% 的像素，图也是糊的。
//
// 【关键发现 2】画布是"非空 cell 的紧包围盒"，不是满格公式。
//   满格模板下二者等价（558/660 精确吻合 (bw+bh-2)*cw/2+cw × ...），
//   但实测有 102 个是"阶梯形"不规则模板（缺角），此时紧包围盒更小。
//   原点要减去 (min Tx, min Ty) 归一化 —— Tx 可以为负。
//
// 【关键发现 3】extra 故意画到格子外面。
//   135 个 1x1 带 extra 的模板里 126 个的 ExtraY 是负数（常见 (0,-15)、(1,-15)、
//   (0,-1)、(30,-1)、(0,-60)），ExtraWH 常见 59x30 / 60x24 / 30x8 ——
//   这是树冠、岩壁、悬崖立面这类"探出到上一格"的图形。
//   所以画布不把 extra 算进包围盒；extra 用同一套坐标原样交给上层合成，
//   由地图渲染器按地块位置去贴，越界部分自然压在上一个地块上。
//
// 实测验收（1x1，文件长度严丝合缝，5 个样本全过）：
//   0x82A81C56  extra(30x16=480)  2352+480 = 2832 = size ✓
//   0x8E387F79  extra(60x10=600)  2472+600 = 3072 = size ✓
//   0x92D36910  extra(60x24=1440) 3312+1440 = 4752 = size ✓
//   0x85BE8273  extra(59x30=1770) 3642+1770 = 5412 = size ✓
//   0x89D457AB  extra(59x29=1711) 3583+1711 = 5294 = size ✓
//
// 端到端：温带地形 0x0F5D1D99 共 1013 条目，解出 660 个 TMP（353 个是 PAL/SHP 之类，
//   57 个模板首 cell 为空），2626 个非空 cell 的 iso 段全部恰好 900 字节。

#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "gfx/Palette.h"

namespace ra2 {

/// TileCellHeader 的 Bitfield 位。
enum TmpCellBits : uint8_t {
    kTmpHasExtraData = 0x01,   ///< 有附加图形（树冠、岩壁、悬崖立面等）
    kTmpHasZData = 0x02,       ///< 有 Z 缓冲（决定遮挡/阴影）
    kTmpHasDamagedData = 0x04, ///< 有损毁态的第二套附加图形
};

/// 52 字节的 cell 头。
///
/// 注意：bit0 为 0 时 ExtraDataOffset / ExtraX / ExtraY / ExtraWidth / ExtraHeight
/// 全是未初始化垃圾（实测是 0xCDCDCDCD 或 3452816845）。用之前必须查 Has_Extra()。
struct TmpCellHeader {
    int32_t tile_x = 0;
    int32_t tile_y = 0;
    uint32_t extra_data_offset = 0;
    uint32_t z_data_offset = 0;
    uint32_t extra_z_data_offset = 0;
    int32_t extra_x = 0;
    int32_t extra_y = 0;
    uint32_t extra_width = 0;
    uint32_t extra_height = 0;
    uint8_t bitfield = 0;
    uint8_t height = 0;      ///< 地块高度（斜坡/悬崖用）
    uint8_t land_type = 0;   ///< 地形类型（对应 rules.ini [TerrainTypes]）
    uint8_t slope_type = 0;  ///< 斜坡朝向
    uint8_t radar_top_left[3] = {};
    uint8_t radar_bottom_right[3] = {};

    bool Has_Extra() const noexcept { return (bitfield & kTmpHasExtraData) != 0; }
    bool Has_Z() const noexcept { return (bitfield & kTmpHasZData) != 0; }
    bool Has_Damaged() const noexcept { return (bitfield & kTmpHasDamagedData) != 0; }
};

/// 一个 cell 解出来的内容。
struct TmpTile {
    int index = -1;        ///< 在偏移表里的下标
    bool present = false;  ///< 偏移为 0 的空 cell
    TmpCellHeader header;

    /// 菱形内的调色板索引，按行紧排（Row_Geometry 顺序），0 = 透明。
    std::vector<uint8_t> iso;
    std::vector<uint8_t> z;        ///< Z 缓冲，与 iso 同长；没有则空
    std::vector<uint8_t> extra;    ///< 附加图形，ExtraWidth*ExtraHeight
    std::vector<uint8_t> extra_z;  ///< 附加图形 Z 缓冲
};

class TmpFile {
public:
    /// 解析整块 TMP 数据。返回 false 表示不是 TMP 或头部不合理。
    bool Load(const uint8_t* data, size_t size);

    int Block_Width() const noexcept { return block_width_; }
    int Block_Height() const noexcept { return block_height_; }
    int Cell_Width() const noexcept { return cell_width_; }
    int Cell_Height() const noexcept { return cell_height_; }
    int Tile_Count() const noexcept { return block_width_ * block_height_; }

    /// 模板画布尺寸 = 非空 cell 的紧包围盒（已按 (min Tx, min Ty) 归一化）。
    /// 1x1 模板恒为 CellWidth × CellHeight（RA2 就是 60x30）。
    int Canvas_Width() const noexcept { return canvas_width_; }
    int Canvas_Height() const noexcept { return canvas_height_; }

    const std::vector<TmpTile>& Tiles() const noexcept { return tiles_; }

    /// 第 i 个 cell 在画布里的落点（已减掉 min Tx/min Ty）。
    std::pair<int, int> Cell_Origin(int i) const;

    /// 第 i 个 cell 的 extra 在画布坐标系里的落点。**可以为负 / 越界** ——
    /// 这是设计如此（树冠探到上一格），调用方要在更大的画布上合成。
    std::pair<int, int> Extra_Origin(int i) const;

    /// 把第 i 个 cell 的菱形展开成 CellWidth×CellHeight 的 RGBA（菱形外 alpha=0）。
    /// with_extra 为真时把 extra 也贴进去（按画布坐标，越出 cell 的部分裁掉）。
    std::vector<uint32_t> Render_Cell_RGBA(int i, const Palette& pal,
                                           bool with_extra = false) const;

    /// 带外扩边距的 cell 渲染 —— 铺地图用这一个。
    ///
    /// 为什么需要：135 个带 extra 的 1x1 模板里有 126 个 ExtraY 是负数
    /// （常见 (0,-15)、(1,-15)、(0,-60)），也就是树冠、岩壁故意画到格子**上方**。
    /// Render_Cell_RGBA 会把越界部分裁掉，铺出来的树就只剩树桩。
    /// 这里四边各留 pad 像素，extra 完整保留；ox/oy 回传 cell 左上角在图里的坐标，
    /// 调用方按 (落点 - ox, 落点 - oy) 贴即可。
    std::vector<uint32_t> Render_Cell_Padded_RGBA(int i, const Palette& pal, int pad,
                                                  int* ox = nullptr, int* oy = nullptr,
                                                  int* w = nullptr, int* h = nullptr) const;

    /// 把整张模板拼成 CanvasWidth×CanvasHeight 的 RGBA。extra 同样会被画布裁掉
    /// 越界部分 —— 要看全图的调用方请用 Extra_Origin() 自己往外合成。
    std::vector<uint32_t> Render_Image_RGBA(const Palette& pal,
                                            bool with_extra = false) const;

    /// 菱形逐行几何：返回 ch 个 (行首x, 该行宽度)。
    /// 60x30 时得到 (29,2)(27,6)...(1,58)(1,58)...(29,2)，宽度合计 900 = 60*30/2。
    static std::vector<std::pair<int, int>> Row_Geometry(int cell_width, int cell_height);

private:
    int block_width_ = 0;
    int block_height_ = 0;
    int cell_width_ = 0;
    int cell_height_ = 0;
    int min_tx_ = 0;   ///< 非空 cell 的 min TileX，画布原点
    int min_ty_ = 0;
    int canvas_width_ = 0;
    int canvas_height_ = 0;
    std::vector<TmpTile> tiles_;
};

}  // namespace ra2
