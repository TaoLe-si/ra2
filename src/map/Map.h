// Map.h -- MapClass 还原
//
// RTTI 确认存在：MapClass、MapRegionClass、MapSeedClass。
// 已定位函数：
//   0x005659F0  MapClass::Init_Clear   （引用 "MapClass::Init_Clear done"）
//   0x004B6C30  地图生成相关（Dropship.cpp 区间内，2962 指令）
//   0x00599650  MapGen.cpp 区间（1157 指令）
//
// 地图是整个引擎的共享状态中心，也是多核改造里最难啃的部分：
// 它同时被寻路（读）、单位移动（写占用）、视野（写可见性）、
// 渲染（读）访问。

#pragma once

#include <cstdint>
#include <vector>

#include "core/Types.h"
#include "map/Cell.h"

namespace ra2 {

/// 可见矩形（屏幕上能看到的地图范围）。
/// 等距地图里逻辑地图大于可见区域，用 X/Y/Width/Height 描述窗口。
struct RectangleStruct {
    int32_t X = 0;
    int32_t Y = 0;
    int32_t Width = 0;
    int32_t Height = 0;
};

class MapClass {
public:
    void Init_Clear(int width, int height);

    int Width() const noexcept { return width_; }
    int Height() const noexcept { return height_; }

    /// 一维索引。<---- 多核提示：所有并行遍历都基于这个连续数组做区间切分。
    int Index(CellStruct c) const noexcept { return c.Y * width_ + c.X; }

    bool In_Radar(CellStruct c) const noexcept {
        return c.X >= 0 && c.Y >= 0 && c.X < width_ && c.Y < height_;
    }

    CellClass* Cell_At(CellStruct c) noexcept {
        if (!In_Radar(c)) {
            return nullptr;
        }
        return &cells_[static_cast<size_t>(Index(c))];
    }

    const CellClass* Cell_At(CellStruct c) const noexcept {
        if (!In_Radar(c)) {
            return nullptr;
        }
        return &cells_[static_cast<size_t>(Index(c))];
    }

    /// 直接暴露底层连续存储，便于并行算法分块。
    CellClass* Cell_Data() noexcept { return cells_.data(); }
    const CellClass* Cell_Data() const noexcept { return cells_.data(); }
    size_t Cell_Count() const noexcept { return cells_.size(); }

    /// 每帧的地图更新（矿石生长、动画等）。
    /// 原本散落在多个子系统里，这里集中成一个入口。
    void Update();

private:
    int32_t width_ = 0;
    int32_t height_ = 0;
    std::vector<CellClass> cells_;
    RectangleStruct visible_rect_{};
};

}  // namespace ra2
