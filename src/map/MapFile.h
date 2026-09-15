// MapFile.h -- .map / .mmx / .yro 地图文件解析
//
// 【实测链路】2026-09-15
//   .mmx / .yro 不是裸地图，而是 **加密 MIX**（flags=0x00030000），
//   里面装一个 <地图名>.map 和一个小 INI。所以先当 MIX 剥一层。
//   .map 本体是"看起来像 INI 的二进制容器"：文本段真是 INI，
//   但 [IsoMapPack5] / [OverlayPack] / [OverlayDataPack] 是 base64 包着的
//   **分块 LZO** 流（见 io/Lzo1x.h）。
//
// 【IsoMapPack5 的坐标】最容易搞错的地方，实测结论：
//   解压后是 N 条 11 字节记录 + 4 字节全 0 结尾，
//     int16 X, int16 Y, int32 TileIndex, uint8 SubTile, uint8 Level, uint8 Ice
//   X/Y 取值范围都是 1..(W+H-1)，但**只有 X+Y 为奇数的才是真单元**：
//     ARENA.map 80x80 -> 记录 12720 条，其中 X+Y 奇数恰好 6400 = 80*80 ✓
//   真单元 (X,Y) 与逻辑格 (cx,cy) 的换算（实测第一格是 (80,1) -> (0,0)）：
//     X = W + cy - cx
//     Y = cx + cy + 1
//   反解：cx = (Y - 1 - (X - W)) / 2，cy = (X + Y - 1 - W) / 2
//
// 【哪个是 cx 哪个是 cy —— 别再翻案】IsoMapPack5 自己分不出来：
// 两种指派都给出 [0,W)x[0,H) 内的合法格，只是整张图左右镜像。
// 真正的判据是**对象段**（[Units]/[Structures]/[Terrain]）用的是同一个 (X,Y) 帧：
//   * 按 cx=(X+Y-1-W)/2 算：53 张官方地图有 30 张出现对象越界，共 2141 个，
//     且只有 82.6% 落在 LocalSize 可玩矩形内。
//   * 按 cx=(Y-1-(X-W))/2 算：只有 3 张地图共 24 个越界，
//     落在可玩矩形内的比例 93.4%。
// 差了整整两个数量级，所以取后者。
// 另一个独立印证：Arena 一个路口的 4 个红绿灯 (81,47)(81,50)(85,50)(85,47)
// 在本帧下算出 (22,23)(24,25)(22,27)(20,25)，是以 (22,25) 为中心的**正菱形**；
// 反过来算会得到不对称的四点。
//
// 【取整用下取整】对象里 X+Y 的奇偶各占一半（只有奇数才是整格，
// 偶数是半格偏移）。下取整才对：见上面红绿灯那组，四舍五入会把菱形压歪。
//
// 【屏幕落点】等距菱形 60x30，相邻格错半格：
//   sx = 30 * (cx - cy)
//   sy = 15 * (cx + cy) - Level * kLevelHeightPx
//   Level 是实测出来的高度：相邻格 |ΔLevel|<=1 的比例 12393/12640 = 98%，
//   说明它确实是高度而不是噪声（同一个字节当 SubTile 解释只有 74%）。

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ra2 {

/// 地图里的一个"摆件"：车辆 / 步兵 / 建筑 / 飞机 / 地形装饰。
///
/// 字段是**照抄真实地图**来的，不是照抄文档（tools/mapobjects.py 拿的事实）：
///   [Units]      Owner,Type,HP,X,Y,Facing,Mission,Tag,Veterancy,Group,OnBridge,Follows,?,?
///   [Infantry]   Owner,Type,HP,X,Y,SubCell,Mission,Facing,Tag,Veterancy,Group,OnBridge,?,?
///   [Structures] Owner,Type,HP,X,Y,Rotation,Tag,?,?,?,?,?,?,?,?,?,?
///   [Aircraft]   同 [Units]
///   [Terrain]    键就是坐标（X*1000+Y），值只有一个名字
/// 四个段**都是 14 个字段**（逗号 13 个），只有 Structures 是 17 个（逗号 16 个）。
/// 玩家自己的建筑在 [Structures]，中立的树/灯在 [Terrain]。
enum class MapObjectKind {
    Terrain,     ///< [Terrain]：树、路灯、交通灯这类不可交互装饰
    Unit,        ///< [Units]：车辆
    Infantry,    ///< [Infantry]：步兵（多一个 SubCell）
    Building,    ///< [Structures]：建筑
    Aircraft,    ///< [Aircraft]：飞机
};

struct MapObject {
    MapObjectKind kind = MapObjectKind::Terrain;
    std::string owner;    ///< [Houses] 里的阵营名，如 Russians / Neutral
    std::string type;     ///< rules.ini 里的对象名，如 MTNK / CABUNK01
    int hp = 256;         ///< 出场血量，256 = 满血
    int cx = 0, cy = 0;   ///< 逻辑格（已换算过，不是文件里的 X,Y）
    int facing = 0;       ///< 朝向 0..255，256 分度
    int subcell = 0;      ///< 步兵在格内的子位（0..4），其它恒 0
    std::string mission;  ///< Sleep / Sticky / Guard / Area Guard ...
    int group = -1;       ///< 编队号，-1 = 无
};

/// 路径点。`0..7` 是 8 个出生点，其余是触发器和脚本用的。
struct MapWaypoint {
    int index = 0;
    int cx = 0, cy = 0;
};

/// 一格地形。
struct IsoCell {
    int cx = 0;         ///< 逻辑列 0..W-1
    int cy = 0;         ///< 逻辑行 0..H-1
    int32_t tile = -1;  ///< 剧场瓦片全局下标；0xFFFF / -1 = 空
    uint8_t sub = 0;    ///< 瓦片内子格（多格模板用）
    uint8_t level = 0;  ///< 高度级
    uint8_t ice = 0;    ///< 冰面生长标记（TS 雪地用）
};

/// 每级高度抬升的像素数。RA2 的悬崖是 4 级，观感上约 48px。
constexpr int kLevelHeightPx = 12;

class MapFile {
public:
    /// 从磁盘读。自动判断是 MIX 包还是裸 .map。
    bool Load_Path(const char* path, std::string* err = nullptr);

    /// 解析 .map 正文。
    bool Load_Data(const uint8_t* data, size_t size, std::string* err = nullptr);

    const std::string& Name() const noexcept { return name_; }
    const std::string& Theater() const noexcept { return theater_; }

    /// [Map] Size= 的四个数（x, y, width, height）。
    int Rect_X() const noexcept { return rect_[0]; }
    int Rect_Y() const noexcept { return rect_[1]; }
    int Width() const noexcept { return rect_[2]; }
    int Height() const noexcept { return rect_[3]; }

    /// [Map] LocalSize= 的四个数（可见区域）。没写就用全图。
    int Local_X() const noexcept { return local_[0]; }
    int Local_Y() const noexcept { return local_[1]; }
    int Local_Width() const noexcept { return local_[2]; }
    int Local_Height() const noexcept { return local_[3]; }

    const std::vector<IsoCell>& Cells() const noexcept { return cells_; }

    /// 地图里出现过的最大瓦片下标（用来挑剧场 INI）。没有有效格返回 -1。
    int Max_Tile_Index() const noexcept { return max_tile_; }

    /// OverlayPack：每格一个字节（0xFF = 无）。可能为空（地图没写这段）。
    const std::vector<uint8_t>& Overlay() const noexcept { return overlay_; }
    const std::vector<uint8_t>& Overlay_Data() const noexcept { return overlay_data_; }

    /// 地图里摆好的对象（车辆/步兵/建筑/飞机/装饰）。Load 时一并解析好。
    const std::vector<MapObject>& Objects() const noexcept { return objects_; }
    /// 因为算出界而被丢掉的对象条数。**坐标帧对不对就看这个** ——
    /// 正确帧下全库 53 张官方地图只有 3 张有零星几个（24 个 / 23366 个），
    /// 帧反了会变成 30 张共 2141 个。
    int Objects_Dropped() const noexcept { return objects_dropped_; }
    /// 路径点，按 [Waypoints] 里的编号升序。
    const std::vector<MapWaypoint>& Waypoints() const noexcept { return waypoints_; }
    /// [Houses] 的编号 -> 阵营名。编号就是对象段里 Owner 出现之前那个东西的索引。
    const std::vector<std::string>& Houses() const noexcept { return houses_; }

    /// 原始文本段（[Terrain] / [Units] / [Infantry] / [Structures] 等）。
    /// 目前只原样留着，等对象系统接进来再解析。
    std::string Raw_Section(const char* name) const;

    /// 解码统计，用来在离屏自检里证明"确实解开了"。
    size_t Packed_Bytes() const noexcept { return packed_bytes_; }
    size_t Unpacked_Bytes() const noexcept { return unpacked_bytes_; }

    /// 记录里**实际给出**的单元数。小于 Width()*Height() 说明地图包被裁过
    /// （省掉了高度 0 的 Clear 瓦片），缺的由 Clear01/level 0 补齐。
    size_t Stored_Cells() const noexcept { return stored_; }

private:
    bool Decode_Iso_Pack(const std::string& b64, std::string* err);
    /// 解析 [Terrain]/[Units]/[Infantry]/[Structures]/[Aircraft]/[Waypoints]/[Houses]。
    void Parse_Objects();

    std::string name_;
    std::string theater_;
    int rect_[4] = {0, 0, 0, 0};
    int local_[4] = {0, 0, 0, 0};
    std::vector<IsoCell> cells_;
    std::vector<MapObject> objects_;
    int objects_dropped_ = 0;
    std::vector<MapWaypoint> waypoints_;
    std::vector<std::string> houses_;
    int max_tile_ = -1;
    std::vector<uint8_t> overlay_;
    std::vector<uint8_t> overlay_data_;
    std::vector<std::pair<std::string, std::string>> raw_sections_;

    size_t packed_bytes_ = 0;
    size_t unpacked_bytes_ = 0;
    size_t stored_ = 0;
};

}  // namespace ra2
