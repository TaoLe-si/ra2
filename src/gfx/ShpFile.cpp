// ShpFile.cpp

#include "gfx/ShpFile.h"

#include <cmath>

namespace ra2 {
namespace {

inline uint16_t RdU16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
inline uint32_t RdU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace

std::vector<uint8_t> ShpFile::Decode_Rle_Zero(const uint8_t* src, size_t len,
                                              int w, int h) {
    // Westwood RLE-Zero（TS 变体）：
    //   每行 u16 行长（含这 2 字节）；行内 0x00 后跟"重复几个透明像素"。
    std::vector<uint8_t> out;
    out.reserve(static_cast<size_t>(w) * h);
    size_t p = 0;
    for (int line = 0; line < h && p + 2 <= len; ++line) {
        const size_t line_len = RdU16(src + p);
        p += 2;
        const size_t end = (p + line_len - 2 < len) ? (p + line_len - 2) : len;
        if (line_len < 2) {
            continue;
        }
        while (p < end) {
            const uint8_t v = src[p++];
            if (v == 0) {
                if (p >= end) {
                    break;
                }
                const uint8_t count = src[p++];
                out.insert(out.end(), count, 0);
            } else {
                out.push_back(v);
            }
        }
    }
    // 不足一帧补透明，调用方就不必处理半帧。
    const size_t want = static_cast<size_t>(w) * h;
    if (out.size() < want) {
        out.resize(want, 0);
    } else if (out.size() > want) {
        out.resize(want);
    }
    return out;
}

bool ShpFile::Load(const uint8_t* data, size_t size) {
    frames_.clear();
    pixels_.clear();
    width_ = height_ = 0;
    if (data == nullptr || size < 8) {
        return false;
    }
    if (RdU16(data) != 0) {
        return false;   // SHP(TS) 的头两字节必须是 0
    }
    width_ = RdU16(data + 2);
    height_ = RdU16(data + 4);
    const uint16_t count = RdU16(data + 6);
    if (count == 0 || 8 + static_cast<size_t>(count) * 24u > size) {
        return false;
    }

    frames_.resize(count);
    for (uint16_t i = 0; i < count; ++i) {
        const uint8_t* e = data + 8 + static_cast<size_t>(i) * 24u;
        ShpFrameInfo& f = frames_[i];
        f.x = RdU16(e);
        f.y = RdU16(e + 2);
        f.w = RdU16(e + 4);
        f.h = RdU16(e + 6);
        f.flags = RdU32(e + 8);
        f.color[0] = e[12];
        f.color[1] = e[13];
        f.color[2] = e[14];
        f.color[3] = e[15];
        f.data_offset = RdU32(e + 20);
    }

    pixels_.resize(count);
    for (uint16_t i = 0; i < count; ++i) {
        const ShpFrameInfo& f = frames_[i];
        // 帧数据结束位置：下一帧的 offset，最后一帧到文件尾。
        uint32_t end = static_cast<uint32_t>(size);
        if (i + 1 < count && frames_[i + 1].data_offset > f.data_offset) {
            end = frames_[i + 1].data_offset;
        }
        if (f.data_offset >= size) {
            pixels_[i].assign(static_cast<size_t>(f.w) * f.h, 0);
            continue;
        }
        if (end > size) {
            end = static_cast<uint32_t>(size);
        }
        const size_t len = end - f.data_offset;
        if (f.flags & kShpUsesRle) {
            pixels_[i] = Decode_Rle_Zero(data + f.data_offset, len, f.w, f.h);
        } else {
            pixels_[i].assign(data + f.data_offset,
                              data + f.data_offset + (len < static_cast<size_t>(f.w) * f.h
                                                          ? len
                                                          : static_cast<size_t>(f.w) * f.h));
            pixels_[i].resize(static_cast<size_t>(f.w) * f.h, 0);
        }
    }
    return true;
}

int ShpFile::Pick_Palette(const ShpFrameInfo& f,
                          const std::vector<uint8_t>& pixels,
                          const std::vector<Palette>& candidates,
                          double* out_error) {
    // 判据来自格式本身：FrameColor 是该帧在**正确**调色板下非透明像素的平均色。
    // 遍历候选算平均色取最接近的，比"猜哪个 PAL 配哪个 SHP"可靠得多 ——
    // 实测在 ra2md.mix 的 72 个候选里，AutoLoginQuery.shp 命中了同名的
    // AutoLoginQuery.PAL，色差 0.6，而第二名是 1.7。
    int best = -1;
    double best_err = 1e30;
    for (size_t c = 0; c < candidates.size(); ++c) {
        double acc[3] = {0, 0, 0};
        long long n = 0;
        for (uint8_t px : pixels) {
            if (px == 0) {
                continue;           // 透明不计入
            }
            const Palette::Color col = candidates[c].Map(px);
            acc[0] += col.r;
            acc[1] += col.g;
            acc[2] += col.b;
            ++n;
        }
        if (n == 0) {
            continue;
        }
        const double err =
            std::sqrt((acc[0] / n - f.color[0]) * (acc[0] / n - f.color[0]) +
                      (acc[1] / n - f.color[1]) * (acc[1] / n - f.color[1]) +
                      (acc[2] / n - f.color[2]) * (acc[2] / n - f.color[2]));
        if (err < best_err) {
            best_err = err;
            best = static_cast<int>(c);
        }
    }
    if (out_error) {
        *out_error = best_err;
    }
    return best;
}

}  // namespace ra2
