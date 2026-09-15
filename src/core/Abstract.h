// Abstract.h -- 对象体系还原（AbstractClass / ObjectClass / TechnoClass ...）
//
// 类名的来源是可信的：全部取自 gamemd.exe 的 MSVC RTTI TypeDescriptor。
//   提取工具：tools/rtti.py  输出：db/rtti.json, db/classes.md
//   规模：988 条 TypeDescriptor（328 个非模板类 + 660 个模板实例）
//   校验：所有 TypeDescriptor 的 pVFTable 均为 0x007F9594、spare 均为 0，
//         这是 MSVC type_info 的标准布局，说明这些类名确实由编译器生成。
//
// 未确认部分（结构体字段偏移、虚表槽位顺序）一律显式标注，不做臆测填充。

#pragma once

#include <cstdint>

#include "core/Types.h"

namespace ra2 {

class AbstractTypeClass;

// ---------------------------------------------------------------------------
// 抽象基：AbstractClass
// ---------------------------------------------------------------------------
// RTTI 确认存在：AbstractClass、AbstractTypeClass
// （"AbstractTypeClass" 与 "AbstractClass" 都在 328 个类名清单里）
class AbstractClass {
public:
    virtual ~AbstractClass() = default;

    /// 对象的唯一 ID。二进制证据：0x005659F0 附近引用 "House->Fetch_ID()"，
    /// 0x0064DEA0 引用 "COORD:%d,%d  Facing:%d ... TarCom:%s"，说明
    /// 调试输出以 (ID / 坐标 / 朝向 / 任务) 四元组标识一个对象。
    virtual int Fetch_ID() const noexcept { return id_; }

    /// 该对象在逻辑层"是什么"（用于 INI 与网络包里的类型索引）。
    /// RTTI 中存在 AbstractTypeClass，与 AbstractClass 成对出现，
    /// 是 Westwood 典型的 "实例 / 类型" 双类设计。
    virtual const AbstractTypeClass* Fetch_Type() const noexcept { return nullptr; }

    /// 每逻辑帧更新。这是多核改造里最关键的虚函数：
    /// 逻辑帧内所有对象的 AI 都挂在这条链上。
    virtual void Update() {}

protected:
    int id_ = 0;
};

/// 类型对象基类。RTTI 确认存在：AbstractTypeClass、ObjectTypeClass、
/// TechnoTypeClass、UnitTypeClass、InfantryTypeClass、BuildingTypeClass、
/// AircraftTypeClass、TerrainTypeClass、OverlayTypeClass、SmudgeTypeClass、
/// WeaponTypeClass、WarheadTypeClass、SuperWeaponTypeClass、HouseTypeClass、
/// ScriptTypeClass、TeamTypeClass、TaskForceTypeClass、TriggerTypeClass、
/// TagTypeClass、ParticleSystemTypeClass、VoxelAnimTypeClass 等。
///
/// 这些名字与 rulesmd.ini / aimd.ini 的段名一一对应（二进制里能搜到
/// "AircraftType"、"InfantryType"、"BuildingType"、"TeamType"、"TaskForce" 等键），
/// 说明类型系统是数据驱动的：INI 段 -> 类型实例 -> 动态创建对象。
class AbstractTypeClass {
public:
    virtual ~AbstractTypeClass() = default;

    /// INI 中的名字，例如 "E1"、"MTNK"。
    const char* Name() const noexcept { return name_; }

protected:
    const char* name_ = "";
};

// ---------------------------------------------------------------------------
// 场上对象：ObjectClass
// ---------------------------------------------------------------------------
// RTTI 确认存在：ObjectClass、ObjectTypeClass、FoggedObjectClass
// （FoggedObjectClass 是战争迷雾下的"记忆"对象，说明迷雾与对象系统是耦合的）
class ObjectClass : public AbstractClass {
public:
    const AbstractTypeClass* Fetch_Type() const noexcept override = 0;

    /// 所在格子。
    CellStruct Get_Cell() const noexcept { return cell_; }

    /// 连续坐标（lepton）。
    CoordStruct Get_Coord() const noexcept { return coord_; }

    void Set_Coord(CoordStruct c) noexcept {
        coord_ = c;
        cell_ = c.Cell();
    }

protected:
    CellStruct cell_{};
    CoordStruct coord_{};
};

// ---------------------------------------------------------------------------
// 可行动对象：TechnoClass
// ---------------------------------------------------------------------------
// RTTI 确认存在：TechnoClass、TechnoTypeClass，以及大量
// `DynamicVectorClass<TechnoClass*>` / `VectorClass<TechnoClass*>` 模板实例，
// 说明引擎用动态数组集中管理所有 Techno —— 这一点对多核很关键：
// 一个连续的 Techno 池天然适合按区间切分给多个工作线程。
class TechnoClass : public ObjectClass {
public:
    /// 所属阵营。RTTI 确认存在 HouseClass、HouseTypeClass。
    class HouseClass* House = nullptr;

    /// 当前任务。RTTI 确认存在 MissionClass（"MissionClass" 在类名清单里），
    /// 且 0x0064DEA0 的调试串含 "Mission:%d"，说明任务是枚举值。
    int32_t Mission = 0;

    int32_t Health = 0;

    /// 朝向，0..255 对应 256 个方向。二进制证据：0x0064DEA0 的
    /// "Facing:%d  Facing2:%d" 说明单位保存了当前朝向与目标朝向两个值。
    uint8_t Facing = 0;
    uint8_t FacingTarget = 0;

    void Update() override;

protected:
    /// 子类实现各自的每帧行为。
    virtual void Update_Movement() {}
    virtual void Update_Weapon() {}
    virtual void Update_AI() {}
};

// ---------------------------------------------------------------------------
// 可移动对象：FootClass 及其子类
// ---------------------------------------------------------------------------
// RTTI 确认存在：FootClass、UnitClass、InfantryClass、AircraftClass、
// BuildingClass、UnitTypeClass、InfantryTypeClass、AircraftTypeClass、
// BuildingTypeClass。
//
// 移动并不是写死在 FootClass 里的：RTTI 里有一整套
// LocomotionClass 派生类 ——
//   DriveLocomotionClass / WalkLocomotionClass / FlyLocomotionClass /
//   JumpjetLocomotionClass / HoverLocomotionClass / ShipLocomotionClass /
//   MechLocomotionClass / RocketLocomotionClass / TeleportLocomotionClass /
//   TunnelLocomotionClass / DropPodLocomotionClass
// 以及接口 ILocomotion、IPiggyback、IFlyControl。
// 这是标准的" locomotor 策略模式"：移动算法被外挂成对象，
// 每种移动方式有自己的状态机，天然是独立、可并行的计算单元。
class FootClass : public TechnoClass {
public:
    class LocomotionClass* Locomotor = nullptr;

    /// 当前路径。RTTI 确认存在 WaypointPathClass，
    /// 且二进制里有 "Regular findpath failure" / "Hierarchical findpath failure"
    /// 两条日志（同一函数 0x0042C900 引用），说明寻路有两级：
    /// 粗粒度分层寻路 + 细粒度常规寻路。
    class WaypointPathClass* Path = nullptr;

    uint8_t Speed = 0;

protected:
    void Update_Movement() override;
};

class UnitClass : public FootClass {};
class InfantryClass : public FootClass {};
class AircraftClass : public FootClass {};

/// 建筑。RTTI 确认存在 BuildingClass、BuildingTypeClass、BuildingLightClass、
/// FactoryClass（出兵工厂）、PowerClass（电力）、CaptureManagerClass（被占领）、
/// SlaveManagerClass、SpawnManagerClass、ParasiteClass、TemporalClass 等与建筑
/// 强相关的附属类。
class BuildingClass : public TechnoClass {
protected:
    void Update_AI() override;
};

/// 地形与覆盖物。RTTI 确认存在 TerrainClass、OverlayClass、SmudgeClass、
/// TiberiumClass、VeinholeMonsterClass、TubeClass、IsometricTileClass。
class TerrainClass : public ObjectClass {
    const AbstractTypeClass* Fetch_Type() const noexcept override { return nullptr; }
};

}  // namespace ra2
