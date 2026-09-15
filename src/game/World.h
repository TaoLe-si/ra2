// World.h -- P3 逻辑层：战场上的活对象（单位实例）+ 选择 + 命令
//
// 与 MapFile 的分工：
//   MapFile 里的 MapObject 是**存档里的死数据**（位置、血量、朝向）；
//   World 里的 Object 是**活的对象**：位置是浮点（可以在格之间）、
//   有任务、会转向会移动、能被选中。Load 时把 MapObject 灌进来。
//
// 【为什么位置用浮点】原版一格内部是 256 个 leptons，单位移动是连续的，
//   只在渲染时才归到格。用整数格会导致单位"一格一跳"。
//
// 【逻辑帧】Update(dt) 只推进**一个逻辑帧**（1/15 秒）。外面负责按
//   固定步长调用它，锁步联机要求逻辑帧严格定步，不能跟着渲染帧走。
//
// 【快捷键对应】见 GameShell::On_Key_Down。这里只提供能力，
//   哪个键做什么由外壳决定 —— 原版热键表是从手册和实测整理出来的。

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "map/MapFile.h"

namespace ra2 {

/// 任务。原版 MissionClass 的 Mission 枚举，这里只留地图里真会出现的
/// 和玩家能下的那几个。
enum class Mission {
    None,       ///< 无（默认待命）
    Sleep,      ///< 中立物件：不动不反应
    Sticky,     ///< 钉在原地（中立步兵用）
    Guard,      ///< 警戒：原地不动，敌人进射程就打
    AreaGuard,  ///< 区域警戒：可以小范围追击
    Move,       ///< 移动到目标点
    Attack,     ///< 攻击指定目标
    AttackMove, ///< 移动攻击（Ctrl+Shift）
    Scatter,    ///< 散开
    Deploy,     ///< 展开（基地车 / 美国大兵）
    Stop,       ///< 停止
};

const char* Mission_Name(Mission m);

/// 战场上的一个对象。
struct Object {
    int id = 0;
    MapObjectKind kind = MapObjectKind::Terrain;
    std::string type;        ///< rules.ini 里的名字
    int house = -1;          ///< 阵营下标（对应 World::Houses()）
    float x = 0.0f, y = 0.0f;///< 逻辑格坐标，浮点（移动中在格之间）
    int facing = 0;          ///< 朝向 0..255，0 = 正上方（北），顺时针
    int hp = 256;
    int hp_max = 256;
    float speed = 0.0f;      ///< 格/秒。0 = 不能动（建筑、装饰、中立）
    float turn_rate = 0.0f;  ///< 朝向变化速度（单位/秒）

    Mission mission = Mission::None;
    float dest_x = 0.0f, dest_y = 0.0f;
    bool has_dest = false;
    int target = -1;         ///< 攻击目标 id，-1 = 无

    bool selectable = false; ///< 能不能被玩家选中（装饰物不能）
    bool selected = false;
    bool is_mine = false;    ///< 是不是玩家的（用来决定默认能不能选）

    /// 是不是"活物"（有脑子、能接收命令）。装饰物和树不算。
    bool Is_Techno() const noexcept {
        return kind == MapObjectKind::Unit || kind == MapObjectKind::Infantry ||
               kind == MapObjectKind::Aircraft || kind == MapObjectKind::Building;
    }
};

/// 一个编队（Ctrl+数字建、数字选）。
using Team = std::vector<int>;

class World {
public:
    /// 从地图灌对象进来。houses 来自地图的 [Houses]。
    bool Build(const MapFile& map);

    const std::vector<Object>& Objects() const noexcept { return objects_; }
    std::vector<Object>& Objects() noexcept { return objects_; }
    const std::vector<std::string>& Houses() const noexcept { return houses_; }

    int Count() const noexcept { return static_cast<int>(objects_.size()); }
    int Selected_Count() const noexcept { return selected_count_; }
    Object* Find(int id);

    /// 推进一个逻辑帧（1/15 秒）。
    void Update(float dt);

    // ---- 选择 ----

    /// 点选：返回离 (x,y) 最近且在拾取半径内的对象 id，-1 = 没点到。
    /// 只挑 selectable 的。半径 0.75 格 —— 比半格大一点，手抖也点得到。
    int Pick_At(float x, float y) const;

    void Select_None();
    /// 框选。add = Shift 追加。返回选中个数。
    int Select_In_Rect(float x0, float y0, float x1, float y1, bool add);
    /// 点选一个（add = Shift 追加）。
    int Select_At(float x, float y, bool add);
    /// T：选中与当前选中同类型的全部单位。
    int Select_Same_Type();
    /// P：全选有战斗力的（原版是"集结所有有攻击力的部队"）。
    int Select_All_Combat();
    /// U：按生命值排序选 / Y：按等级排序选 —— 简化成"选血最少的那个"。
    int Select_By_Health(bool lowest);

    std::vector<int> Selected_Ids() const;

    // ---- 命令 ----

    /// 移动命令：给所有选中单位下目标点。
    /// 原版会做队形（同时到达），这里先直线走，队形等寻路接进来再做。
    int Order_Move(float x, float y);
    int Order_Attack_Move(float x, float y);
    int Order_Attack(int target_id);
    int Order_Stop();
    int Order_Guard();
    int Order_Scatter();
    int Order_Deploy();

    // ---- 编队 ----

    void Team_Set(int slot);              ///< Ctrl+数字
    void Team_Add(int slot);              ///< Shift+数字
    int Team_Select(int slot);            ///< 数字
    bool Team_Valid(int slot) const;

    /// 请求把相机挪到某个格（H 回基地 / 空格去事件 / F1-F4 书签用）。
    void Request_Camera(float x, float y) {
        camera_x_ = x;
        camera_y_ = y;
        have_camera_order_ = true;
    }
    /// 外壳每帧取一次。返回 false 表示这次没有需要相机做的事。
    bool Consume_Camera_Order(float* x, float* y);

    /// 出生点（waypoint 0..7）对应的格。
    bool Spawn_Cell(int index, float* x, float* y) const;

    /// 玩家的第一个建筑的位置，当作"主基地"。没有就退化到 0 号出生点。
    bool Home_Cell(float* x, float* y) const;

private:
    void Move_Toward(Object& o, float dt);

    std::vector<Object> objects_;
    std::vector<MapWaypoint> waypoints_;
    std::vector<std::string> houses_;
    std::vector<Team> teams_{std::vector<Team>(10)};
    int selected_count_ = 0;
    int next_id_ = 1;
    float camera_x_ = 0.0f, camera_y_ = 0.0f;
    bool have_camera_order_ = false;
    int player_house_ = -1;
};

}  // namespace ra2
