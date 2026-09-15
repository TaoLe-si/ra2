#include "gfx/VoxelLight.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "gfx/VxlNormals.h"

namespace ra2 {

VoxelLight& VoxelLight::Normalize() {
    const float len = std::sqrt(light[0] * light[0] + light[1] * light[1] +
                                light[2] * light[2]);
    if (len > 1e-6f) {
        light[0] /= len;
        light[1] /= len;
        light[2] /= len;
    }
    return *this;
}

VoxelShadeTable::VoxelShadeTable() { Reset_To_Brightest(16); }

void VoxelShadeTable::Reset_To_Brightest(int levels) {
    levels_ = levels > 0 ? levels : 16;
    for (int i = 0; i < 256; ++i) {
        lut_[i] = static_cast<uint8_t>(levels_ > 255 ? 255 : levels_);
    }
}

void VoxelShadeTable::Build(int normals_type, const VoxelLight& light,
                            const float* R) {
    levels_ = light.levels > 0 ? light.levels : 16;
    // exe 的行为：先把整张表填成"最亮"，再把用到的那部分算出来盖掉。
    // 表长只有 count 项（16/36/64/245），剩下的保持最亮 —— 这一步不能省，
    // 否则 normal 越界的体素会被算成纯黑。
    Reset_To_Brightest(levels_);

    const int slot = Normal_Slot_Of(normals_type);
    int count = 0;
    const float* tab = Voxel_Normal_Table(slot, &count);
    if (tab == nullptr || count <= 0) {
        return;
    }

    // L_obj = Rᵀ · L_world。R 是行主序 3×3（元素 0,1,2 / 3,4,5 / 6,7,8）。
    float lx = light.light[0], ly = light.light[1], lz = light.light[2];
    if (R != nullptr) {
        lx = R[0] * light.light[0] + R[3] * light.light[1] + R[6] * light.light[2];
        ly = R[1] * light.light[0] + R[4] * light.light[1] + R[7] * light.light[2];
        lz = R[2] * light.light[0] + R[5] * light.light[1] + R[8] * light.light[2];
    }

    const float kLevels = static_cast<float>(levels_);
    for (int i = 0; i < count && i < 256; ++i) {
        const float nx = tab[i * 3 + 0];
        const float ny = tab[i * 3 + 1];
        const float nz = tab[i * 3 + 2];
        const float d = nx * lx + ny * ly + nz * lz;
        int lv = 0;
        if (d > 0.0f) {
            // exe 用的是 ftol（截断），不是四舍五入 —— 保持一致。
            lv = static_cast<int>(d * kLevels);
            if (lv > levels_) lv = levels_;
        }
        lut_[i] = static_cast<uint8_t>(lv);
    }
}

void Shade_To_RGBA(const uint8_t* indexed, const uint8_t* shade, int n,
                   const uint8_t* pal768, const VoxelLight& light,
                   std::vector<uint8_t>* out_rgba) {
    if (out_rgba == nullptr || n <= 0) {
        return;
    }
    out_rgba->assign(static_cast<size_t>(n) * 4, 0);
    // 17 级明暗（0..16）的系数先算好，省掉每像素一次乘除。
    float factor[257];
    const int nlev = light.levels > 0 ? light.levels : 16;
    for (int i = 0; i <= 256; ++i) {
        factor[i] = Shade_Factor(light, i);
    }
    for (int i = 0; i < n; ++i) {
        const uint8_t idx = (indexed != nullptr) ? indexed[i] : 0;
        const size_t o = static_cast<size_t>(i) * 4;
        if (idx == 0) {
            continue;  // 透明
        }
        const uint8_t lv = (shade != nullptr) ? shade[i] : static_cast<uint8_t>(nlev);
        const float k = factor[lv];
        const uint8_t* c = pal768 + static_cast<size_t>(idx) * 3;
        out_rgba->at(o + 0) = static_cast<uint8_t>(
            std::min(255.0f, static_cast<float>(c[0]) * k));
        out_rgba->at(o + 1) = static_cast<uint8_t>(
            std::min(255.0f, static_cast<float>(c[1]) * k));
        out_rgba->at(o + 2) = static_cast<uint8_t>(
            std::min(255.0f, static_cast<float>(c[2]) * k));
        out_rgba->at(o + 3) = 255;
    }
}

}  // namespace ra2
