// Palette.h -- RA2 调色板（.PAL）
//
// 768 字节 = 256 色 × 3 字节 RGB。分量取值 0..63（6 bit），需要扩到 8 bit。
// 实测 FSSLG.PAL 首色 = 3f 00 3f（品红），是常见的透明/关键色，印证 6 bit 值域。
//
// 6→8 bit 用 (v<<2)|(v>>4) 而不是 v*4：后者最大只到 252，会整体偏暗。

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ra2 {

class Palette {
public:
    struct Color {
        uint8_t r = 0, g = 0, b = 0, a = 255;
    };

    /// 从 768 字节加载 .PAL（分量 0..63，6 bit，内部展开成 8 bit）。
    /// 输入不足 768 会补零而不是失败（素材可能被截断）。
    bool Load(const uint8_t* data, size_t size);

    /// 从 768 字节加载**已经是 8 bit** 的调色板，不做 6→8 展开。
    ///
    /// 给 VXL 用：VXL 内嵌调色板实测 768 个分量全部 ≡ 3 (mod 4)，
    /// 即存的已经是 `(v6<<2)|(v6>>4)` 展开后的值，再展开一次就会
    /// 把炮塔渲成一片青紫洋红（踩过）。
    bool Load_Expanded(const uint8_t* data, size_t size);

    const Color* Colors() const noexcept { return colors_; }
    Color Map(uint8_t index) const noexcept { return colors_[index]; }

    /// 直接改写某一色。给 remap 用：阵营色表只替换调色板中间那 16 个索引，
    /// 其余要原样保留，所以必须有"改单个"而不是"整块重载"。
    void Set_Color(int index, uint8_t r, uint8_t g, uint8_t b) {
        if (index < 0 || index > 255) return;
        colors_[index] = Color{r, g, b, 255};
    }
    void Set_Loaded(bool v) noexcept { loaded_ = v; }

    /// 打包成 RGBA8 数组，方便直接上传成 256x1 纹理。
    void To_RGBA8(uint8_t* out256x4) const;

    /// 打包成 768 字节 RGB（已经是 8 位分量，不再做 6->8 展开）。
    /// 给 remap 用：改完再喂回 Load_Expanded 就能拿到 Palette。
    void To_RGB8(uint8_t* out768) const;

    bool Is_Loaded() const noexcept { return loaded_; }

private:
    Color colors_[256];
    bool loaded_ = false;
};

}  // namespace ra2
