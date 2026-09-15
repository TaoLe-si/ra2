// VxlFile.h -- VXL 体素模型（RA2/YR 全部载具/建筑的 3D 几何）
//
// 这是 P0 素材层最后一块。SHP 是"画好的图"，VXL 是"真的 3D 模型"：
// 渲染器按 HVA 的变换矩阵摆好每根肢体，再把体素投影成像素，
// 所以同一辆车能转向、能抬炮管，而不是每帧一张位图。
//
// 【外层结构】184+37 个样本上 `32 + 770*NP + 28*L + BodySize + 92*L == size`
// 100% 成立：
//   +0    16B   "Voxel Animation"（唯一有魔数的 Westwood 素材格式）
//   +16   u32   PaletteCount（恒 1）
//   +20   u32   NumLimbs
//   +24   u32   NumLimbFrames（实测恒等于 NumLimbs）
//   +28   u32   BodySize
//   +32   u8    RemapStart（恒 16）
//   +33   u8    RemapEnd（恒 31）
//   +34   768B  RGB 调色板（**已经是 8 位值，不要再 <<2**）
//   +802  L×28  肢体头：name[16] + limb_number(i32) + unk1 + unk2
//         BodySize 字节的 body
//         L×92  肢体尾：
//           +0  u32 span_start_ofs  }\ 都是**相对 body 起点**的偏移，
//           +4  u32 span_end_ofs    } 指向两张 X*Y 的 u32 表
//           +8  u32 span_data_ofs   —  体素数据的起点（**不是**第三张表）
//           +12 f32 det（体素→世界单位缩放，实测 = 1/12）
//           +16 f32[12] 3×4 变换（行主序，前 3 列旋转、第 4 列平移）
//           +64 f32[3] min_bounds   +76 f32[3] max_bounds
//           +88 u8 XSize  +89 u8 YSize  +90 u8 ZSize  +91 u8 NormalsType(2 或 4)
//
// 【min_bounds/max_bounds 是干嘛的】它们是**该肢体体素在局部坐标系下的 AABB**，
// 而局部原点落在 AABB 中心 —— 13 根肢体的 (min+max)/2 实测都 ≤1.8，
// BODY 是 (0.000,-0.164,0.345)。所以体素索引不能直接喂变换，要先平移：
//     world = R · (index + min_bounds) + T
// 判据（多肢模型立刻散架 vs 落地）：见 Render_Isometric 的注释。
//
// 【只认两张表】网上不少资料说 span_start / span_end / span_data 是三张表。
// 实测不是：body 里只有 [start 表 (X*Y u32)][end 表 (X*Y u32)][体素数据]，
// span_data_ofs 是**数据区起点**。硬证据：多数肢体上 span_data_ofs + X*Y*4
// 会直接越出 body，而两张表的值域恰好落在 0..数据区长度-1。
//   空列在两张表里都是 0xFFFFFFFF。
//
// 【列内编码】2026-09-15 破解，239309 列 / 856623 体素零异常：
//   游标 z = 0，循环读：
//       delta u8    游标前移 z += delta（**增量编码**，不是绝对坐标！）
//       n     u8    本游程体素数
//       n×2B        每个体素 (colour, normal)
//       n     u8    计数再写一遍（渲染器要能反向遍历 span）
//       z += n
//   n == 0 即终止符，此时 delta 是最后一个体素之上剩的空格数，
//   终止符后面还有一个 0 字节（等价于 n=0 的重复计数）。
//   若最后一个游程正好填满到 Z，终止符**整个省略**。
//
// 举例（id=0x36B0C51B，12×11×12，Z=12）：
//   列[17] `00 0c | 24B | 0c`          → z 0..11 全满，无终止符，共 27 字节
//   列[53] `00 01 | 39 02 | 01`
//          `02 02 | 14 20 28 16 | 02`
//          `07 00 00`                    → z=0 一簇、z=3/4 一簇，顶上 7 格空
//   z 是增量这点最容易踩：列[53] 第二段写的是 02 而不是 03。

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "gfx/VoxelLight.h"

namespace ra2 {

/// 体素数据区里一张表的"空列"标记。
constexpr uint32_t kVxlNoSpan = 0xFFFFFFFFu;

/// 体素：坐标 + 调色板索引 + 法线索引。
struct VxlVoxel {
    uint8_t x = 0;
    uint8_t y = 0;
    uint8_t z = 0;
    uint8_t colour = 0;   ///< 调色板索引
    uint8_t normal = 0;   ///< 法线索引（VXL 法线表 256 项）
};

struct VxlLimbHeader {
    std::string name;      ///< 已按第一个 NUL 截断
    int32_t number = 0;    ///< limb_number，实测与序号一致
    uint32_t unk1 = 0;     ///< 实测恒 1
    uint32_t unk2 = 0;     ///< 实测恒 0（少数文件非 0）
};

struct VxlLimbTailer {
    uint32_t span_start_ofs = 0;   ///< 相对 body：start 表（X*Y u32）
    uint32_t span_end_ofs = 0;     ///< 相对 body：end 表（X*Y u32）
    uint32_t span_data_ofs = 0;    ///< 相对 body：体素数据起点
    float det = 0.0f;              ///< 体素→世界缩放
    float transform[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    float min_bounds[3] = {0, 0, 0};
    float max_bounds[3] = {0, 0, 0};
    uint8_t x_size = 0;
    uint8_t y_size = 0;
    uint8_t z_size = 0;
    uint8_t normals_type = 0;      ///< 实测只有 2 和 4
};

class VxlFile;

/// 附加模型层：把另一个 VXL（炮塔 / 炮管）按给定变换叠到主体上。
///
/// RA2 把坦克拆成三个文件，三者**共享同一模型空间原点**（实测 GTNK /
/// GTNKTUR / GTNKBARL 的肢体平移都是 `(0.377,-0.052,-0.555)`），所以这里
/// 的 transform 是**叠加在肢体自身变换之前**的模型空间变换：
///     world = Attach · (R·(index + min_bounds) + T)
/// 绕 Z 转就是炮塔朝向，绕 Y 转就是炮口俯仰（模型空间 X 向前、Y 横向、Z 向上，
/// 实测四足机甲的四只脚分别在 ±Y、±X 上）。
struct VxlAttach {
    const VxlFile* file = nullptr;
    float transform[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
};

class VxlFile {
public:
    /// 解析一整块 VXL 数据。返回 false 表示不是 VXL 或外层长度不自洽。
    bool Load(const uint8_t* data, size_t size);

    int Palette_Count() const noexcept { return palette_count_; }
    int Limb_Count() const noexcept { return limb_count_; }
    int Limb_Frame_Count() const noexcept { return num_limb_frames_; }
    uint32_t Body_Size() const noexcept { return body_size_; }
    uint8_t Remap_Start() const noexcept { return remap_start_; }
    uint8_t Remap_End() const noexcept { return remap_end_; }

    /// 768 字节 RGB，**直接可用**。
    ///
    /// 【别踩的坑】VXL 内嵌调色板常被说成"6 位值要 <<2"，实测是错的：
    /// 184 个样本里 768 个分量**全部**满足 raw ≡ 3 (mod 4)，即存的已经是
    /// `(v6<<2)|(v6>>4)` 展开后的 8 位值（63 → 255，56 → 227）。
    /// 再 <<2 会把炮塔渲成一片青紫洋红（实测踩过）。
    /// 另外 0..15 号恒为 (255,0,255) 品红，是"无此色"标记，正好在
    /// remap 区间 (16,31) 之前 —— 16..31 是御主色渐变，32+ 才是模型本体色。
    const uint8_t* Palette() const noexcept { return palette_.data(); }

    const VxlLimbHeader& Header(int limb) const;
    const VxlLimbTailer& Tailer(int limb) const;

    /// 第 limb 根肢体的第 col 列原始字节（已按 start/end 裁好）。空列返回空。
    std::vector<uint8_t> Column_Bytes(int limb, int col) const;

    /// 解出一列。strict 打开时任何一条不变量不成立都返回 false。
    bool Decode_Column(int limb, int col, std::vector<VxlVoxel>* out,
                       bool strict = true) const;

    /// 解出一根肢体的全部体素（按列序，列内按 z 升序）。
    bool Decode_Limb(int limb, std::vector<VxlVoxel>* out, bool strict = true) const;

    /// 整份文件的体素总数。strict 为 false 时按"能解多少算多少"统计。
    bool Voxel_Count(size_t* out, bool strict = true) const;

    /// 等距投影软件光栅化：产出 w×h 的调色板索引图（0 = 空白）。
    ///
    /// 这不是"取巧"—— 原引擎就是这么干的：RA2 的体素是软件光栅化成索引图
    /// 之后走同一条 blitter 管线，而不是用 D3D 画三角形。所以这里保持
    /// "索引图 + 调色板"的形态，正好接上 Dx12Renderer 的 R8 + 256×1 查表。
    ///
    /// 投影：先套每根肢体的 3×4 变换进模型空间，再按 (x-y, (x+y)/2 - z)
    /// 做等距投影（z 向上），画家算法按 x+y+z 从远到近落格。
    ///
    /// pose 是 HVA 的姿态：指向 `Limb_Count() × 12` 个 float（行主序 3×4）。
    /// 传 nullptr 就用 VXL 肢体尾自带的静态变换。
    ///
    /// attach/attach_count 是**附加模型层**：炮塔、炮管这些在 RA2 里是独立
    /// 的 VXL 文件（`<名>TUR.VXL` / `<名>BARL.VXL`），三者共享同一模型空间
    /// 原点 —— 实测 GTNK 车体顶面 z=11.01、GTNKTUR 底面 z=11.02、GTNKBARL
    /// 从炮塔内部穿出。所以只要给这一层一个额外的 3×4（比如绕 Z 转炮塔朝向），
    /// 就能直接叠上去，不需要坐标换算。见 VxlAttach。
    ///
    /// 【两套变换怎么统一】实测（3 个肢体名唯一的 VXL/HVA 配对，18 根肢体）：
    ///   * 3×3 部分两边**完全相同**（最大差 0.000000）；
    ///   * 平移 T_vxl == T_hva × det，det 恒为 1/12。
    /// 于是统一成 `world = R·(index + min_bounds) + T`，其中 T 都换算到和
    /// 体素坐标同一套单位：HVA 的平移乘 det，VXL 肢体尾的平移**已经是**
    /// 那个单位了，直接用。
    ///
    /// 注意 det **只乘平移**。写成 `world = det×(R·v + T)` 会把体素坐标缩小
    /// 12 倍而平移不变，13 根肢体立刻散成天上的一堆小方块（实测踩过）。
    ///
    /// 【光影】light 非空时每个体素会按法线算一个明暗级写进 shade_out
    /// （尺寸与 indexed 相同，取值 0..light->levels）。明暗级的算法完全照抄
    /// gamemd.exe（见 gfx/VoxelLight.h）。light 为空而 shade_out 非空时，
    /// 整张图填"最亮级"，等于不打光但结构对齐。
    /// 最终颜色由调用方算：`palette[colour] × Shade_Factor(light, level)`。
    /// shadow_light / shadow_out：地面投影阴影。
    ///
    /// shadow_light 非空时，每个体素会额外沿 **-L** 方向投到 z = ground_z 的
    /// 地面平面上，落点写进 shadow_out（w×h 的 0/255 掩膜，255 = 有阴影）。
    /// 合成本体时"本体不透明处不画阴影"就行 —— 体素是画家算法从远到近画的，
    /// 本体最后落格，自然盖住自己脚下的阴影。
    ///
    /// 投影：t = (pz - ground_z) / Lz，落点 = (px - Lx·t, py - Ly·t)。
    /// Lz 必须 > 0（光得从上面来），否则退化成垂直投影。
    ///
    /// 【画布会变大】阴影落点在模型之外，所以 bbox 要把地面投影一起纳入，
    /// 不然阴影会被裁掉一截（画布按本体的范围算是不够的）。
    bool Render_Isometric(std::vector<uint8_t>* indexed, int* out_w, int* out_h,
                          float scale = 8.0f, const float* pose = nullptr,
                          const VxlAttach* attach = nullptr,
                          int attach_count = 0,
                          const VoxelLight* light = nullptr,
                          std::vector<uint8_t>* shade_out = nullptr,
                          const float* shadow_light = nullptr,
                          float ground_z = 0.0f,
                          std::vector<uint8_t>* shadow_out = nullptr) const;

    void Reset();

private:
    std::vector<uint8_t> raw_;                ///< 保留整块，body 按偏移切
    size_t body_start_ = 0;

    int palette_count_ = 0;
    int limb_count_ = 0;
    int num_limb_frames_ = 0;
    uint32_t body_size_ = 0;
    uint8_t remap_start_ = 0;
    uint8_t remap_end_ = 0;
    std::vector<uint8_t> palette_;            ///< 768 字节
    std::vector<VxlLimbHeader> headers_;
    std::vector<VxlLimbTailer> tailers_;
};

}  // namespace ra2
