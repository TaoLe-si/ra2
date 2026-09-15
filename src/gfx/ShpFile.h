// ShpFile.h -- SHP(TS) 精灵（RA2 / Yuri's Revenge 用的那种）
//
// 格式（moddingwiki 权威 + 本项目实测）：
//   文件头 8 字节：  u16 0 | u16 宽 | u16 高 | u16 帧数
//   帧表 每帧 24 字节：
//     +0  u16 X      +2  u16 Y       +4  u16 宽   +6  u16 高
//     +8  u32 flags  （bit0=HasTransparency, bit1=UsesRle）
//     +12 u8[4] FrameColor（该帧非透明像素的平均色，小地图用）
//     +16 u32 Reserved（0）
//     +20 u32 DataOffset
//
// 帧数据（flags bit1 = UsesRle 决定走哪条）：
//   UsesRle  -> Westwood RLE-Zero：每行开头 u16 = 该行输入字节数（含这 2 字节），
//               行内遇到 0x00 则紧随的一字节是"重复多少个透明像素"。
//               **行尾游程计数常比实际多 1，必须逐行裁剪到行宽**，见 .cpp 注释。
//   否则     -> 未压缩，宽*高 字节。
//
// 注意 bit0（HasTransparency）和 bit1（UsesRle）共用同一个解码器：
// 0x0/0x1 未压缩、0x2/0x3 RLE-Zero。这两个位只是告诉 blitter 怎么画，
// 不是两套压缩算法。实测 6373 帧：未压缩 2289 帧、RLE 4084 帧，各 100% 命中。
//
// 已验证：
//   - AutoLoginQuery.shp（632x568, flags=0x2）精确解出 358976 像素，并用帧表里的
//     FrameColor 自动匹配调色板，命中了同名的 AutoLoginQuery.PAL（色差 0.6）。
//   - COMPASS.SHP / FULLFNT3.SHP（flags=0x3，占了全部帧的 62%）逐行裁剪后
//     3964/3964 帧精确解出，字形肉眼可辨。

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "gfx/Palette.h"

namespace ra2 {

enum ShpFlags : uint32_t {
    kShpHasTransparency = 0x01,
    kShpUsesRle = 0x02,
};

struct ShpFrameInfo {
    uint16_t x = 0, y = 0, w = 0, h = 0;
    uint32_t flags = 0;
    uint8_t color[4] = {};     ///< 前 3 字节是 RGB 平均色，8 bit 分量
    uint32_t data_offset = 0;
};

class ShpFile {
public:
    bool Load(const uint8_t* data, size_t size);

    int Width() const noexcept { return width_; }
    int Height() const noexcept { return height_; }
    int Frame_Count() const noexcept { return static_cast<int>(frames_.size()); }
    const ShpFrameInfo& Frame_Info(int i) const { return frames_[i]; }

    /// 第 i 帧的调色板索引像素（w*h 字节，索引 0 = 透明）。
    const std::vector<uint8_t>& Frame_Pixels(int i) const { return pixels_[i]; }

    /// 按帧表里的 FrameColor 从一组候选调色板里挑最贴近的。
    /// 返回下标，没有候选返回 -1。判据见 .cpp 注释。
    static int Pick_Palette(const ShpFrameInfo& f,
                            const std::vector<uint8_t>& pixels,
                            const std::vector<Palette>& candidates,
                            double* out_error = nullptr);

private:
    static std::vector<uint8_t> Decode_Rle_Zero(const uint8_t* src, size_t len,
                                                int w, int h);

    int width_ = 0;
    int height_ = 0;
    std::vector<ShpFrameInfo> frames_;
    std::vector<std::vector<uint8_t>> pixels_;
};

}  // namespace ra2
