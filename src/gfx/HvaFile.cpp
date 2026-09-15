#include "gfx/HvaFile.h"

#include <cmath>
#include <cstring>

namespace ra2 {
namespace {

constexpr size_t kHeaderSize = 24;      ///< 16 字节源路径 + 4 帧数 + 4 肢体数
constexpr size_t kLimbNameSize = 16;
constexpr size_t kMatrixFloats = 12;    ///< 3×4
constexpr size_t kMatrixBytes = kMatrixFloats * sizeof(float);   // 48

uint32_t RdU32(const uint8_t* p) noexcept {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

float RdF32(const uint8_t* p) noexcept {
    uint32_t bits = RdU32(p);
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

/// 把 16 字节的定长名按第一个 NUL 截断。
std::string Fixed_Name(const uint8_t* p, size_t n) {
    size_t len = 0;
    while (len < n && p[len] != 0) {
        ++len;
    }
    return std::string(reinterpret_cast<const char*>(p), len);
}

}  // namespace

void HvaFile::Reset() {
    frame_count_ = 0;
    limb_count_ = 0;
    source_path_.clear();
    limb_names_.clear();
    matrices_.clear();
}

bool HvaFile::Load(const uint8_t* data, size_t size) {
    Reset();
    if (data == nullptr || size < kHeaderSize) {
        return false;
    }
    const uint32_t frames = RdU32(data + 16);
    const uint32_t limbs = RdU32(data + 20);

    // 判据 1：计数本身要合理。上限取得比实际需求宽，但足以挡住随机字节。
    if (frames == 0 || frames > 4096 || limbs == 0 || limbs > 256) {
        return false;
    }
    // 判据 2：长度必须严丝合缝。这是最硬的一条 ——
    // 24 + 16*L + 48*F*L == size。4 个误判的 SHP 就是死在这里。
    const uint64_t need = kHeaderSize + static_cast<uint64_t>(limbs) * kLimbNameSize +
                          static_cast<uint64_t>(frames) * limbs * kMatrixBytes;
    if (need != static_cast<uint64_t>(size)) {
        return false;
    }
    // 判据 3：肢体名必须真的像名字 —— 至少要有可打印字符。
    // 随机字节几乎不可能在 16 字节里既无 NUL 又全可打印。
    for (uint32_t i = 0; i < limbs; ++i) {
        const uint8_t* p = data + kHeaderSize + i * kLimbNameSize;
        size_t len = 0;
        bool printable = false;
        while (len < kLimbNameSize && p[len] != 0) {
            if (p[len] >= 0x20 && p[len] < 0x7F) {
                printable = true;
            }
            ++len;
        }
        if (!printable) {
            return false;
        }
    }

    frame_count_ = static_cast<int>(frames);
    limb_count_ = static_cast<int>(limbs);
    source_path_ = Fixed_Name(data, 16);

    limb_names_.reserve(limb_count_);
    for (int i = 0; i < limb_count_; ++i) {
        limb_names_.push_back(
            Fixed_Name(data + kHeaderSize + static_cast<size_t>(i) * kLimbNameSize,
                       kLimbNameSize));
    }

    matrices_.resize(static_cast<size_t>(frames) * limb_count_);
    const uint8_t* base = data + kHeaderSize + static_cast<size_t>(limb_count_) * kLimbNameSize;
    for (size_t k = 0; k < matrices_.size(); ++k) {
        float* out = matrices_[k].m;
        for (size_t j = 0; j < kMatrixFloats; ++j) {
            out[j] = RdF32(base + k * kMatrixBytes + j * sizeof(float));
        }
    }
    return true;
}

const std::string& HvaFile::Limb_Name(int i) const {
    static const std::string kEmpty;
    if (i < 0 || i >= static_cast<int>(limb_names_.size())) {
        return kEmpty;
    }
    return limb_names_[static_cast<size_t>(i)];
}

HvaMatrix HvaFile::Matrix(int frame, int limb) const {
    if (frame < 0 || frame >= frame_count_ || limb < 0 || limb >= limb_count_) {
        return HvaMatrix{};
    }
    return matrices_[static_cast<size_t>(frame) * limb_count_ + limb];
}

std::string HvaFile::Source_Path() const { return source_path_; }

bool Hva_Matches_Vxl(const HvaFile& hva, const VxlFile& vxl, int frame, float eps_r,
                     float eps_t) {
    if (vxl.Limb_Count() <= 0 || hva.Limb_Count() != vxl.Limb_Count()) {
        return false;
    }
    if (frame < 0 || frame >= hva.Frame_Count()) {
        return false;
    }
    for (int l = 0; l < vxl.Limb_Count(); ++l) {
        const HvaMatrix hm = hva.Matrix(frame, l);
        const VxlLimbTailer& t = vxl.Tailer(l);
        // 3×3 部分必须一致（实测完全相等，所以给的容差很小）。
        const int rot[9] = {0, 1, 2, 4, 5, 6, 8, 9, 10};
        for (int i = 0; i < 9; ++i) {
            if (std::fabs(hm.m[rot[i]] - t.transform[rot[i]]) > eps_r) {
                return false;
            }
        }
        // 平移：T_vxl == T_hva * det。
        const float det = (t.det != 0.0f) ? t.det : 1.0f;
        const float tv[3] = {t.transform[3], t.transform[7], t.transform[11]};
        const float hv[3] = {hm.m[3], hm.m[7], hm.m[11]};
        for (int i = 0; i < 3; ++i) {
            if (std::fabs(tv[i] - hv[i] * det) > eps_t) {
                return false;
            }
        }
    }
    // 肢体名逐一相同 —— 单肢模型名字会撞车，所以只当加分项，对不上不算失败。
    return true;
}

}  // namespace ra2
