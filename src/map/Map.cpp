// Map.cpp
//
// 对应原始 MapClass::Init_Clear（0x005659F0，引用 "MapClass::Init_Clear done"）
// 与地图生成相关代码（MapGen.cpp 区间 0x00595680-0x0059A66C）。

#include "map/Map.h"

namespace ra2 {

void MapClass::Init_Clear(int width, int height) {
    width_ = width;
    height_ = height;
    cells_.assign(static_cast<size_t>(width) * static_cast<size_t>(height), CellClass{});
    visible_rect_ = RectangleStruct{0, 0, width, height};
}

void MapClass::Update() {
    // 逐格更新。多核提示：这里的循环可以直接用 TaskSystem 按区间切分，
    // 但要注意"格子 i 的更新会不会读到格子 i±1 在本帧刚写的值"——
    // 如果会，就必须做双缓冲，否则并行结果会与串行不一致。
    for (auto& c : cells_) {
        c.Update();
    }
}

}  // namespace ra2
