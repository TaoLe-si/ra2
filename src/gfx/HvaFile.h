// HvaFile.h -- HVA（Hierarchical Voxel Animation）体素动画变换
//
// RA2/YR 里每个 .VXL 体素模型都配一个同名 .HVA：前者存体素几何，后者存
// "每一帧、每根肢体（车身/炮塔/炮管……）该怎么摆"的 3×4 变换矩阵。
// 渲染时把 HVA 的矩阵套到 VXL 的肢体上，单位才能转向、抬起炮管。
//
// 【没有魔数】HVA 文件开头不是 "HVA!" 而是编译期残留的源文件路径
// （"N:\RA2\ASSETS\C\0"、"C:\3DSMAX\MESHE\0" 之类，正好 16 字节）。
// 游戏是按 `模型名 + ".HVA"` 拼出文件名再算 Westwood CRC 去 MIX 里查的 ——
// gamemd.exe 里能找到 ".HVA" 这个扩展名串（0x004268E4，紧挨着
// "Failed to create VoxLib!"），但找不到任何 HVA 魔数比较。
// 所以判别一个条目是不是 HVA 只能靠**长度自洽**：见 Load() 的三条判据。
//
// 布局（实测 ra2.mix 子归档 0xA8548FD9 里 183 个候选**全部**精确吻合）：
//   +0    16  char[16]   编译期残留的源文件路径（截断/补零，RA2 不读）
//   +16    4  uint32     FrameCount
//   +20    4  uint32     LimbCount
//   +24   16*LimbCount   char[16] 每根肢体名（"BODY"/"TURRET"/"DUMMY01"…，
//                        NUL 结尾，后面 15 个字节是未初始化的垃圾，必须按第一个
//                        NUL 截断 —— 实测见过 "MAIN BODY " 这种带尾空格的）
//   +..   48*Frame*Limb  float[12]  每帧每肢体的 3×4 变换矩阵
//   文件长度 = 24 + 16*LimbCount + 48*FrameCount*LimbCount
//
// 矩阵是行主序的 3 行 × 4 列：前 3 列是旋转（含缩放），第 4 列是平移。
// 实测 RAD03 的唯一一帧是恒等旋转 + translate(-1.352, -3.202, 186.803)。
//
// 【为什么必须靠长度判别】有 4 个 SHP 文件碰巧也能解出"合法"的
// FrameCount/LimbCount，只有 48*F*L 这条算术能把它们挡掉。
//
// 分布（ra2.mix 183 个）：180 个是 (1 帧, 1 肢) 的静态体素；
// 另有 (17,13)、(2,3)、(1,2) 各 1 个 —— 13 根肢体那个才是真正有动画的。

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "gfx/VxlFile.h"

namespace ra2 {

/// 一根肢体在某一帧的 3×4 变换矩阵（行主序）。
struct HvaMatrix {
    float m[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};

    float At(int row, int col) const noexcept { return m[row * 4 + col]; }

    /// 平移分量（第 4 列）。
    float Tx() const noexcept { return m[3]; }
    float Ty() const noexcept { return m[7]; }
    float Tz() const noexcept { return m[11]; }
};

class HvaFile {
public:
    /// 解析一整块 HVA 数据。没有魔数，所以只能靠长度自洽判定，见 .cpp。
    bool Load(const uint8_t* data, size_t size);

    int Frame_Count() const noexcept { return frame_count_; }
    int Limb_Count() const noexcept { return limb_count_; }

    /// 第 i 根肢体的名字（已按第一个 NUL 截断）。
    const std::string& Limb_Name(int i) const;

    /// 第 frame 帧、第 limb 根肢体的变换矩阵。越界返回恒等矩阵。
    HvaMatrix Matrix(int frame, int limb) const;

    /// 开发期残留的源文件路径（16 字节头，截断到第一个 NUL）。调试用。
    std::string Source_Path() const;

    void Reset();

private:
    int frame_count_ = 0;
    int limb_count_ = 0;
    std::string source_path_;          ///< 16 字节头里残留的源文件路径
    std::vector<std::string> limb_names_;
    std::vector<HvaMatrix> matrices_;   ///< [frame * limb_count + limb]
};

/// 判断一份 HVA 是不是某个 VXL 的动画。
///
/// 为什么需要"用内容认人"：MIX 里**没有文件名**，游戏是按
/// `模型名 + ".HVA"` 拼字符串算 Westwood CRC 去查的，而我们手上只有 CRC。
/// 好在有一个极强的指纹可用 —— **HVA 第 0 帧的矩阵就是 VXL 肢体尾的姿态**：
/// 实测（肢体名唯一的 3 组配对 / 18 根肢体）3×3 部分完全相同（最大差 0），
/// 平移则满足 `T_hva = T_vxl / det`（det 恒 1/12）。
///
/// 所以配对规则：肢数相同 + 每根肢体第 frame 帧的 R 与 T·det 都对得上。
/// 再叠加肢体名逐一相同作为确认（单肢模型名字普遍是 DUMMY01，撞车，只能当辅助）。
///
/// 【肢体名实测也是全等的】3 组多肢配对（ra2.mix 里多肢 VXL 只有 4 个）：
///   WALKER    13/13（FOOT R REAR … BODY）
///   SHAD       3/3（CYLINDER18 / CYLINDER09 / DUMMY01）
///   NAR TURNTA 2/2（SONICTUR.VXL）
/// 所以名字对不上基本可以断定配错，但它不是必需判据 —— 因为单肢模型的
/// 名字（DUMMY01 / BODY / MAIN BODY）在 183 个 HVA 里大面积撞车。
bool Hva_Matches_Vxl(const HvaFile& hva, const VxlFile& vxl, int frame = 0,
                     float eps_r = 1e-3f, float eps_t = 2e-2f);

}  // namespace ra2
