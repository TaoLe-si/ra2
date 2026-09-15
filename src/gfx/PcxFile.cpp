#include "gfx/PcxFile.h"

#include <cstring>

namespace ra2 {
namespace {

constexpr size_t kHeaderSize = 128;
constexpr uint8_t kManufacturer = 0x0A;
constexpr uint8_t kPaletteMagic = 0x0C;
constexpr size_t kPaletteSize = 768;

uint16_t RdU16(const uint8_t* p) noexcept {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

}  // namespace

void PcxFile::Reset() {
    width_ = height_ = planes_ = bytes_per_line_ = 0;
    indices_.clear();
    pixels_.clear();
    palette_.clear();
}

bool PcxFile::Load(const uint8_t* data, size_t size) {
    Reset();
    if (data == nullptr || size < kHeaderSize) return false;
    if (data[0] != kManufacturer) return false;

    const uint8_t encoding = data[2];
    const int bpp = data[3];
    const int xmin = RdU16(data + 4);
    const int ymin = RdU16(data + 6);
    const int xmax = RdU16(data + 8);
    const int ymax = RdU16(data + 10);
    const int planes = data[65];
    const int bpl = RdU16(data + 66);

    // 编码 1 = RLE。RA2 全是 RLE；那个 101MB 的假 PCX 编码字节是 69，挡在这里。
    if (encoding != 1) return false;
    if (bpp != 8 || (planes != 1 && planes != 3)) return false;

    const int w = xmax - xmin + 1;
    const int h = ymax - ymin + 1;
    if (w <= 0 || h <= 0 || bpl < w || bpl > 4096) return false;
    // 8000×8000 已经很离谱了，再加一道，防止畸形头把内存算爆。
    if (static_cast<int64_t>(w) * h > 64ll * 1024 * 1024) return false;

    // 8 位模式：文件尾必然是可选的 0x0C + 768 字节调色板，数据区到它之前为止。
    size_t data_end = size;
    if (planes == 1) {
        if (size < kHeaderSize + 1 + kPaletteSize) return false;
        const size_t pal_start = size - (1 + kPaletteSize);
        if (data[pal_start] != kPaletteMagic) return false;
        palette_.assign(data + pal_start + 1, data + size);
        data_end = pal_start;
    }

    // ---- RLE ----
    const size_t want = static_cast<size_t>(bpl) * h * planes;
    pixels_.reserve(want);
    size_t i = kHeaderSize;
    while (i < data_end && pixels_.size() < want) {
        const uint8_t b = data[i++];
        if ((b & 0xC0) == 0xC0) {
            const size_t run = b & 0x3F;
            if (i >= data_end) return false;   // 游程头后面没值了
            pixels_.insert(pixels_.end(), run, data[i]);
            ++i;
        } else {
            pixels_.push_back(b);
        }
    }
    // 三条硬判据之二三：字节数恰好、数据区刚好吃完。
    if (pixels_.size() != want) return false;
    if (i != data_end) return false;

    width_ = w;
    height_ = h;
    planes_ = planes;
    bytes_per_line_ = bpl;

    if (planes == 1) {
        // 行尾可能有对齐补齐字节，按 Xmin/Xmax 裁掉。
        indices_.resize(static_cast<size_t>(w) * h);
        for (int y = 0; y < h; ++y) {
            std::memcpy(&indices_[static_cast<size_t>(y) * w],
                        &pixels_[static_cast<size_t>(y) * bpl],
                        static_cast<size_t>(w));
        }
    }
    return true;
}

std::vector<uint8_t> PcxFile::To_RGBA(const Palette* pal) const {
    std::vector<uint8_t> out(static_cast<size_t>(width_) * height_ * 4, 0);
    if (width_ <= 0 || height_ <= 0) return out;

    if (planes_ == 1) {
        const size_t n = static_cast<size_t>(width_) * height_;
        const bool use_own = (pal == nullptr) && palette_.size() >= kPaletteSize;
        for (size_t k = 0; k < n; ++k) {
            const uint8_t idx = indices_[k];
            uint8_t r, g, b;
            if (use_own) {
                // 内嵌调色板是 8 位值，直接取，不做 6→8 位展开。
                r = palette_[idx * 3 + 0];
                g = palette_[idx * 3 + 1];
                b = palette_[idx * 3 + 2];
            } else if (pal != nullptr) {
                const Palette::Color c = pal->Map(idx);
                r = c.r; g = c.g; b = c.b;
            } else {
                r = g = b = idx;   // 既没内嵌也没外部：退化成灰度，至少不是全黑
            }
            out[k * 4 + 0] = r;
            out[k * 4 + 1] = g;
            out[k * 4 + 2] = b;
            out[k * 4 + 3] = 255;
        }
        return out;
    }

    // 24 位：R/G/B 三条带按行交织。
    for (int y = 0; y < height_; ++y) {
        const size_t band = static_cast<size_t>(y) * bytes_per_line_ * 3;
        for (int x = 0; x < width_; ++x) {
            const size_t dst = (static_cast<size_t>(y) * width_ + x) * 4;
            out[dst + 0] = pixels_[band + x];
            out[dst + 1] = pixels_[band + bytes_per_line_ + x];
            out[dst + 2] = pixels_[band + 2 * bytes_per_line_ + x];
            out[dst + 3] = 255;
        }
    }
    return out;
}

}  // namespace ra2
