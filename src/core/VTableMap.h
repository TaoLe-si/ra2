// VTableMap.h -- 已定位的虚函数表与推断出的继承层次
//
// 来源：tools/analyze.py 定界 + tools/vtmap.py 推断，数据见 db/vtables.json / db/vtmap.json。
//
// 【重要】定界算法的修正
// 初版把"是否已被识别为函数起点"当作虚表的延续条件，结果虚表被大量截断：
// 0x007E19D0 实际至少 24 个槽位，却只识别出 13 个 —— 因为虚函数里有一部分是
// thunk 或非标准开场，不在函数表里。
// 改成"值落在 .text 范围内即可延续 + 用代码引用点定界"之后，
// 虚表从 231 张增加到 1026 张，槽位分布才符合真实情况。
//
// 【继承判据】
// 不能用"前缀完全一致"：派生类一旦重写（override）基类虚函数，对应槽位的值就变了，
// 前缀必然断开（实测 231 张里一张都匹配不上，全是"根"）。
// 正确做法是在基类的槽位范围内统计一致比例——未被重写的槽位仍指向基类的实现。
//
// 【结论】
// 下面这张图是**结构推断**，不是查表结果，需要人工确认后才能当成事实使用。
// 但它给出的骨架非常清晰，可以直接指导 C++ 类的还原顺序。

#pragma once

#include <cstdint>

namespace ra2 {
namespace vtable {

// ---------------------------------------------------------------------------
// 推断出的主继承链（槽位数递增 = 虚函数逐层增加）
// ---------------------------------------------------------------------------
//   0x007E8FE8  128 槽   根（未找到更短的匹配者）
//   0x007E46E4  125 槽   <- 0x007E3354 (79%)
//   0x007E3354  124 槽   <- 0x007EF954 (81%)
//   0x007EF954  123 槽   <- 0x007EF060 (91%)
//   ┌ 0x007EF060  122 槽 \
//   │ 0x007F32FC  122 槽  |
//   │ 0x007F66A8  122 槽  |
//   │ 0x007F6BF4  122 槽  | 这 9 张槽位数相同、彼此高度相似，
//   │ 0x007EF3D4  122 槽  | 且都以 0x007EC258(109槽) 为共同基类（79%~90%）
//   │ 0x007F6318  122 槽  | —— 典型的"同一层派生、各自重写不同虚函数"形态
//   │ 0x007F66A8  122 槽  |
//   │ 0x007E3AD0  122 槽  |
//   └ 0x007F522C  122 槽 /
//       ↓
//   0x007EC258  109 槽   共同基类
//
// 对照 RTTI 已知的类名（db/classes.md）：
//   AbstractClass -> ObjectClass -> TechnoClass -> FootClass
//                                              -> { UnitClass, InfantryClass, AircraftClass }
//                                              -> BuildingClass
// 最自然的对应是：109 槽 ≈ ObjectClass 一带，9 张 122 槽 ≈ TechnoClass 及其派生族。
// 但这只是最合理的猜测，**尚未证实**，不要直接写进结构体布局。

/// 共同基类（109 槽），上面 9 张 122 槽虚表的父级。
inline constexpr uint32_t kRootBase109 = 0x007EC258;

/// 9 张 122 槽虚表 —— 同一继承层，各自重写了不同虚函数。
inline constexpr uint32_t kFamily122[] = {
    0x007E3AD0, 0x007EF060, 0x007EF3D4, 0x007EFB9C, 0x007F32FC,
    0x007F522C, 0x007F6318, 0x007F66A8, 0x007F6BF4,
};
inline constexpr int kFamily122Count =
    static_cast<int>(sizeof(kFamily122) / sizeof(kFamily122[0]));

/// 逐级派生的三张（槽位递增）。
inline constexpr uint32_t kLevel123 = 0x007EF954;
inline constexpr uint32_t kLevel124 = 0x007E3354;
inline constexpr uint32_t kLevel125 = 0x007E46E4;
inline constexpr uint32_t kRoot128 = 0x007E8FE8;

// ---------------------------------------------------------------------------
// 其余统计
// ---------------------------------------------------------------------------
// 虚表总数：1026（只扫 .rdata，槽位 3..128）
// 被代码引用过的：800（说明各自都有构造函数写入）
// 有继承候选的：116
//
// 槽位分布里 3 槽的有 126 张、4 槽 37 张 —— 这些是 COM 接口的 IUnknown
// （QueryInterface / AddRef / Release）。RTTI 里确实有一大堆 I* 接口：
// IUnknown、IClassFactory、IPersist、IStream、ILocomotion、IGameMap、IHouse 等。

}  // namespace vtable
}  // namespace ra2
