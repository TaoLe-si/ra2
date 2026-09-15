#include "gfx/RemapTable.h"

#include <cstring>

namespace ra2 {
namespace {

/// 0..255 的 H 映射到 0..360 度，转成 HSV 六扇区里常用的 0..1530 刻度
/// （每扇区 255）。h255 * 6 = 度数的六分之一刻度，除以 255 得整数扇区。
constexpr int kHueScale = 1530;   // 6 * 255

double Lum(const Palette::Color& c) {
    return 0.299 * c.r + 0.587 * c.g + 0.114 * c.b;
}

bool Eq_Fold(const std::string& a, const char* b) {
    if (b == nullptr) return false;
    const size_t n = a.size();
    if (std::strlen(b) != n) return false;
    for (size_t i = 0; i < n; ++i) {
        char ca = a[i];
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
        char cb = b[i];
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
        if (ca != cb) return false;
    }
    return true;
}

}  // namespace

void RemapTable::Hsv_To_Rgb(uint8_t h, uint8_t s, uint8_t v,
                            uint8_t* r, uint8_t* g, uint8_t* b) {
    // H 的 0..255 -> 0..1530（即 0..360 度的六扇区刻度）
    const int hue = (static_cast<int>(h) * kHueScale + 127) / 255;
    const int sector = hue / 255;          // 0..5
    const int frac = hue % 255;            // 扇区内偏移
    // 标准 HSV 六边形：p 最暗、q 过渡、t 中间
    const int p = (static_cast<int>(v) * (255 - s)) / 255;
    const int q = (static_cast<int>(v) * (255 - (s * frac) / 255)) / 255;
    const int t = (static_cast<int>(v) * (255 - (s * (255 - frac)) / 255)) / 255;
    const int vv = v;

    int rr = 0, gg = 0, bb = 0;
    switch (sector % 6) {
        case 0: rr = vv; gg = t;  bb = p;  break;   // 红 -> 黄
        case 1: rr = q;  gg = vv; bb = p;  break;   // 黄 -> 绿
        case 2: rr = p;  gg = vv; bb = t;  break;   // 绿 -> 青
        case 3: rr = p;  gg = q;  bb = vv; break;   // 青 -> 蓝
        case 4: rr = t;  gg = p;  bb = vv; break;   // 蓝 -> 品红
        default: rr = vv; gg = p; bb = q;  break;   // 品红 -> 红
    }
    *r = static_cast<uint8_t>(rr < 0 ? 0 : (rr > 255 ? 255 : rr));
    *g = static_cast<uint8_t>(gg < 0 ? 0 : (gg > 255 ? 255 : gg));
    *b = static_cast<uint8_t>(bb < 0 ? 0 : (bb > 255 ? 255 : bb));
}

int RemapTable::Load_From_Ini(const IniFile& ini) {
    colors_.clear();
    const IniSection* sec = ini.Find_Section("Colors");
    if (sec == nullptr) return 0;

    for (const IniEntry& e : sec->entries) {
        // 值是 "H,S,V"。用逗号切三段，每段取前导整数。
        std::vector<int> tri;
        const char* p = e.value.c_str();
        for (int part = 0; part < 3; ++part) {
            while (*p == ' ' || *p == '\t' || *p == ',') ++p;
            int n = 0;
            bool any = false;
            while (*p >= '0' && *p <= '9') {
                n = n * 10 + (*p - '0');
                if (n > 100000) n = 100000;   // 防溢出
                any = true;
                ++p;
            }
            if (!any) break;
            tri.push_back(n);
            while (*p == ' ' || *p == '\t') ++p;
            if (*p == ',') { ++p; continue; }
            if (*p != '\0') break;            // 第三段后面还有东西 -> 不是纯三元组
        }
        if (tri.size() != 3) continue;
        RemapColor c;
        c.name = e.key;
        c.h = static_cast<uint8_t>(tri[0] > 255 ? 255 : tri[0]);
        c.s = static_cast<uint8_t>(tri[1] > 255 ? 255 : tri[1]);
        c.v = static_cast<uint8_t>(tri[2] > 255 ? 255 : tri[2]);
        colors_.push_back(c);
    }
    return static_cast<int>(colors_.size());
}

int RemapTable::Find_Color(const char* name) const {
    for (size_t i = 0; i < colors_.size(); ++i) {
        if (Eq_Fold(colors_[i].name, name)) return static_cast<int>(i);
    }
    return -1;
}

int RemapTable::Make_Ramp(const uint8_t* base768, bool expanded, int start, int end,
                          int color_index, Palette::Color* out16) const {
    if (out16 == nullptr || base768 == nullptr) return 0;
    if (color_index < 0 || color_index >= static_cast<int>(colors_.size())) return 0;
    if (start < 0 || end > 255 || end < start) return 0;

    Palette base;
    if (expanded) {
        base.Load_Expanded(base768, 768);
    } else {
        base.Load(base768, 768);
    }
    if (!base.Is_Loaded()) return 0;

    // 1) 亮度包络
    const int n = end - start + 1;
    double max_lum = 0.0;
    for (int i = 0; i < n; ++i) {
        const double l = Lum(base.Colors()[start + i]);
        if (l > max_lum) max_lum = l;
    }

    // 2) 目标色
    uint8_t tr = 0, tg = 0, tb = 0;
    Hsv_To_Rgb(colors_[color_index].h, colors_[color_index].s,
               colors_[color_index].v, &tr, &tg, &tb);

    // 3) 按包络缩放
    for (int i = 0; i < n && i < 256; ++i) {
        const double f = (max_lum > 0.0) ? Lum(base.Colors()[start + i]) / max_lum : 0.0;
        out16[i] = Palette::Color{
            static_cast<uint8_t>(tr * f + 0.5),
            static_cast<uint8_t>(tg * f + 0.5),
            static_cast<uint8_t>(tb * f + 0.5), 255};
    }
    return n;
}

bool RemapTable::Make_Palette768(const uint8_t* base768, bool expanded,
                                 int start, int end, int color_index,
                                 uint8_t* out768) const {
    if (base768 == nullptr || out768 == nullptr) return false;

    // 原样拷贝一份（8 位分量）
    if (expanded) {
        std::memcpy(out768, base768, 768);
    } else {
        for (int i = 0; i < 256; ++i) {
            for (int c = 0; c < 3; ++c) {
                const uint8_t v6 = base768[i * 3 + c];
                out768[i * 3 + c] = static_cast<uint8_t>((v6 << 2) | (v6 >> 4));
            }
        }
    }

    if (start < 0 || end > 255 || end < start) return false;
    if (color_index < 0 || color_index >= static_cast<int>(colors_.size())) return false;

    const int n = end - start + 1;
    Palette::Color ramp[256];
    if (Make_Ramp(base768, expanded, start, end, color_index, ramp) != n) {
        return false;
    }
    for (int i = 0; i < n; ++i) {
        out768[(start + i) * 3 + 0] = ramp[i].r;
        out768[(start + i) * 3 + 1] = ramp[i].g;
        out768[(start + i) * 3 + 2] = ramp[i].b;
    }
    return true;
}

Palette RemapTable::Make_Palette(const uint8_t* base768, bool expanded,
                                 int start, int end, int color_index) const {
    Palette out;
    if (base768 == nullptr) return out;
    if (expanded) {
        out.Load_Expanded(base768, 768);
    } else {
        out.Load(base768, 768);
    }
    if (!out.Is_Loaded()) return out;

    if (start < 0 || end > 255 || end < start) return out;
    if (color_index < 0 || color_index >= static_cast<int>(colors_.size())) return out;

    const int n = end - start + 1;
    std::vector<Palette::Color> ramp(static_cast<size_t>(n));
    if (Make_Ramp(base768, expanded, start, end, color_index, ramp.data()) != n) {
        return out;
    }
    for (int i = 0; i < n; ++i) {
        out.Set_Color(start + i, ramp[i].r, ramp[i].g, ramp[i].b);
    }
    return out;
}

}  // namespace ra2
