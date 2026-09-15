// VTableMap.h -- 虚函数表与类层次的事实记录
//
// 【本文件已被 RTTI 实证推翻，以下内容仅为过程记录】
// 早前没有解析出 RTTI 的 CompleteObjectLocator 时，这里记录的是「靠槽位重叠率
// 推断」的继承骨架：109 槽共同基类 0x007EC258 <- 9 张 122 槽 <- 123/124/125 槽。
// 那个推断**是错的**。真实情况（RTTI 原文，见 src/re/ClassHierarchy.h）：
//
//   0x007EC258 = IsometricTileClass，122 槽（不是 109 —— 启发式定界把它截短了）
//   所谓的「9 张 122 槽同一层」其实是 ObjectClass 的各个直接派生类
//   真正的链是 AbstractClass -> ObjectClass -> MissionClass -> RadioClass -> TechnoClass
//
// 教训：虚表定界必须用 RTTI 的 COL 反查，靠「值落在 .text」延伸会同时
// 少算（遇到 thunk）和多算（相邻虚表粘连）。
//
// 【现在的权威数据源】
//   src/re/ClassHierarchy.h —— 由 tools/genmodel.py 从 db/rtti.json 自动生成：
//   949 个类、12717 个槽位，含 ClassInfo 表、FindClass / FindClassByVTable /
//   VirtualEntry / IsDerivedFrom 四个查询函数。
//   docs/class-hierarchy.md —— 同样内容的人类可读版。

#pragma once

#include <cstdint>

#include "re/ClassHierarchy.h"

namespace ra2 {
namespace vtable {

// ---------------------------------------------------------------------------
// 游戏对象模型（RTTI 实证；槽位数 = 主虚表槽位数）
// ---------------------------------------------------------------------------
//   AbstractClass        24  0x007E1F50  (多重继承 IPersistStream)
//   ├─ ObjectClass      122  0x007EF060
//   │  ├─ MissionClass  157  0x007EDCC0
//   │  │  └─ RadioClass  161  0x007F0508
//   │  │     └─ TechnoClass 309  0x007F4960
//   │  │        ├─ FootClass     341  0x007E8C94
//   │  │        │  ├─ UnitClass     344  0x007F5C70
//   │  │        │  ├─ InfantryClass 343  0x007EB058
//   │  │        │  └─ AircraftClass 341  0x007E22A4
//   │  │        └─ BuildingClass    322  0x007E3EBC
//   │  ├─ AnimClass 124 / BulletClass 125 / ParticleClass 123
//   │  └─ TerrainClass, OverlayClass, SmudgeClass, IsometricTileClass,
//   │     VoxelAnimClass, WaveClass, ParticleSystemClass,
//   │     BuildingLightClass, VeinholeMonsterClass        （各 122）
//   ├─ AbstractTypeClass  27  0x007E2000
//   │  └─ ObjectTypeClass 40  0x007EF2D8
//   │     └─ TechnoTypeClass 48  0x007F4ED8 -> {Aircraft,Building,Infantry,Unit}TypeClass
//   └─ HouseClass, CellClass, FactoryClass, TeamClass, TriggerClass, TagClass, ... (24)
//
// TechnoClass 309 槽、UnitClass 344 槽 —— 这两个数字对多核改造直接相关：
// 对象越大、虚函数越多，按类型分派（type-switch）的开销越值得被
// 按类批量化的流水线取代。详见 docs/multicore-plan.md。

/// 核心对象模型的类在 ClassHierarchy 表中的索引（避免到处硬编码 VA）。
namespace cls {
inline constexpr int kAbstractClass = ra2::re::kIndexAbstractClass;
inline constexpr int kObjectClass = ra2::re::kIndexObjectClass;
inline constexpr int kMissionClass = ra2::re::kIndexMissionClass;
inline constexpr int kRadioClass = ra2::re::kIndexRadioClass;
inline constexpr int kTechnoClass = ra2::re::kIndexTechnoClass;
inline constexpr int kFootClass = ra2::re::kIndexFootClass;
inline constexpr int kUnitClass = ra2::re::kIndexUnitClass;
inline constexpr int kInfantryClass = ra2::re::kIndexInfantryClass;
inline constexpr int kAircraftClass = ra2::re::kIndexAircraftClass;
inline constexpr int kBuildingClass = ra2::re::kIndexBuildingClass;
inline constexpr int kAnimClass = ra2::re::kIndexAnimClass;
inline constexpr int kBulletClass = ra2::re::kIndexBulletClass;
inline constexpr int kCellClass = ra2::re::kIndexCellClass;
inline constexpr int kHouseClass = ra2::re::kIndexHouseClass;
}  // namespace cls

/// 判断一个运行时 vptr 是否属于 TechnoClass 家族（多核分派的常用判据）。
inline bool IsTechno(uint32_t vptr) {
    int id = re::FindClassByVTable(vptr);
    return id >= 0 && re::IsDerivedFrom(id, cls::kTechnoClass);
}

}  // namespace vtable
}  // namespace ra2
