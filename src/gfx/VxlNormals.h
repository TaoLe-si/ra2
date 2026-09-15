// VxlNormals.h -- RA2/YR 体素法线表（数据见 VxlNormals.cpp，由脚本生成）
//
// VXL 每个体素带一个 normal 索引（0..255），法线向量却是引擎内置的、不在文件里。
// 这里就是那张表：4 张 LOD 表，精度递增，**槽位 = VXL 头的 NormalsType - 1**。

#pragma once

namespace ra2 {

/// 法线表的槽位数（= NormalsType 的最大值）。
constexpr int kNormalSlotCount = 4;

/// 取第 slot 张法线表。返回的是 [count][3] 的 float 数组（单位向量）。
/// slot 越界会被夹到 [0, kNormalSlotCount-1]。
/// out_count 可以为 nullptr。
const float* Voxel_Normal_Table(int slot, int* out_count = nullptr);

/// VXL 的 NormalsType -> 法线表槽位。就减 1，夹到合法范围。
inline int Normal_Slot_Of(int normals_type) {
    int s = normals_type - 1;
    if (s < 0) s = 0;
    if (s >= kNormalSlotCount) s = kNormalSlotCount - 1;
    return s;
}

}  // namespace ra2
