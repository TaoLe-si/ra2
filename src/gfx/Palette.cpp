// Palette.cpp

#include "gfx/Palette.h"

#include <cstring>

namespace ra2 {
namespace {

inline uint8_t Expand6(uint8_t v) {
    return static_cast<uint8_t>((v << 2) | (v >> 4));
}

}  // namespace

bool Palette::Load(const uint8_t* data, size_t size) {
    if (data == nullptr) {
        loaded_ = false;
        return false;
    }
    const size_t n = size < 768 ? size : 768;
    for (size_t i = 0; i < 256; ++i) {
        const size_t o = i * 3;
        if (o + 2 < n) {
            colors_[i].r = Expand6(data[o]);
            colors_[i].g = Expand6(data[o + 1]);
            colors_[i].b = Expand6(data[o + 2]);
            colors_[i].a = 255;
        } else {
            colors_[i] = Color{};
        }
    }
    // 索引 0 是透明色：RA2 的 blitter 一律跳过它，这里把 alpha 清零，
    // 这样渲染端不用再判断索引。
    colors_[0].a = 0;
    loaded_ = (n >= 768);
    return loaded_;
}

void Palette::To_RGBA8(uint8_t* out) const {
    for (int i = 0; i < 256; ++i) {
        out[i * 4 + 0] = colors_[i].r;
        out[i * 4 + 1] = colors_[i].g;
        out[i * 4 + 2] = colors_[i].b;
        out[i * 4 + 3] = colors_[i].a;
    }
}

}  // namespace ra2
