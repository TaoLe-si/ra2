// TmpFile.cpp
//
// 与 tools/tmpdump.py、tools/tmppara.py 对账过（逐像素比对工具 tools/tmpcmp.py）。
// 三个"不显然"的点写在头文件里了，这里只强调实现上的取舍：
//   * 区段长度靠"下一个 cell 的起点 / 文件尾"反推，不靠猜，也不信任 extra 字段
//     （bit0=0 时那些字段是 0xCDCDCDCD 垃圾）。
//   * 画布是紧包围盒，extra 不参与 —— extra 是"挂在格子外"的图形。

#include "gfx/TmpFile.h"

#include <algorithm>
#include <cstring>

namespace ra2 {

namespace {
constexpr size_t kTmpFileHeader = 16;
constexpr size_t kTmpCellHeader = 52;

inline int32_t RdI32(const uint8_t* p) {
    const uint32_t v = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                       (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
    return static_cast<int32_t>(v);
}
inline uint32_t RdU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
inline uint32_t Pack_RGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8) |
           (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(a) << 24);
}
}  // namespace

std::vector<std::pair<int, int>> TmpFile::Row_Geometry(int cw, int ch) {
    // 菱形顶点在 (cw/2, 0) (cw, ch/2) (cw/2, ch) (0, ch/2)，
    // 边缘斜率 = (cw/2)/(ch/2) = cw/ch。往下走一行左右各外扩 cw/ch 个像素：
    //     w(y) = 2 + 2*step*min(y, ch-1-y)
    //     x(y) = (cw/2 - 1) - step*min(y, ch-1-y)
    // RA2 (60x30, step=2)：2,6,...,58,58,...,2，合计 900 = 60*30/2 ✓
    std::vector<std::pair<int, int>> rows;
    if (cw <= 0 || ch <= 0) {
        return rows;
    }
    const int step = std::max(1, cw / std::max(1, ch));
    rows.reserve(static_cast<size_t>(ch));
    for (int y = 0; y < ch; ++y) {
        const int d = std::min(y, ch - 1 - y);
        rows.emplace_back((cw / 2 - 1) - step * d, 2 + 2 * step * d);
    }
    return rows;
}

bool TmpFile::Load(const uint8_t* data, size_t size) {
    block_width_ = block_height_ = cell_width_ = cell_height_ = 0;
    min_tx_ = min_ty_ = canvas_width_ = canvas_height_ = 0;
    tiles_.clear();
    if (data == nullptr || size < kTmpFileHeader) {
        return false;
    }

    const int bw = RdI32(data + 0);
    const int bh = RdI32(data + 4);
    const int cw = RdI32(data + 8);
    const int ch = RdI32(data + 12);
    // 合理性闸门：真实素材 1x1 居多，桥/悬崖模板到 7x5 左右；画布恒 60x30。
    // 放宽到 10x10 / 8..256 是为了兼容 TS 老素材，同时足以挡住
    // "随便一个文件被当成 TMP"（实测温带归档里 353/1013 是 PAL/SHP 之类）。
    if (bw < 1 || bw > 10 || bh < 1 || bh > 10 || cw < 8 || cw > 256 || ch < 8 || ch > 256) {
        return false;
    }
    const size_t n = static_cast<size_t>(bw) * static_cast<size_t>(bh);
    if (kTmpFileHeader + n * 4 > size) {
        return false;
    }

    std::vector<uint32_t> offs(n);
    for (size_t i = 0; i < n; ++i) {
        offs[i] = RdU32(data + kTmpFileHeader + i * 4);
    }
    // 非空 cell 的起点升序排好，用来给每个 cell 定界（区段长度靠边界反推）。
    std::vector<uint32_t> starts;
    for (uint32_t o : offs) {
        if (o != 0 && o + kTmpCellHeader <= size) {
            starts.push_back(o);
        }
    }
    std::sort(starts.begin(), starts.end());
    if (starts.empty()) {
        return false;   // 一个 cell 都没有，不算有效模板
    }

    block_width_ = bw;
    block_height_ = bh;
    cell_width_ = cw;
    cell_height_ = ch;

    const size_t iso_len = static_cast<size_t>(cw) * static_cast<size_t>(ch) / 2;

    tiles_.resize(n);
    for (size_t i = 0; i < n; ++i) {
        TmpTile& t = tiles_[i];
        t.index = static_cast<int>(i);
        const uint32_t base = offs[i];
        if (base == 0 || base + kTmpCellHeader > size) {
            continue;   // 空 cell
        }
        const uint8_t* h = data + base;
        TmpCellHeader& hd = t.header;
        hd.tile_x = RdI32(h + 0);
        hd.tile_y = RdI32(h + 4);
        hd.extra_data_offset = RdU32(h + 8);
        hd.z_data_offset = RdU32(h + 12);
        hd.extra_z_data_offset = RdU32(h + 16);
        hd.extra_x = RdI32(h + 20);
        hd.extra_y = RdI32(h + 24);
        hd.extra_width = RdU32(h + 28);
        hd.extra_height = RdU32(h + 32);
        hd.bitfield = h[36];
        hd.height = h[40];
        hd.land_type = h[41];
        hd.slope_type = h[42];
        std::memcpy(hd.radar_top_left, h + 43, 3);
        std::memcpy(hd.radar_bottom_right, h + 46, 3);

        auto it = std::upper_bound(starts.begin(), starts.end(), base);
        const size_t cell_end = (it == starts.end()) ? size : static_cast<size_t>(*it);

        auto slice = [&](uint32_t rel, size_t want) -> std::vector<uint8_t> {
            const size_t begin = static_cast<size_t>(base) + rel;
            if (rel == 0 || begin >= cell_end) {
                return {};
            }
            const size_t len = std::min(want, cell_end - begin);
            return std::vector<uint8_t>(data + begin, data + begin + len);
        };

        t.iso = slice(kTmpCellHeader, iso_len);
        if (hd.Has_Z()) {
            t.z = slice(hd.z_data_offset, iso_len);
        }
        // extra 的尺寸字段在 bit0 为 0 时是垃圾，必须先查位再取。
        const size_t extra_len = static_cast<size_t>(hd.extra_width) *
                                 static_cast<size_t>(hd.extra_height);
        if (hd.Has_Extra() && extra_len > 0 && extra_len < (16u << 20)) {
            t.extra = slice(hd.extra_data_offset, extra_len);
            if (hd.Has_Z()) {
                t.extra_z = slice(hd.extra_z_data_offset, extra_len);
            }
        }
        t.present = true;
    }

    // 画布 = 非空 cell 的紧包围盒，原点归一化到 (0,0)。
    bool first = true;
    int maxx = 0, maxy = 0;
    for (const TmpTile& t : tiles_) {
        if (!t.present) {
            continue;
        }
        const int tx = t.header.tile_x;
        const int ty = t.header.tile_y;
        if (first) {
            min_tx_ = tx;
            min_ty_ = ty;
            maxx = tx + cw;
            maxy = ty + ch;
            first = false;
            continue;
        }
        min_tx_ = std::min(min_tx_, tx);
        min_ty_ = std::min(min_ty_, ty);
        maxx = std::max(maxx, tx + cw);
        maxy = std::max(maxy, ty + ch);
    }
    canvas_width_ = maxx - min_tx_;
    canvas_height_ = maxy - min_ty_;
    return true;
}

std::pair<int, int> TmpFile::Cell_Origin(int i) const {
    const int idx = std::min(std::max(i, 0), static_cast<int>(tiles_.size()) - 1);
    if (idx < 0) {
        return {0, 0};
    }
    const TmpCellHeader& h = tiles_[static_cast<size_t>(idx)].header;
    return {h.tile_x - min_tx_, h.tile_y - min_ty_};
}

std::pair<int, int> TmpFile::Extra_Origin(int i) const {
    const int idx = std::min(std::max(i, 0), static_cast<int>(tiles_.size()) - 1);
    if (idx < 0) {
        return {0, 0};
    }
    const TmpCellHeader& h = tiles_[static_cast<size_t>(idx)].header;
    return {h.extra_x - min_tx_, h.extra_y - min_ty_};
}

std::vector<uint32_t> TmpFile::Render_Cell_RGBA(int i, const Palette& pal,
                                                bool with_extra) const {
    std::vector<uint32_t> out;
    if (i < 0 || i >= static_cast<int>(tiles_.size()) || !tiles_[i].present) {
        return out;
    }
    out.assign(static_cast<size_t>(cell_width_) * static_cast<size_t>(cell_height_), 0);

    const TmpTile& t = tiles_[i];
    const auto rows = Row_Geometry(cell_width_, cell_height_);
    size_t row_start = 0;   // 每行都是"行首 + w 个像素"，p 必须按 w 推进，
                            // 不能因为越界提前停下（否则后面所有行全错位）。
    for (int y = 0; y < cell_height_; ++y) {
        const int x0 = rows[static_cast<size_t>(y)].first;
        const int w = rows[static_cast<size_t>(y)].second;
        uint32_t* dst = out.data() + static_cast<size_t>(y) * cell_width_;
        for (int k = 0; k < w; ++k) {
            const size_t p = row_start + static_cast<size_t>(k);
            if (p >= t.iso.size()) {
                break;
            }
            const int x = x0 + k;
            const uint8_t v = t.iso[p];
            if (v == 0 || x < 0 || x >= cell_width_) {
                continue;
            }
            const Palette::Color c = pal.Map(v);
            dst[x] = Pack_RGBA(c.r, c.g, c.b, c.a);
        }
        row_start += static_cast<size_t>(w);
    }

    if (with_extra && !t.extra.empty() && t.header.extra_width > 0) {
        // extra 落在画布坐标系（跟 TileX/TileY 同一套），减去本 cell 的落点后
        // 就是相对 cell 画布的偏移 —— 越界的部分裁掉。
        const int ox = t.header.tile_x - t.header.extra_x;
        const int oy = t.header.tile_y - t.header.extra_y;
        const int ew = static_cast<int>(t.header.extra_width);
        const int eh = static_cast<int>(t.header.extra_height);
        for (int y = 0; y < eh; ++y) {
            const int dy = y - oy;
            if (dy < 0 || dy >= cell_height_) {
                continue;
            }
            for (int x = 0; x < ew; ++x) {
                const int dx = x - ox;
                if (dx < 0 || dx >= cell_width_) {
                    continue;
                }
                const uint8_t v = t.extra[static_cast<size_t>(y) * ew + x];
                if (v == 0) {
                    continue;
                }
                const Palette::Color c = pal.Map(v);
                out[static_cast<size_t>(dy) * cell_width_ + dx] =
                    Pack_RGBA(c.r, c.g, c.b, c.a);
            }
        }
    }
    return out;
}

std::vector<uint32_t> TmpFile::Render_Cell_Padded_RGBA(int i, const Palette& pal, int pad,
                                                       int* ox, int* oy,
                                                       int* w, int* h) const {
    const int W = cell_width_ + 2 * pad;
    const int H = cell_height_ + 2 * pad;
    if (ox) *ox = pad;
    if (oy) *oy = pad;
    if (w) *w = W;
    if (h) *h = H;
    std::vector<uint32_t> out(static_cast<size_t>(W) * static_cast<size_t>(H), 0);
    if (i < 0 || i >= static_cast<int>(tiles_.size()) || !tiles_[i].present) {
        return out;
    }
    if (pad < 0 || cell_width_ <= 0 || cell_height_ <= 0) {
        return out;
    }

    // 菱形本体
    const std::vector<uint32_t> body = Render_Cell_RGBA(i, pal, false);
    for (int y = 0; y < cell_height_; ++y) {
        for (int x = 0; x < cell_width_; ++x) {
            const uint32_t px = body[static_cast<size_t>(y) * cell_width_ + x];
            if ((px & 0xFF000000u) == 0) {
                continue;
            }
            out[static_cast<size_t>(y + pad) * W + (x + pad)] = px;
        }
    }

    // extra：画布坐标 (extra_x, extra_y) 与本 cell 的 (tile_x, tile_y) 同一套，
    // 所以相对偏移就是 (extra_x - tile_x, extra_y - tile_y)。
    const TmpTile& t = tiles_[static_cast<size_t>(i)];
    if (t.extra.empty() || t.header.extra_width == 0 || !t.header.Has_Extra()) {
        return out;
    }
    const int ex = static_cast<int>(t.header.extra_x) - t.header.tile_x;
    const int ey = static_cast<int>(t.header.extra_y) - t.header.tile_y;
    const int ew = static_cast<int>(t.header.extra_width);
    const int eh = static_cast<int>(t.header.extra_height);
    for (int y = 0; y < eh; ++y) {
        const int dy = pad + ey + y;
        if (dy < 0 || dy >= H) {
            continue;
        }
        for (int x = 0; x < ew; ++x) {
            const int dx = pad + ex + x;
            if (dx < 0 || dx >= W) {
                continue;
            }
            const uint8_t v = t.extra[static_cast<size_t>(y) * ew + x];
            if (v == 0) {
                continue;
            }
            const Palette::Color c = pal.Map(v);
            out[static_cast<size_t>(dy) * W + dx] = Pack_RGBA(c.r, c.g, c.b, c.a);
        }
    }
    return out;
}

std::vector<uint32_t> TmpFile::Render_Image_RGBA(const Palette& pal, bool with_extra) const {
    const int W = canvas_width_;
    const int H = canvas_height_;
    if (W <= 0 || H <= 0) {
        return {};
    }
    std::vector<uint32_t> out(static_cast<size_t>(W) * static_cast<size_t>(H), 0);

    auto blit = [&](const uint8_t* src, int sx, int sy, int sw, int sh) {
        for (int y = 0; y < sh; ++y) {
            const int dy = sy + y;
            if (dy < 0 || dy >= H) {
                continue;
            }
            for (int x = 0; x < sw; ++x) {
                const int dx = sx + x;
                if (dx < 0 || dx >= W) {
                    continue;
                }
                const uint8_t v = src[static_cast<size_t>(y) * sw + x];
                if (v == 0) {
                    continue;
                }
                const Palette::Color c = pal.Map(v);
                out[static_cast<size_t>(dy) * W + dx] = Pack_RGBA(c.r, c.g, c.b, c.a);
            }
        }
    };

    // 先铺所有 cell 的菱形，再把 extra 压上去（extra 是"前景"，比如树冠）。
    const auto rows = Row_Geometry(cell_width_, cell_height_);
    for (const TmpTile& t : tiles_) {
        if (!t.present) {
            continue;
        }
        const int ox = t.header.tile_x - min_tx_;
        const int oy = t.header.tile_y - min_ty_;
        size_t row_start = 0;
        for (int y = 0; y < cell_height_; ++y) {
            const int x0 = rows[static_cast<size_t>(y)].first;
            const int w = rows[static_cast<size_t>(y)].second;
            const int dy = oy + y;
            if (dy >= 0 && dy < H) {
                for (int k = 0; k < w; ++k) {
                    const size_t p = row_start + static_cast<size_t>(k);
                    if (p >= t.iso.size()) {
                        break;
                    }
                    const int dx = ox + x0 + k;
                    const uint8_t v = t.iso[p];
                    if (v != 0 && dx >= 0 && dx < W) {
                        const Palette::Color c = pal.Map(v);
                        out[static_cast<size_t>(dy) * W + dx] = Pack_RGBA(c.r, c.g, c.b, c.a);
                    }
                }
            }
            row_start += static_cast<size_t>(w);
        }
    }

    if (with_extra) {
        for (const TmpTile& t : tiles_) {
            if (!t.present || t.extra.empty() || t.header.extra_width == 0) {
                continue;
            }
            blit(t.extra.data(),
                 t.header.extra_x - min_tx_, t.header.extra_y - min_ty_,
                 static_cast<int>(t.header.extra_width),
                 static_cast<int>(t.header.extra_height));
        }
    }
    return out;
}

}  // namespace ra2
