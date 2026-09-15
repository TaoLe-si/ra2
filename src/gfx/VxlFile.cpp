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

namespace {

/// 3×4 行主序矩阵相乘：先 c = a·b（与 VxlAttach 的语义一致：a 叠在 b 之前）。
void Mat34_Mul(const float* a, const float* b, float* c) {
    for (int r = 0; r < 3; ++r) {
        for (int col = 0; col < 3; ++col) {
            c[r * 4 + col] = a[r * 4 + 0] * b[0 * 4 + col] +
                             a[r * 4 + 1] * b[1 * 4 + col] +
                             a[r * 4 + 2] * b[2 * 4 + col];
        }
        c[r * 4 + 3] = a[r * 4 + 0] * b[3] + a[r * 4 + 1] * b[7] +
                       a[r * 4 + 2] * b[11] + a[r * 4 + 3];
    }
}

}  // namespace

bool VxlFile::Render_Isometric(std::vector<uint8_t>* indexed, int* out_w, int* out_h,
                               float scale, const float* pose, const VxlAttach* attach,
                                int attach_count, const VoxelLight* light,
                                std::vector<uint8_t>* shade_out,
                                const float* shadow_light, float ground_z,
                                std::vector<uint8_t>* shadow_out) const {
    if (indexed == nullptr || out_w == nullptr || out_h == nullptr || scale <= 0.0f) {
        return false;
    }
    if (attach_count < 0 || (attach_count > 0 && attach == nullptr)) {
        return false;
    }
    struct Pt {
        float sx, sy, depth;
        float gx = 0.0f, gy = 0.0f;   ///< 地面投影落点（只有算阴影时才有效）
        uint8_t c;
        uint8_t shade;
    };
    std::vector<Pt> pts;
    std::vector<VxlVoxel> vox;
    VoxelShadeTable shade;

    // 把一个 VXL 的体素全投影成 (sx, sy, depth, colour) 追加到 pts。
    //
    // pre 是**前置**变换（附加层用，比如绕 Z 转炮塔）；pose 是 HVA 姿态；
    // pre 为空时针就等价于只有 pose 或静态肢体尾。
    auto add_model = [&](const VxlFile& f, const float* m_pose, const float* pre) -> bool {
        for (int l = 0; l < f.limb_count_; ++l) {
            const VxlLimbTailer& t = f.tailers_[static_cast<size_t>(l)];
            vox.clear();
            if (!f.Decode_Limb(l, &vox)) {
                return false;
            }
            // 统一到"体素坐标那一套单位"的 R|T。
            //
            // 【det 用在哪，实测踩过】det 只乘**平移**，绝不能乘体素坐标。
            // 曾经写成 world = det×(R·v + T)，结果体素坐标被缩小 12 倍而平移没变，
            // 四足机甲的 13 根肢体被拆成散落在天上的一堆小方块。
            // 判据是静态姿态：VXL 肢体尾直接当 R|T 用时，13 根肢体的位置是自洽的 ——
            // 四只脚 z≈0.85、小腿 z≈9.5、大腿 z≈16.6、车体 z≈20.1，四脚分居四角。
            // 而 HVA 的平移是同一槽位的 12 倍（T_vxl = T_hva × det），所以把
            // HVA 的平移乘上 det 就和 VXL 肢体尾同单位了。
            float m[12];
            if (m_pose != nullptr) {
                for (int i = 0; i < 12; ++i) {
                    m[i] = m_pose[static_cast<size_t>(l) * 12 + i];
                }
                const float d = (t.det != 0.0f) ? t.det : 1.0f;
                m[3] *= d;
                m[7] *= d;
                m[11] *= d;
            } else {
                std::memcpy(m, t.transform, sizeof(float) * 12);
            }
            if (pre != nullptr) {
                float mm[12];
                Mat34_Mul(pre, m, mm);
                std::memcpy(m, mm, sizeof(mm));
            }
            // 【肢体局部原点不是索引 (0,0,0)】实测踩过：把体素索引直接喂给变换，
            // 单肢模型（占 184 个里的 180 个）看不出问题，多肢模型立刻散架 ——
            // JEEP 的 GUN01 会嵌进车身中部，四足机甲的"车体"会挪到四足重心前面
            // 24.6 格远的位置。
            //
            // 正确读法是读肢体尾自带的 min_bounds/max_bounds：它们是**该肢体体素
            // 在局部坐标系下的 AABB**，而局部原点在 AABB 中心（实测 13 根肢体的
            // (min+max)/2 都 ≤1.8，BODY 是 (0.000,-0.164,0.345)）。也就是说
            // 局部坐标 = 体素索引 + min_bounds，再套 R|T：
            //     world = R · (index + min_bounds) + T
            // 三条独立佐证（都要求"落地/相接触"）：
            //   * JEEP  车身 z 0.09..12.30（落地），GUN01 z 11.96..14.58 正压车顶；
            //   * 四足机甲 脚 z 0.15..5.03（踩地），小腿/大腿/车体依次叠上去；
            //   * SHAD  DUMMY01 z 0.11..25.61（落地）。
            // 用 (N-1)/2 当枢轴会差 0.5~3.6 格：GUN01 会被整个埋进车身里看不到。
            const float lox = t.min_bounds[0];
            const float loy = t.min_bounds[1];
            const float loz = t.min_bounds[2];
            // 明暗查表：光向量按**本肢体**的旋转转到物体空间，再对法线表逐项
            // 量化。每根肢体一张表（最多 245 项），和 exe 的做法一致
            // （exe 也是每个物体调用一次 LUT 生成，传的是同一个光向量）。
            if (light != nullptr) {
                const float R[9] = {m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10]};
                shade.Build(t.normals_type, *light, R);
            } else {
                shade.Reset_To_Brightest(light ? light->levels : 16);
            }
            for (const VxlVoxel& v : vox) {
                const float lx = static_cast<float>(v.x) + lox;
                const float ly = static_cast<float>(v.y) + loy;
                const float lz = static_cast<float>(v.z) + loz;
                const float px = m[0] * lx + m[1] * ly + m[2] * lz + m[3];
                const float py = m[4] * lx + m[5] * ly + m[6] * lz + m[7];
                const float pz = m[8] * lx + m[9] * ly + m[10] * lz + m[11];
                const float k = 0.70710678f;
                Pt p;
                p.sx = (px - py) * k;
                p.sy = (px + py) * k * 0.5f - pz;
                p.depth = px + py + pz;
                p.c = v.colour;
                p.shade = shade.Level(v.normal);
                if (shadow_light != nullptr) {
                    // 沿 -L 投到地面：t = (pz - ground_z) / Lz。
                    // Lz <= 0（光从下面来）时退化成垂直投影，免得除零或投到天上。
                    const float lz = shadow_light[2];
                    float qx = px, qy = py;
                    if (lz > 1e-4f) {
                        const float t = (pz - ground_z) / lz;
                        qx = px - shadow_light[0] * t;
                        qy = py - shadow_light[1] * t;
                    }
                    p.gx = (qx - qy) * k;
                    p.gy = (qx + qy) * k * 0.5f - ground_z;
                }
                pts.push_back(p);
            }
        }
        return true;
    };

    if (!add_model(*this, pose, nullptr)) {
        return false;
    }
    for (int a = 0; a < attach_count; ++a) {
        if (attach[a].file == nullptr) {
            continue;
        }
        // 附加层：炮塔/炮管没有自己的 HVA 动画帧（实测都是 1×1 的静态 HVA），
        // 所以这里固定用静态肢体尾，只叠 attach 的变换。
        if (!add_model(*attach[a].file, nullptr, attach[a].transform)) {
            return false;
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
        // 阴影落点在模型之外，不一起算进 bbox 就会被裁掉
        if (shadow_light != nullptr) {
            if (p.gx < x0) x0 = p.gx;
            if (p.gx > x1) x1 = p.gx;
            if (p.gy < y0) y0 = p.gy;
            if (p.gy > y1) y1 = p.gy;
        }
    }
    const int w = static_cast<int>((x1 - x0) * scale) + 2;
    const int h = static_cast<int>((y1 - y0) * scale) + 2;
    if (w <= 0 || h <= 0 || static_cast<int64_t>(w) * h > 64LL * 1024 * 1024) {
        return false;
    }
    std::sort(pts.begin(), pts.end(),
              [](const Pt& a, const Pt& b) { return a.depth < b.depth; });

    indexed->assign(static_cast<size_t>(w) * h, 0);
    if (shade_out != nullptr) {
        shade_out->assign(static_cast<size_t>(w) * h, 0);
    }

    // 阴影掩膜：不排序（是 0/1 覆盖，谁先谁后无所谓），画在本体之前。
    if (shadow_light != nullptr && shadow_out != nullptr) {
        shadow_out->assign(static_cast<size_t>(w) * h, 0);
        const int scell = (static_cast<int>(scale) < 1) ? 1 : static_cast<int>(scale);
        for (const Pt& p : pts) {
            const int cx = static_cast<int>((p.gx - x0) * scale);
            const int cy = static_cast<int>((p.gy - y0) * scale);
            for (int dy = 0; dy < scell; ++dy) {
                const int yy = cy + dy;
                if (yy < 0 || yy >= h) {
                    continue;
                }
                for (int dx = 0; dx < scell; ++dx) {
                    const int xx = cx + dx;
                    if (xx < 0 || xx >= w) {
                        continue;
                    }
                    (*shadow_out)[static_cast<size_t>(yy) * w + xx] = 255;
                }
            }
        }
    }
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
                const size_t o = static_cast<size_t>(yy) * w + xx;
                (*indexed)[o] = p.c;
                if (shade_out != nullptr) {
                    (*shade_out)[o] = p.shade;
                }
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
