#include "gfx/VxlFile.h"

#include <algorithm>
#include <cstring>

namespace ra2 {
namespace {

constexpr size_t kHeaderBase = 32;     // 魔数 16 + u32×4
constexpr size_t kPaletteBlock = 770;  // 2 字节 remap 区间 + 768 字节调色板
constexpr size_t kLimbHeader = 28;
constexpr size_t kLimbTailer = 92;

uint32_t Rd_U32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

int32_t Rd_I32(const uint8_t* p) { return static_cast<int32_t>(Rd_U32(p)); }

float Rd_F32(const uint8_t* p) {
    const uint32_t u = Rd_U32(p);
    float f = 0.0f;
    std::memcpy(&f, &u, sizeof(f));
    return f;
}

/// 按第一个 NUL 截断，并把不可打印字符挡掉（肢体名必须是 ASCII）。
std::string Fixed_Name(const uint8_t* p, size_t n) {
    size_t len = 0;
    while (len < n && p[len] != 0) {
        ++len;
    }
    return std::string(reinterpret_cast<const char*>(p), len);
}

}  // namespace

void VxlFile::Reset() {
    raw_.clear();
    body_start_ = 0;
    palette_count_ = 0;
    limb_count_ = 0;
    num_limb_frames_ = 0;
    body_size_ = 0;
    remap_start_ = 0;
    remap_end_ = 0;
    palette_.clear();
    headers_.clear();
    tailers_.clear();
}

bool VxlFile::Load(const uint8_t* data, size_t size) {
    Reset();
    // 判据 1：魔数。VXL 是 Westwood 素材里唯一有魔数的格式。
    if (data == nullptr || size < kHeaderBase + kPaletteBlock) {
        return false;
    }
    if (std::memcmp(data, "Voxel Animation", 15) != 0) {
        return false;
    }

    palette_count_ = static_cast<int>(Rd_U32(data + 16));
    limb_count_ = static_cast<int>(Rd_U32(data + 20));
    num_limb_frames_ = static_cast<int>(Rd_U32(data + 24));
    body_size_ = Rd_U32(data + 28);
    remap_start_ = data[32];
    remap_end_ = data[33];

    // 判据 2：计数必须合理。palette_count 实测恒 1，但格式上允许更多。
    if (palette_count_ < 1 || palette_count_ > 8) {
        return false;
    }
    if (limb_count_ < 1 || limb_count_ > 512) {
        return false;
    }

    const size_t header_size =
        kHeaderBase + kPaletteBlock * static_cast<size_t>(palette_count_);
    const size_t limb_hdr_end = header_size + kLimbHeader * static_cast<size_t>(limb_count_);
    const size_t body_end = limb_hdr_end + body_size_;
    const size_t tailer_end = body_end + kLimbTailer * static_cast<size_t>(limb_count_);

    // 判据 3（最硬）：整份文件长度必须刚好等于三个区段拼起来。
    // 就是这一条把 4 个碰巧能解出合法帧数/肢数的 SHP 挡在门外的。
    if (tailer_end != size) {
        return false;
    }

    raw_.assign(data, data + size);
    body_start_ = limb_hdr_end;

    palette_.assign(data + kHeaderBase + 2, data + kHeaderBase + 2 + 768);

    headers_.reserve(static_cast<size_t>(limb_count_));
    for (int i = 0; i < limb_count_; ++i) {
        const uint8_t* p = raw_.data() + header_size + kLimbHeader * static_cast<size_t>(i);
        VxlLimbHeader h;
        h.name = Fixed_Name(p, 16);
        // 名字必须全是可打印 ASCII，否则说明我们把别的东西当成 VXL 了。
        for (char c : h.name) {
            if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7E) {
                return false;
            }
        }
        h.number = Rd_I32(p + 16);
        h.unk1 = Rd_U32(p + 20);
        h.unk2 = Rd_U32(p + 24);
        headers_.push_back(std::move(h));
    }

    tailers_.reserve(static_cast<size_t>(limb_count_));
    for (int i = 0; i < limb_count_; ++i) {
        const uint8_t* p = raw_.data() + body_end + kLimbTailer * static_cast<size_t>(i);
        VxlLimbTailer t;
        t.span_start_ofs = Rd_U32(p + 0);
        t.span_end_ofs = Rd_U32(p + 4);
        t.span_data_ofs = Rd_U32(p + 8);
        t.det = Rd_F32(p + 12);
        for (int k = 0; k < 12; ++k) {
            t.transform[k] = Rd_F32(p + 16 + 4 * k);
        }
        for (int k = 0; k < 3; ++k) {
            t.min_bounds[k] = Rd_F32(p + 64 + 4 * k);
            t.max_bounds[k] = Rd_F32(p + 76 + 4 * k);
        }
        t.x_size = p[88];
        t.y_size = p[89];
        t.z_size = p[90];
        t.normals_type = p[91];
        tailers_.push_back(t);
    }
    return true;
}

const VxlLimbHeader& VxlFile::Header(int limb) const {
    static const VxlLimbHeader kEmpty;
    if (limb < 0 || limb >= limb_count_) {
        return kEmpty;
    }
    return headers_[static_cast<size_t>(limb)];
}

const VxlLimbTailer& VxlFile::Tailer(int limb) const {
    static const VxlLimbTailer kEmpty;
    if (limb < 0 || limb >= limb_count_) {
        return kEmpty;
    }
    return tailers_[static_cast<size_t>(limb)];
}

std::vector<uint8_t> VxlFile::Column_Bytes(int limb, int col) const {
    if (limb < 0 || limb >= limb_count_) {
        return {};
    }
    const VxlLimbTailer& t = tailers_[static_cast<size_t>(limb)];
    const size_t n = static_cast<size_t>(t.x_size) * t.y_size;
    if (col < 0 || static_cast<size_t>(col) >= n || body_start_ == 0) {
        return {};
    }
    // 两张表都住在 body 里，偏移相对 body 起点。
    const uint8_t* body = raw_.data() + body_start_;
    const size_t body_avail = body_size_;
    if (t.span_start_ofs + n * 4 > body_avail || t.span_end_ofs + n * 4 > body_avail) {
        return {};
    }
    const uint32_t a = Rd_U32(body + t.span_start_ofs + 4 * static_cast<size_t>(col));
    const uint32_t b = Rd_U32(body + t.span_end_ofs + 4 * static_cast<size_t>(col));
    if (a == kVxlNoSpan || b == kVxlNoSpan || b < a) {
        return {};
    }
    const size_t base = t.span_data_ofs + a;
    const size_t len = static_cast<size_t>(b - a) + 1;
    if (base + len > body_avail) {
        return {};
    }
    return std::vector<uint8_t>(body + base, body + base + len);
}

bool VxlFile::Decode_Column(int limb, int col, std::vector<VxlVoxel>* out,
                            bool strict) const {
    if (out == nullptr) {
        return false;
    }
    const std::vector<uint8_t> b = Column_Bytes(limb, col);
    if (b.empty()) {
        return true;  // 空列不算失败
    }
    const VxlLimbTailer& tl = Tailer(limb);
    const int z_size = tl.z_size;
    if (tl.x_size == 0) {
        return false;
    }
    const uint8_t x = static_cast<uint8_t>(col % tl.x_size);
    const uint8_t y = static_cast<uint8_t>(col / tl.x_size);

    size_t i = 0;
    int z = 0;
    while (i < b.size()) {
        if (i + 2 > b.size()) {
            return !strict;
        }
        z += b[i];                    // 增量编码：游标前移
        const uint8_t n = b[i + 1];
        i += 2;
        if (n == 0) {                 // 终止符
            if (i >= b.size()) {
                return !strict;
            }
            if (strict && b[i] != 0) {
                return false;
            }
            if (strict && z != z_size) {
                return false;
            }
            ++i;
            break;
        }
        if (z + n > z_size) {
            return !strict;
        }
        if (i + static_cast<size_t>(n) * 2 + 1 > b.size()) {
            return !strict;
        }
        for (int k = 0; k < n; ++k) {
            VxlVoxel v;
            v.x = x;
            v.y = y;
            v.z = static_cast<uint8_t>(z + k);
            v.colour = b[i];
            v.normal = b[i + 1];
            i += 2;
            out->push_back(v);
        }
        if (strict && b[i] != n) {    // 计数重复字节必须等于 n
            return false;
        }
        ++i;
        z += n;
    }
    if (strict && i != b.size()) {    // 必须刚好消费完
        return false;
    }
    return true;
}

bool VxlFile::Decode_Limb(int limb, std::vector<VxlVoxel>* out, bool strict) const {
    if (out == nullptr || limb < 0 || limb >= limb_count_) {
        return false;
    }
    const VxlLimbTailer& t = tailers_[static_cast<size_t>(limb)];
    const size_t n = static_cast<size_t>(t.x_size) * t.y_size;
    for (size_t c = 0; c < n; ++c) {
        if (!Decode_Column(limb, static_cast<int>(c), out, strict)) {
            return false;
        }
    }
    return true;
}

bool VxlFile::Render_Isometric(std::vector<uint8_t>* indexed, int* out_w, int* out_h,
                               float scale) const {
    if (indexed == nullptr || out_w == nullptr || out_h == nullptr || scale <= 0.0f) {
        return false;
    }
    struct Pt {
        float sx, sy, depth;
        uint8_t c;
    };
    std::vector<Pt> pts;
    std::vector<VxlVoxel> vox;
    for (int l = 0; l < limb_count_; ++l) {
        const VxlLimbTailer& t = tailers_[static_cast<size_t>(l)];
        vox.clear();
        if (!Decode_Limb(l, &vox)) {
            return false;
        }
        const float* m = t.transform;
        for (const VxlVoxel& v : vox) {
            const float px = m[0] * v.x + m[1] * v.y + m[2] * v.z + m[3];
            const float py = m[4] * v.x + m[5] * v.y + m[6] * v.z + m[7];
            const float pz = m[8] * v.x + m[9] * v.y + m[10] * v.z + m[11];
            const float k = 0.70710678f;
            Pt p;
            p.sx = (px - py) * k;
            p.sy = (px + py) * k * 0.5f - pz;
            p.depth = px + py + pz;
            p.c = v.colour;
            pts.push_back(p);
        }
    }
    if (pts.empty()) {
        return false;
    }
    float x0 = pts[0].sx, x1 = pts[0].sx, y0 = pts[0].sy, y1 = pts[0].sy;
    for (const Pt& p : pts) {
        if (p.sx < x0) x0 = p.sx;
        if (p.sx > x1) x1 = p.sx;
        if (p.sy < y0) y0 = p.sy;
        if (p.sy > y1) y1 = p.sy;
    }
    const int w = static_cast<int>((x1 - x0) * scale) + 2;
    const int h = static_cast<int>((y1 - y0) * scale) + 2;
    if (w <= 0 || h <= 0 || static_cast<int64_t>(w) * h > 64LL * 1024 * 1024) {
        return false;
    }
    std::sort(pts.begin(), pts.end(),
              [](const Pt& a, const Pt& b) { return a.depth < b.depth; });

    indexed->assign(static_cast<size_t>(w) * h, 0);
    const int cell = static_cast<int>(scale);
    for (const Pt& p : pts) {
        const int cx = static_cast<int>((p.sx - x0) * scale);
        const int cy = static_cast<int>((p.sy - y0) * scale);
        for (int dy = 0; dy < cell; ++dy) {
            const int yy = cy + dy;
            if (yy < 0 || yy >= h) {
                continue;
            }
            for (int dx = 0; dx < cell; ++dx) {
                const int xx = cx + dx;
                if (xx < 0 || xx >= w) {
                    continue;
                }
                (*indexed)[static_cast<size_t>(yy) * w + xx] = p.c;
            }
        }
    }
    *out_w = w;
    *out_h = h;
    return true;
}

bool VxlFile::Voxel_Count(size_t* out, bool strict) const {
    if (out == nullptr) {
        return false;
    }
    size_t total = 0;
    std::vector<VxlVoxel> tmp;
    for (int l = 0; l < limb_count_; ++l) {
        tmp.clear();
        if (!Decode_Limb(l, &tmp, strict)) {
            return false;
        }
        total += tmp.size();
    }
    *out = total;
    return true;
}

}  // namespace ra2
