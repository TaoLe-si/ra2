// RemapTable.h -- 阵营色（remap）表
//
// 【原版机制】RA2 的单位素材不做多份。步兵 SHP 和载具 VXL 里，
// 需要显示成阵营色的那部分像素统一用调色板索引 **16..31** 这 16 个
// "占位色"，游戏启动时按所属阵营把这一段整段换掉，一份素材渲染出
// 红蓝黄紫 8 个阵营。
//
// 【实测证据】
//   * unittem.pal / uniturb.pal（按名 CRC 命中）的 0x10..0x1F 都是同一组
//     纯红渐变：fc0000 ec0000 dc0000 d00000 c00000 b00000 a40000 940000
//     840000 780000 680000 580000 4c0000 3c0000 2c0000 200000（6 bit），
//     即"占位色是红色"，靠运行时替换成别的阵营色。
//   * GI.SHP（186032 字节，78x66x744 帧）索引直方图里 0x10..0x1F **全段在用**，
//     共 14241 像素，占非透明像素约 12.5% —— 正是衣服/徽章那块阵营色。
//   * VXL 头 +32/+33 自己声明了区间（RemapStart=16 / RemapEnd=31，实测恒等），
//     VxlFile 已经把它解析出来了。所以区间是**素材自报**，不写死。
//
// 【颜色从哪来】rules.ini 的 [Colors] 段（ra2.mix 里 id=0xF025A96C，541915 字节），
// 19 个颜色，值是 **HSV 而不是 RGB**：
//     Gold=41,240,230        ; Yellow
//     Magenta=221,102,255    ; Pink
//     LightGrey=0,0,240      ; White
// 三个分量都是 0..255，其中 H 的 0..255 **映射 0..360 度** —— 判据是
// Magenta：221/255*360 = 311 度是品红，对得上注释里的 Pink；
// 若把 221 当度数就变成蓝色，对不上。LightBlue=119 -> 168 度 = 青（Aqua），同理。
//
// 【16 级怎么生成】拿原调色板 remap 区那 16 个占位色的**亮度包络**当缩放系数，
// 乘到目标色上：
//     f[i] = lum(base[start+i]) / max_lum(base[start..end])
//     new[i] = target_rgb * f[i]
// 占位色是纯红，缩放 RGB 等价于"保持色相和饱和度、只按 f 缩放明度"，
// 与原版一致。用包络而不是写死数值，换任何一张调色板都成立。

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "data/Ini.h"
#include "gfx/Palette.h"

namespace ra2 {

/// [Colors] 里的一条。存原始 HSV，不急着转 RGB。
struct RemapColor {
    std::string name;       ///< 键名，如 "DarkRed"
    uint8_t h = 0, s = 0, v = 0;
};

class RemapTable {
public:
    /// 从已解析的 INI 读 [Colors]。
    ///
    /// 只收"三个 0..255 整数"的条目，跳过 AlliedLoad 之类同段里的其它键值
    /// （它们格式一样其实也收得下，但语义上不是玩家色，靠名字白名单区分太脆，
    /// 所以这里按**段内顺序 + 三元组格式**全收，调用方按名字取）。
    /// 返回读到的条数；0 表示这个 INI 里没有 [Colors]。
    int Load_From_Ini(const IniFile& ini);

    int Color_Count() const noexcept { return static_cast<int>(colors_.size()); }
    const RemapColor& Color(int i) const { return colors_[i]; }
    /// 按名取（大小写不敏感）；找不到返回 -1。
    int Find_Color(const char* name) const;

    /// HSV -> RGB，分量 0..255，H 的 0..255 映射 0..360 度。
    static void Hsv_To_Rgb(uint8_t h, uint8_t s, uint8_t v,
                           uint8_t* r, uint8_t* g, uint8_t* b);

    /// 生成 remap 后的调色板。
    ///
    /// base768  原调色板 768 字节
    /// expanded true = 已经是 8 位分量（VXL 内嵌调色板），
    ///          false = 6 位分量（.PAL 文件，需要 6->8 展开）
    /// start/end remap 区间（含两端）；越界或 end<start 时原样返回
    /// color_index [Colors] 下标
    Palette Make_Palette(const uint8_t* base768, bool expanded,
                         int start, int end, int color_index) const;

    /// 同上，但直接吐 768 字节（8 位分量）。
    ///
    /// 给那些本来就吃 `const uint8_t* pal768` 的下游用（比如
    /// VoxelLight::Shade_To_RGBA），省掉 Palette -> 768 再转回来的一步。
    /// 非 remap 区间原样拷贝；参数非法时把 base 原样拷出并返回 false。
    bool Make_Palette768(const uint8_t* base768, bool expanded,
                         int start, int end, int color_index,
                         uint8_t* out768) const;

    /// 单独取出某个阵营色的 16 级色阶（调试/自检用）。
    /// 返回实际写入的级数。
    int Make_Ramp(const uint8_t* base768, bool expanded, int start, int end,
                  int color_index, Palette::Color* out16) const;

private:
    std::vector<RemapColor> colors_;
};

}  // namespace ra2
