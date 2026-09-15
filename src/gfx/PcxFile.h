// PcxFile.h -- RA2 PCX（加载画面、UI 面板、按钮条）
//
// RA2 里的 PCX 就是**标准 ZSoft PCX**，Westwood 没改过 —— 161 个样本里
// 三条硬判据全过（见 tools/pcxdec.py）：
//   1. RLE 解出的字节数恰好 = 每行字节数 × 行数 × 平面数，不多不少；
//   2. 8 位模式下文件尾必须是 0x0C 魔数 + 768 字节调色板；
//   3. 128(头) + 数据区 + 调色板 = 文件长度，且数据区"刚好吃完"。
// 第 3 条尤其强：640×480×3 的图要 RLE 精确吃掉 66 万字节后正好停在调色板
// 之前的那个字节上，格式理解只要有一处偏差就不可能成立。
//
// 布局：
//   +0   u8  制造商 0x0A
//   +1   u8  版本 5
//   +2   u8  编码 1 = RLE
//   +3   u8  每平面位数（RA2 全是 8）
//   +4   u16 Xmin   +6 u16 Ymin   +8 u16 Xmax   +10 u16 Ymax
//   +12  u16 HDpi   +14 u16 VDpi
//   +16  48 字节 16 色 EGA 调色板（RA2 用不到，但字段要跳过去）
//   +64  u8  保留（0）
//   +65  u8  平面数  1 = 8 位带调色板，3 = 24 位 RGB
//   +66  u16 每行字节数（= 宽度，PCX 规定按偶对齐，RA2 里恒等于宽度）
//   +68  u16 PaletteInfo
//   +70  u16 HScreenSize / +72 u16 VScreenSize
//   +74  54 字节填充
//
// RLE：读一字节 b。若 (b & 0xC0) == 0xC0 则是游程头，长度 = b & 0x3F，
//   紧接一字节是要重复的值；否则 b 本身就是一个像素值。
//   注意每行**独立重置**计数，跨行不断游程。
//
// 平面排布（多平面时）是"按行交织"：一行内先存完 R 的 bpl 字节，再 G，再 B，
// 然后才是下一行。不是"整幅图 R 段 + 整幅图 G 段"。
//
// 【别踩的坑】8 位 PCX 的内嵌调色板是**标准 8 位**（实测 136 个样本最大分量 = 255），
//   不是 .PAL 那种 0..63 的 6 位值。所以这里**不能**套 Palette::Load 的 (v<<2)|(v>>4)
//   展开，否则整幅图会暗一大截、还会把 255 压成 252 造成渐变断层。
//
// 分布（ra2.mix 161 个 + ra2md.mix 84 个 + expandmd01.mix 10 个，全部解通）：
//   * 14×14 / 28×28 / 60×27 这类小块 —— 侧边栏按钮、图标，8 位带调色板；
//   * 640×480 / 800×600 / 856×736 平面数 3 —— 任务简报、加载画面，24 位真彩；
//   * 640×480 平面数 1 —— 少数 8 位全屏图。
// 注意 MIX 里没有文件名，只能靠 0A 05 魔数认；ra2.mix 里有一个 0x08050506 的
// 101MB 条目首字节恰好也是 0A 05，但编码字节是 69 而不是 1，会被 Load 挡掉。

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "gfx/Palette.h"

namespace ra2 {

class PcxFile {
public:
    /// 解析一整块 PCX 数据。返回 false 表示不是 PCX 或头部/数据不自洽。
    bool Load(const uint8_t* data, size_t size);

    int Width() const noexcept { return width_; }
    int Height() const noexcept { return height_; }
    bool Is_Indexed() const noexcept { return planes_ == 1; }
    int Planes() const noexcept { return planes_; }
    int Bytes_Per_Line() const noexcept { return bytes_per_line_; }

    /// 调色板索引，宽×高，行间无 padding（已按 Xmin/Xmax 裁掉行尾补齐字节）。
    /// 仅 8 位模式有效。
    const std::vector<uint8_t>& Indices() const noexcept { return indices_; }

    /// 内嵌调色板，768 字节 RGB，8 位分量（**不做 6→8 位展开**）。
    /// 仅 8 位模式有效。
    const uint8_t* Embedded_Palette() const noexcept { return palette_.data(); }
    bool Has_Embedded_Palette() const noexcept { return planes_ == 1; }

    /// 展开成 Width×Height 的 RGBA8（R,G,B,A 逐字节，不是 0xAABBGGRR）。
    /// pal 传非空时用外部调色板（做调色板替换时用），否则用内嵌的那张。
    /// 内嵌调色板是 8 位值，直接取用；外部 Palette 是 6 位值，由 Palette::Map 展开。
    std::vector<uint8_t> To_RGBA(const Palette* pal = nullptr) const;

    void Reset();

private:
    int width_ = 0;
    int height_ = 0;
    int planes_ = 0;
    int bytes_per_line_ = 0;

    std::vector<uint8_t> indices_;   ///< 8 位模式：width*height 的调色板索引
    std::vector<uint8_t> pixels_;    ///< 原始解出的平面数据（bpl*h*planes）
    std::vector<uint8_t> palette_;   ///< 8 位模式的 768 字节内嵌调色板
};

}  // namespace ra2
