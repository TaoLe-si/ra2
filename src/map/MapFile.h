// MapFile.h -- .map / .mmx / .yro 地图文件解析
//
// 【实测链路】2026-09-15
//   .mmx / .yro 不是裸地图，而是 **加密 MIX**（flags=0x00030000），
//   里面装一个 <地图名>.map 和一个小 INI。所以先当 MIX 剥一层。
//   .map 本体是"看起来像 INI 的二进制容器"：文本段真是 INI，
//   但 [IsoMapPack5] / [OverlayPack] / [OverlayDataPack] 是 base64 包着的
//   **分块 LZO** 流（见 io/Lzo1x.h）。
//
// 【IsoMapPack5 的坐标】对齐 ModEnc / CNCMaps（这是路面锯齿的根因）：
//   解压后是 N 条 11 字节记录 + 4 字节全 0 结尾，
//     int16 X, int16 Y, int32 TileIndex, uint8 SubTile, uint8 Level, uint8 Ice
//   单元数 = (2*W - 1) * H（Arena 80x80 → 12720，奇偶 X+Y **全部**有效）。
//   显示坐标（CNCMaps TileLayer）：
//     dx = X - Y + W - 1          // 0 .. 2W-2
//     dy = X + Y - W - 1          // 偶数列偶数 dy，奇数列奇数 dy
//   存盘下标：cells[dx + (dy/2) * (2W-1)]，与 CNCMaps tiles[dx, dy/2] 一致。
//   IsoCell.cx = dx，IsoCell.cy = dy/2；完整 dy = 2*cy + (cx & 1)。
//
// 【屏幕落点】TmpRenderer / GetTilePixelCenter：
//   sx = dx * 30
//   sy = (dy - Level) * 15
//   画布约 W*60 × H*30。旧式 sx=30*(cx-cy) 只铺了半数格，菱形之间留空 → 棋盘锯齿。
//
// 【对象段】同样用 (X,Y)→(dx, dy/2)；越界用 Iso 宽高判断。

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
    int cx = 0, cy = 0;   ///< 显示格：cx=dx，cy=dy/2（见 IsoCell）
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

/// 一格地形（CNCMaps 显示格，不是旧的 W×H 折叠格）。
struct IsoCell {
    int cx = 0;         ///< dx：0 .. 2*W-2
    int cy = 0;         ///< dy/2：0 .. H-1
    int32_t tile = -1;  ///< 剧场瓦片全局下标；0xFFFF / -1 = 空
    uint8_t sub = 0;    ///< 瓦片内子格（多格模板用）
    uint8_t level = 0;  ///< 高度级
    uint8_t ice = 0;    ///< 冰面生长标记（TS 雪地用）
    /// 完整显示 dy（= 2*cy + (cx&1)）。
    int Dy() const noexcept { return cy * 2 + (cx & 1); }
};

/// 每级高度抬升的像素数。
///
/// TacticalClass::CoordsToClient（gamemd 0x006D1F10）：
///   screen_x = 30 * (X - Y) / 256
///   screen_y = 15 * (X + Y) / 256 - f(Z)
/// X/Y 是 lepton，1 格 = 256。一格在屏幕 Y 上走 15px，所以高度一级
/// 对齐"半格立面"就是 15px。原先写 12 是观感估的，悬崖会偏矮。
constexpr int kLevelHeightPx = 15;

/// 单条 TEvent（gamemd TEvent::Parse @0x0071F4E0）。
/// Events CSV：count, type, kind, data... 重复 count 次；
/// kind 0 → int @+0x34；kind 1 → 类型名 → AbstractType* @+0x30；
/// kind 2 → int @+0x34 + House 名 @+0x38。
struct MapTriggerEvent {
    int type = 0;       ///< TEvent+0x2c
    int kind = 0;       ///< 0/1/2
    int param = 0;      ///< +0x34
    std::string type_name;
    std::string house_name;
};

/// 地图触发器（[Triggers]/[Events]/[Actions]/[Tags]）。
struct MapTrigger {
    std::string id;
    std::string house;
    std::string name;
    std::string events_raw;   ///< [Events] 原串
    std::string actions_raw;  ///< [Actions] 原串
    std::vector<MapTriggerEvent> events;
    int repeat = 0;           ///< [Tags] 第一字段
    bool fired = false;       ///< Trigger+0x30：已弹簧闩（0x726720）
    bool enabled = true;      ///< Trigger+0x44：初值 1（@0x725FCA）；54→0 / 53→1
    /// Trigger+0x34 CDTimer：Elapsed Time(13) 用。start=-1 未启动。
    int timer_start = -1;
    int timer_duration = 0;   ///< 帧；INI 秒 *15（@0x0072642E）
    uint32_t events_done = 0; ///< Trigger+0x40 已满足事件位
};

/// BaseNode（[Base] 段）：电脑 AI 的建造蓝图。
/// 一行："x,y,Building,Refinery,Owner,Weapon,WeaponCount"。
/// 原版 BaseNodeClass @0x006DF100 + BaseClass @0x0069E8A0；
/// 这里只把数据读进来，AI 真正的"按 BaseNode 出建筑"留给 HouseClass::AI 接。
struct MapBaseNode {
    int cx = 0, cy = 0;
    std::string building;
    std::string refinery;
    std::string owner_house;
    std::string weapon;
    int weapon_count = 0;
};

/// TaskForce 一条：`N=count,Type`（gamemd TaskForce+0xA4/+0xA8 stride 8）。
struct MapTaskForceEntry {
    int count = 0;
    std::string type;
};

/// TeamType（gamemd TeamTypeClass::Read_INI @0x6F1090）。
/// TAction+0x30 指向此类；Create@0x6F09C0 / Reinforce@0x65D8E0 读 House/WP/TF。
struct MapTeamType {
    std::string id;                 ///< AbstractType ID（段名 / +0x24）
    std::string name;               ///< Name=（+0x64）
    std::string house;              ///< House=（→ +0xC4 / 特殊 +0xC8）
    std::string taskforce_id;       ///< TaskForce=（→ +0xE4）
    std::string script_id;          ///< Script=（→ +0xE0）
    int waypoint = -1;              ///< Waypoint= 字母解码（+0xD4；0x763690）
    int transport_waypoint = -1;    ///< TransportWaypoint=（+0xD8）
    bool droppod = false;           ///< Droppod=（+0xB0）
    bool reinforce = false;         ///< Reinforce=（+0xAB）
    std::vector<MapTaskForceEntry> members;  ///< 已解析的 TaskForce 成员
};

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
    /// Iso 网格宽 = 2*W-1（与 CNCMaps TileLayer / ModEnc 一致）。
    int Iso_Width() const noexcept {
        return rect_[2] > 0 ? rect_[2] * 2 - 1 : 0;
    }
    int Iso_Height() const noexcept { return rect_[3]; }

    /// [Map] LocalSize= 的四个数（可见区域）。没写就用全图。
    int Local_X() const noexcept { return local_[0]; }
    int Local_Y() const noexcept { return local_[1]; }
    int Local_Width() const noexcept { return local_[2]; }
    int Local_Height() const noexcept { return local_[3]; }

    const std::vector<IsoCell>& Cells() const noexcept { return cells_; }
    /// LAT 后处理要改写 tile/sub（见 map/LatTiles.h）。
    std::vector<IsoCell>& Mutable_Cells() noexcept { return cells_; }

    /// 地图里出现过的最大瓦片下标（用来挑剧场 INI）。没有有效格返回 -1。
    int Max_Tile_Index() const noexcept { return max_tile_; }

    /// OverlayPack：每格一个字节（0xFF = 无）。可能为空（地图没写这段）。
    const std::vector<uint8_t>& Overlay() const noexcept { return overlay_; }
    const std::vector<uint8_t>& Overlay_Data() const noexcept { return overlay_data_; }
    std::vector<uint8_t>& Mutable_Overlay() noexcept { return overlay_; }
    std::vector<uint8_t>& Mutable_Overlay_Data() noexcept { return overlay_data_; }

    /// 等距格 (cx,cy)=(dx,dy/2) 上的 Overlay / OverlayData 字节；无则 0xFF。
    uint8_t Overlay_At(int cx, int cy) const;
    uint8_t Overlay_Data_At(int cx, int cy) const;
    void Set_Overlay_At(int cx, int cy, uint8_t v);
    void Set_Overlay_Data_At(int cx, int cy, uint8_t v);

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
    /// [Triggers]/[Events]/[Actions]/[Tags]（动作表尚未全部逆完，先存数据）。
    const std::vector<MapTrigger>& Triggers() const noexcept { return triggers_; }
    /// [TeamTypes] + 关联 [TaskForces]（TAction 4/7）。
    const std::vector<MapTeamType>& Team_Types() const noexcept {
        return team_types_;
    }
    /// [Base] 电脑 AI 蓝图节点（电脑据此盖建筑 + 摆防御）。
    const std::vector<MapBaseNode>& Base_Nodes() const noexcept { return base_nodes_; }

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
    /// 解析 [Terrain]/[Units]/[Infantry]/[Structures]/[Aircraft]/[Waypoints]/[Houses]/
    /// 以及 [Triggers]/[Events]/[Actions]/[Tags] / [TeamTypes]/[TaskForces]。
    void Parse_Objects();
    void Parse_Triggers();
    void Parse_Team_Types();
    void Parse_Base_Nodes();

    std::string name_;
    std::string theater_;
    int rect_[4] = {0, 0, 0, 0};
    int local_[4] = {0, 0, 0, 0};
    std::vector<IsoCell> cells_;
    std::vector<MapObject> objects_;
    int objects_dropped_ = 0;
    std::vector<MapWaypoint> waypoints_;
    std::vector<std::string> houses_;
    std::vector<MapTrigger> triggers_;
    std::vector<MapTeamType> team_types_;
    std::vector<MapBaseNode> base_nodes_;
    int max_tile_ = -1;
    std::vector<uint8_t> overlay_;
    std::vector<uint8_t> overlay_data_;
    std::vector<std::pair<std::string, std::string>> raw_sections_;

    size_t packed_bytes_ = 0;
    size_t unpacked_bytes_ = 0;
    size_t stored_ = 0;
};

}  // namespace ra2
