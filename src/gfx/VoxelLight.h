// VoxelLight.h -- 体素明暗（RA2 光照复刻）
//
// 这套参数**不是拍脑袋调的**，是从 gamemd.exe 里一条一条读出来的。
// 证据链（tools/lightprobe*.py）：
//
// 【1】明暗分级 = 16，不是 32 也不是 255
//   0x00758670（生成明暗 LUT 的函数，ecx=光向量 edx=NormalsType）：
//       d  = dot(normals[type][i], L)                    ; fld/fmul/fadd 三分量
//       lut[i] = (d >= 0) ? (int)(d * 16.0) : 0          ; fmul [0x7F6960]=16 -> ftol
//   0x00758930：整张 LUT 先用 0x10 填满（`rep stosd` 0x10101010 × 0x40），
//   所以"没算到的法线"默认是 16 —— **16 就是最亮一级**。
//
// 【2】明暗级 -> 亮度系数：0.6 + 0.8 * (level/16)
//   0x00758B70（LightConvert 建表）：给 32 个明暗级各算一个系数，
//   前 16 级是 `i/16 * 0.8 + 0.6`（fmul [0x7EF718]=1/16，fmul [0x7F696C]=0.8，
//   fadd [0x7F6968]=0.6），然后对 256 个调色板色做 `色 × 系数` 再找最近色，
//   存成 32×256 的重映射表。所以 level=0 -> 0.6×，level=16 -> 1.4×。
//   即 **环境光 0.6 + 漫反射 0.8**，暗处也不会黑成一团 —— 这正是 RA2 的观感。
//
// 【3】光向量是"世界光"逐物体转到物体空间
//   0x00753C80 调用 LUT 生成函数前，先用 0x5AF4D0 把全局光向量
//   （BSS 上的 0x00887470）乘上物体旋转矩阵的**转置**：L_obj = Rᵀ · L_world。
//   等价于 dot(R · N_local, L_world)，这里就用后者（法线转到世界去点乘）。
//
// 【未完全确定的一处：光向量的最终朝向】
//   能静态读到的只有：0x00754C00 把 (-0.7071041, -0.7071041, 0) 过一次 3×4 变换
//   （矩阵由 0x5AE860 / 0x5AF080 用参数 0.785375 ≈ π/4 在运行时算出来）写进
//   0x00887470。所以**水平方位 (-1,-1)/√2 是确凿的，但那个矩阵静态推不出来**。
//
//   于是用一条硬判据来定符号：`--vxlit` 会按世界法线分桶统计明暗级。
//   直接用 (-0.408,-0.408,+0.816)（即方位照抄 exe、只补仰角）的结果是
//   **47% 的像素全部压在最暗级 0** —— 因为这个方位下镜头看得见的三个面
//   (+x / +y / +z) 里有两个是背光的，画面会糊成一坨黑。
//   把水平方位绕 Z 转 180°（光转到镜头这一侧）后，同样判据下最暗级只占 21%，
//   且 顶面 11.7 > 侧面 2.6 > 底面 0.0 依然成立。默认值取的就是这个：
//   normalize(1, 1, 2) = (0.4082, 0.4082, 0.8165)。
//   要改随时用 --light x,y,z 覆盖，不必改代码。

#pragma once

#include <cstdint>
#include <vector>

namespace ra2 {

/// 体素光照参数。默认值 = gamemd.exe 实测值（见本文件头注释）。
struct VoxelLight {
    /// 世界空间光向量（**指向光源**，单位长度）。默认 normalize(1,1,2)。
    float light[3] = {0.40824829f, 0.40824829f, 0.81649658f};
    /// 环境光：背光面也有的底亮度（exe 的 0.6）。
    float ambient = 0.6f;
    /// 漫反射强度：正对光源时叠到 ambient 上（exe 的 0.8）。
    float diffuse = 0.8f;
    /// 明暗分级数（exe 的 16，即 0..16 共 17 级）。
    int levels = 16;

    /// 归一化 light，返回自身引用。
    VoxelLight& Normalize();
};

/// 一张"法线索引 -> 明暗级"的查表（对应 exe 里 0x00B45990 那个 256 字节数组）。
///
/// 注意它**依赖肢体朝向**：光向量要按每根肢体的旋转转到物体空间，
/// 所以每根肢体一张表（最多 245 项，重建一次的代价可以忽略）。
class VoxelShadeTable {
public:
    VoxelShadeTable();

    /// 重建。R 是**物体->世界**的 3×3 行主序旋转（nullptr 视为单位矩阵）。
    /// 内部算 L_obj = Rᵀ · L_world，再对法线表每一项求 dot 量化成 0..levels。
    void Build(int normals_type, const VoxelLight& light, const float* R = nullptr);

    /// 全部填 levels（= 最亮）。对应 exe 里 `rep stosd` 的初始化语义。
    void Reset_To_Brightest(int levels);

    uint8_t Level(int normal) const {
        return lut_[static_cast<unsigned>(normal) & 0xFF];
    }
    int Levels() const { return levels_; }

private:
    uint8_t lut_[256];
    int levels_ = 16;
};

/// 明暗级 -> 亮度系数。就是 exe 的 `level/16 * 0.8 + 0.6`。
inline float Shade_Factor(const VoxelLight& light, int level) {
    if (light.levels <= 0) {
        return light.ambient;
    }
    return light.ambient + light.diffuse * (static_cast<float>(level) /
                                            static_cast<float>(light.levels));
}

/// 地面投影阴影的参数。
///
/// 原版 RA2 的载具/建筑阴影是**把体素沿光线方向压到地面**，不是贴图。
/// 方位直接复用光照那套光向量（同一个太阳），所以这里只额外给出
/// 地面高度和不透明度。
///
/// 不透明度没有从 exe 里读出来（阴影混合那段还没逆到），默认 0.45 ——
/// 判据是"阴影区域要能看出比地面暗，但下面地形的纹理还得透出来"。
/// 后面逆出真值再改这一个数字。
struct VoxelShadow {
    bool on = false;
    float ground_z = 0.0f;   ///< 地面高度（体素坐标），平地就是 0
    float alpha = 0.45f;     ///< 0..1，1 = 全黑
};

/// 把阴影掩膜合进已经烘好的 RGBA。
///
/// 规则是"本体盖住的格子不画阴影"：体素是画家算法从远到近落的格，
/// 本体压掉自己脚下的影子，剩下的才是看得见的。
///
/// 【判据为什么用 indexed 而不是 alpha】离屏画布的背景是**不透明**的
/// （实测 1024×768 全部 786432 个像素 alpha 都是 255），按 alpha 挑会一个
/// 都选不中。所以用本体索引图：indexed[i] == 0 才是"这里没有本体"。
/// shadow 为 nullptr 或 n <= 0 时什么都不做。
void Composite_Shadow(uint8_t* rgba, int n, const uint8_t* indexed,
                      const uint8_t* shadow, const VoxelShadow& sh);

/// 把"索引图 + 明暗图"烘成 RGBA8（每像素 4 字节）。
///
/// 原引擎的做法是 `色 × 系数` 之后**回查调色板找最近色**（8 位色深下必须这么干）。
/// 我们直接输出 RGBA，省掉最近色查找 —— 视觉上等价且更干净，
/// 但严格说少了一步"量化回 256 色"，要完全对齐得再加一次最近色匹配。
///
/// pal768：768 字节 RGB，**已经是 8 位**（VXL 内嵌调色板不用再 <<2）。
/// 索引 0 视为透明（alpha=0），其余 alpha=255。
void Shade_To_RGBA(const uint8_t* indexed, const uint8_t* shade, int n,
                   const uint8_t* pal768, const VoxelLight& light,
                   std::vector<uint8_t>* out_rgba);

}  // namespace ra2
