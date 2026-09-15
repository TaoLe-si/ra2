// Cell.h -- CellClass 还原
//
// RTTI 确认存在：CellClass，以及
//   VectorClass<CellClass*> / DynamicVectorClass<CellClass*>
//   DiscreteDistributionClass<CellClass>
// 说明格子也是被容器集中管理的对象，而不是裸 POD 数组。
//
// 地图在内存中是一维 Cells 数组 + 一个二维索引（等距地图的可见区域是矩形，
// 但逻辑地图是菱形，用 width/height + 可见矩形来描述）。
//
// TODO(逆向)：CellClass 的真实大小与字段偏移尚待确认。
//   已定位的抓手：
//     * 0x005659F0  MapClass::Init_Clear（引用 "MapClass::Init_Clear done"）
//       —— 从这里能看到地图缓冲区的分配与清零，直接给出 sizeof(CellClass)。
//     * 0x00652DE0  RadarClass::Init_Clear / 0x00652E90 RadarClass::Init_For_House
//       —— 雷达按格子着色，其循环步进也能反推出格子数组布局。
//   确认后把下面 static_assert 与偏移注释补齐。

#pragma once

#include <cstdint>

#include "core/Types.h"

namespace ra2 {

class ObjectClass;
class TechnoClass;

/// 地形类型位。二进制证据：INI 键 "TerrainType"、"OverlayType"、"SmudgeType"、
/// "IsAnimatedTiberium"、"TiberiumSpawnType"、"TiberiumSpreadRadius"
/// 均出现在 db/strings.json 中，说明地形分类是数据驱动枚举。
enum class LandType : uint8_t {
    Clear = 0,
    Road = 1,
    Water = 2,
    Rock = 3,
    Wall = 4,
    Tiberium = 5,
    Beach = 6,
    Rough = 7,
    Ice = 8,
};

/// 一个地图格子。
///
/// 多核相关性：这是全局最热的共享数据结构。几乎所有子系统（寻路、视野、
/// 建筑占位、占领、单位避让）每帧都要读写它。任何并行化方案都必须先解决
/// CellClass 的写冲突——详见 docs/multicore-plan.md。
class CellClass {
public:
    CellStruct Position() const noexcept { return position_; }

    LandType Land() const noexcept { return land_; }
    void Set_Land(LandType t) noexcept { land_ = t; }

    /// 高度（0..13），影响视线和移动。
    uint8_t Level() const noexcept { return level_; }

    /// 该格是否可以通行。
    /// 注意：真正的可通行判定还要叠加"已被其它单位占用"，
    /// 后者是运行时状态（occupier_），不是地形属性。
    bool Is_Clear_To_Move(LandType /*for_type*/) const noexcept {
        return land_ != LandType::Rock && land_ != LandType::Wall;
    }

    ObjectClass* Occupier() const noexcept { return occupier_; }
    void Set_Occupier(ObjectClass* o) noexcept { occupier_ = o; }

    /// 战争迷雾。RTTI 中存在 FoggedObjectClass，说明迷雾是逐格记录的状态；
    /// 视野重算（LOS）是另一个典型的热点，且是只读计算，很适合并行。
    bool Is_Visible(int /*house_index*/) const noexcept { return visible_mask_ != 0; }
    void Set_Visible(int house_index, bool v) noexcept {
        if (v) {
            visible_mask_ |= (1u << house_index);
        } else {
            visible_mask_ &= ~(1u << house_index);
        }
    }

    /// 每帧的重算钩子。原引擎把这部分逻辑分散在各子系统的 Update 里，
    /// 这里显式成一个函数，方便后续按格子区间并行。
    void Update() {}

private:
    CellStruct position_{};
    LandType land_ = LandType::Clear;
    uint8_t level_ = 0;
    ObjectClass* occupier_ = nullptr;
    uint32_t visible_mask_ = 0;
};

}  // namespace ra2
