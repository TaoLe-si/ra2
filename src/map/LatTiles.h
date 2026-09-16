// LatTiles.h -- 剧场 LAT（Lookup Adjacent Tile）后处理
//
// gamemd 读 IsoMapPack5 后会跑 LAT：把 Green/Sand/Rough/Pave 等 LAT 集
// 跟 Clear 邻接的格子换成 ClearTo*Lat 过渡瓦（ModEnc LAT_system；
// CNCMaps Operations.FixTiles 同算法）。不跑的话 Clear↔Sand 边界是硬菱形，
// Arena 路面/人行道看起来像锯齿撕碎。
//
// 邻接四向（等距菱形角）位权：TopRight=1 BottomRight=2 BottomLeft=4 TopLeft=8。
// 本工程逻辑格 (cx,cy) 对应：
//   TopRight=(cx,cy-1) BottomRight=(cx+1,cy) BottomLeft=(cx,cy+1) TopLeft=(cx-1,cy)

#pragma once

#include "map/MapFile.h"
#include "map/TheaterFile.h"

namespace ra2 {

/// 就地改写 map 的 cells_（tile/sub）。theater 必须已 Load。
/// 返回被改写的格数。
int Apply_Lat(MapFile* map, const TheaterFile& theater);

}  // namespace ra2
