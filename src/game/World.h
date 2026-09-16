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
#include "ai/PathFinder.h"
#include "map/Map.h"

namespace ra2 {

class UnitModelDB;

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
    Harvest,    ///< 采矿（矿车自动：采满回矿厂）
    Stop,       ///< 停止
    Hunt,       ///< MissionType=15 @0x816cac；All_To_Hunt @0x00501400 Assign 0xF
};

/// 对局结局。Flag_To_Win @0x004FC9E0（house+0x1f7）/ Flag_To_Lose @0x004FCBD0（+0x1f8）。
enum class MatchOutcome {
    Playing = 0,
    Won,
    Lost,
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
    int sight = 0;           ///< Sight= 开雾半径（格）

    // 战斗（从 Primary 武器灌入，对齐 rulesmd Damage/ROF/Range/Verses/Speed）
    int damage = 0;
    int rof = 0;             ///< 射击间隔（逻辑帧）
    float range = 0.0f;      ///< 射程（格）
    int weapon_speed = 0;    ///< [Weapon] Speed= leptons/帧
    bool proj_inviso = false;
    bool proj_arcing = false;
    bool proj_proximity = false;
    std::string proj_image;  ///< 弹道 SHP 基名（Projectile Image=）
    int armor_index = 0;
    int verses[11] = {100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100};
    int fire_cd = 0;         ///< 距下次可开火的剩余逻辑帧
    std::string deploys_into;
    bool harvester = false;
    int storage = 0;         ///< 矿车容量
    int cargo = 0;           ///< 当前装载矿量（bail 数）
    int harvest_timer = 0;   ///< 装/卸计时（对应 unit+0xF8 / +0x10C）

    Mission mission = Mission::None;
    float dest_x = 0.0f, dest_y = 0.0f;
    bool has_dest = false;
    std::vector<CellStruct> path;  ///< Order_Move 走 PathFinder 填的路点
    int path_i = 0;
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

/// 飞行弹道（BulletClass，size 0x160 @0x0046B540）。
struct Bullet {
    float x = 0.0f, y = 0.0f;
    float vx = 0.0f, vy = 0.0f;  ///< 格/逻辑帧（Speed leptons / 256）
    int target = -1;
    int owner = -1;          ///< 发射者 Object id（Verses 来源）
    int damage = 0;
    int verses[11] = {100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100};
    bool proximity = false;
    bool arcing = false;
    std::string image;       ///< Projectile Image=（缺省类型名）
    float arc_z = 0.0f;      ///< 仅表现；逻辑命中仍用 XY
    float dist_left = 0.0f;  ///< 剩余平面路程（格）
    bool alive = true;
};

/// 一个编队（Ctrl+数字建、数字选）。
using Team = std::vector<int>;

class World {
public:
    /// 从地图灌对象进来。houses 来自地图的 [Houses]。
    bool Build(const MapFile& map);

    /// 绑逻辑地图，给寻路 / CanPlaceHere 用。外壳在 TMP 的 LandType 填进格子之后再调。
    void Set_Logic_Map(MapClass* map);

    /// 绑 MapFile：采矿读写 OverlayPack；拷贝触发器并初始化 Elapsed 计时。
    void Set_Map_File(MapFile* map);

    /// 本机玩家资金（TEvent 45/46 / 侧栏 CREDITS）；其它房屋见 House_Credits。
    void Set_Player_Credits(int credits) noexcept { player_credits_ = credits; }
    int Player_Credits() const noexcept { return player_credits_; }
    int House_Credits(int house) const;
    void Set_House_Credits(int house, int credits);

    /// 逻辑帧号（对齐 gamemd Frame @0xA8ED84，15Hz）。
    int Logic_Frame() const noexcept { return logic_frame_; }

    /// 本逻辑帧累计入账的采矿/变卖信贷（外壳每帧 Take）。
    int Take_Credit_Delta() noexcept {
        const int d = credit_delta_;
        credit_delta_ = 0;
        return d;
    }
    void Add_Credits(int n) noexcept { credit_delta_ += n; }

    /// 用 rules.ini 的 Speed= / Strength= / Primary 武器覆盖默认值。
    void Apply_Type_Stats(UnitModelDB& db);

    /// 战斗 / 生产查询用（Update 里查武器与 DeploysInto）。
    void Set_Models(UnitModelDB* db) noexcept { models_ = db; }
    /// FogOfWar 下格是否已揭示（FogOfWar=no 恒真）。
    bool Cell_Revealed(int cx, int cy) const noexcept;
    UnitModelDB* Models() const noexcept { return models_; }

    /// 玩家电力：正=盈余。由外壳在逻辑帧里刷新后写入，供 UI / 生产加速。
    void Set_Power_State(int drained, int output) noexcept {
        power_drain_ = drained;
        power_output_ = output;
    }
    int Power_Drain() const noexcept { return power_drain_; }
    int Power_Output() const noexcept { return power_output_; }
    bool Low_Power() const noexcept { return power_output_ < power_drain_; }

    /// 在 (x,y) 生成一个对象（生产完成 / MCV 展开）。返回新 id，失败 -1。
    int Spawn(const char* type, MapObjectKind kind, int house, float x, float y);

    /// BuildingType::CanPlaceHere @0x00464AC0 → 0x00716150 → Cell 0x0047C620。
    /// PlaceAnywhere 或 foundation 各格 Land.Buildable，且无其它建筑重叠。
    bool Can_Place_Building(const char* type, float x, float y) const;

    /// [General] BaseUnit= 列表成员判定（ShortGame 败北 / MCVDeploy）。
    bool Is_Base_Unit(const Object& o) const;

    /// House 败北判定：ShortGame @0x004F8EC6 看建筑数+BaseUnit；否则 @0x004F8F21 全科技单位。
    bool House_Is_Defeated(int house) const;

    /// MPlayer_Defeated @0x004FC0B0：标 defeated，并按剩余阵营推 Flag_To_Win/Lose。
    void MPlayer_Defeated(int house);

    /// Flag_To_Win @0x004FC9E0 / Flag_To_Lose @0x004FCBD0（对本机玩家）。
    void Flag_To_Win();
    void Flag_To_Lose();

    /// All_To_Hunt @0x00501400：该阵营活体 Assign Mission::Hunt(0xF)。
    void All_To_Hunt(int house);

    /// Crowd_Cheer 房屋扫描（TAction 113 → 0x50C8C0）：
    /// 严格对齐原版的扫描集合（house + Techno 且非建筑），返回受影响个数。
    /// 音频（@0x750920 Play_Voc + 0x8871E0[+0x1C8] 词条）尚未接到设备。
    int Crowd_Cheer(int house);

    /// TAction 分发（jmp 0x6DFDEC）：已接线见 World.cpp；未逆完则 false。
    /// voc_name：Play Voc / 环境音等字符串参数（可空）。
    /// link_id：Enable/Disable Trigger / TeamType ID（Actions 第 3 槽 / +0x30）。
    bool Dispatch_TAction(int action, int house, int param1 = 0,
                          const char* voc_name = nullptr,
                          const char* link_id = nullptr);

    /// 注册一个声音回放钩子。TAction 19/99/108/113 命中时会调它，
    /// 把 voc_name 字符串原样喂回去（去前缀 `_` 也是调用方的事）。
    /// 默认是 nullptr → 与原先"音频桩"等价：不发声，只打印。
    using Sound_Player = void (*)(const char* voc_name);
    void Set_Sound_Player(Sound_Player fn) noexcept { sound_player_ = fn; }
    /// 自检用：注入一张 TeamType（Arena 无 [TeamTypes]）。
    void Add_Team_Type(MapTeamType tt);
    const std::vector<MapTeamType>& Team_Types() const noexcept {
        return team_types_;
    }
    /// 透传 MapFile::Base_Nodes，没绑 map 就空。
    const std::vector<MapBaseNode>& Base_Nodes() const noexcept {
        return map_file_ ? map_file_->Base_Nodes() : static_cast<const std::vector<MapBaseNode>&>(empty_nodes_);
    }
    /// 读某格的 overlay 字节（没绑 map → 0xFF）。
    uint8_t Map_Overlay_At(int cx, int cy) const noexcept {
        return map_file_ ? map_file_->Overlay_At(cx, cy) : 0xFF;
    }
    /// 本局已认领的 Voc/环境音次数（TAction 19/99/108 → 0x750920 桩）。
    int Sound_Play_Count() const noexcept { return sound_play_count_; }

    /// 解析并执行地图 [Actions] 里已逆完的条目。
    void Run_Trigger_Actions(const MapTrigger& trig);

    /// TEvent::HasOccurred @0x0071E940：未接线 type → false。
    bool Event_Has_Occurred(const MapTrigger& trig, const MapTriggerEvent& ev) const;

    /// Make Ally @0x004F9B70：House+0x5788 |= 1<<other（互盟由 TAction 37 双边调用）。
    void Make_Ally(int house_a, int house_b);
    /// Make Enemy @0x004F9F90：House+0x5788 &= ~(1<<other)。
    void Break_Ally(int house_a, int house_b);
    /// Allies 位掩码检测（House+0x5788）。
    bool Is_Ally(int house_a, int house_b) const;

    /// 全图揭示（TAction 16 shroud sweep）。
    void Reveal_Map();

    /// Scenario 任务计时器（+0x11E8 start / +0x11F0 dur；-1=未跑）。
    int Mission_Timer_Start() const noexcept { return mission_timer_start_; }
    int Mission_Timer_Duration() const noexcept { return mission_timer_dur_; }

    /// HouseClass::AI 电脑切片：IQ>=Production → Unload/Hunt；BaseNode 生产见下。
    /// 生产真路径（gamemd）：BaseNode 写 House+0x564C → Building AI 0x4500F0
    /// （门控 +0x1EE）→ Factory 0x4C98B0/0x4C9C70/0x4C9EA0；玩家侧栏则走
    /// Event#14 → Begin_Production @0x4FA350（内亦调 FindSuitableFactory）。
    void Tick_Computer_AI();

    /// BuildingType vcall+0x94 @0x5F7900 / Begin_Production @0x4FA438：
    /// 该阵营是否已有 Factory= 匹配的工厂建筑（"No-one can build." 拒因）。
    bool House_Has_Suitable_Factory(int house, const char* produce_type) const;

    /// Trigger 弹簧：全事件为真则执行 Actions（@0x007264C0 → 0x007265C0）。
    void Tick_Triggers();

    /// 地图触发器表（[Triggers]/[Events]/[Actions]）。
    const std::vector<MapTrigger>& Triggers() const noexcept { return triggers_; }

    MatchOutcome Outcome() const noexcept { return outcome_; }
    /// 测试/触发：强制房屋 IQ（对齐 House+0x1D0 clamp）。
    void Force_House_IQ(int house, int iq);
    bool House_Defeated_Flag(int house) const;
    /// 直接读 0/1 标记（Save/Load 用；不查 Is_House_Defeated 的语义）。
    uint8_t House_Defeated(int house) const noexcept {
        return (house >= 0 && house < static_cast<int>(house_defeated_.size()))
                   ? house_defeated_[house] : 0;
    }
    uint8_t House_Active(int house) const noexcept {
        return (house >= 0 && house < static_cast<int>(house_active_.size()))
                   ? house_active_[house] : 0;
    }
    uint8_t House_Human(int house) const noexcept {
        return (house >= 0 && house < static_cast<int>(house_human_.size()))
                   ? house_human_[house] : 0;
    }

    /// 某阵营是否已有某建筑类型（Prerequisite 用）。
    bool House_Has_Type(int house, const char* type) const;

    /// Prerequisite 单项：具体类型名，或 POWER/BARRACKS/FACTORY/RADAR/TECH/PROC 抽象组。
    bool House_Meets_Prereq(int house, const char* token) const;

    int Player_House() const noexcept { return player_house_; }

    const std::vector<Object>& Objects() const noexcept { return objects_; }
    std::vector<Object>& Objects() noexcept { return objects_; }
    const std::vector<Bullet>& Bullets() const noexcept { return bullets_; }
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
    /// 原版会做队形（同时到达），这里先按寻路路点走。
    int Order_Move(float x, float y);
    int Order_Attack_Move(float x, float y);
    int Order_Attack(int target_id);
    int Order_Stop();
    int Order_Guard();
    int Order_Scatter();
    int Order_Deploy();
    /// 选中矿车开始采矿（无选中则给己方空闲矿车）。
    int Order_Harvest();

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
    void Follow_Path(Object& o, float x, float y);
    void Tick_Combat(Object& o, float dt);
    void Tick_Guard(Object& o);
    void Tick_Deploy(Object& o);
    void Tick_Harvest(Object& o, float dt);
    void Tick_Hunt(Object& o, float dt);
    void Tick_Defeat();
    void Arm_Trigger_Timers(MapTrigger& trig);
    const MapTeamType* Find_Team_Type(const char* id) const;
    int Resolve_Team_House(const MapTeamType& tt, int fallback_house) const;
    MapObjectKind Kind_For_Type(const char* type) const;
    /// TAction 4/7：在 TeamType.Waypoint（或缺省出生点）投放 TaskForce 成员。
    int Spawn_Team_Type(const MapTeamType& tt, int house);
    bool Type_Exists(const char* type) const;
    bool House_Owns_Type(int house, const char* type) const;
    void Fire_Weapon(Object& attacker, Object& target);
    void Tick_Bullets();
    int Apply_Damage(Object& victim, int raw_damage, const Object& attacker);
    int Apply_Damage_Verses(Object& victim, int raw_damage, const int* verses);

    int Find_Enemy_In_Range(const Object& o, float range) const;
    int Find_Nearest_Enemy(const Object& o) const;
    bool Find_Ore_Near(float x, float y, float* ox, float* oy) const;
    bool Find_Refinery(int house, float* rx, float* ry) const;
    void Face_Toward(Object& o, float tx, float ty, float dt);
    int Count_House_Buildings(int house) const;
    int Count_House_Base_Units(int house) const;
    int Count_House_Technos(int house) const;
    void Init_Shroud();
    void Tick_Shroud();
    void Reveal_Around(float x, float y, int radius);

    std::vector<Object> objects_;
    std::vector<Bullet> bullets_;
    std::vector<MapWaypoint> waypoints_;
    std::vector<std::string> houses_;
    std::vector<Team> teams_{std::vector<Team>(10)};
    std::vector<uint8_t> house_defeated_;
    /// 本局是否参战（有过科技单位 / 玩家阵营）。空席位不进 ShortGame 败北链。
    std::vector<uint8_t> house_active_;
    /// House Allies= 位掩码（house index → bit）；自联盟含本阵营。
    std::vector<uint32_t> house_allies_;
    /// FogOfWar：已揭示格（1=可见）。FogOfWar=no 时全 1。
    std::vector<uint8_t> shroud_;
    int shroud_w_ = 0;
    int shroud_h_ = 0;
    int selected_count_ = 0;
    int next_id_ = 1;
    float camera_x_ = 0.0f, camera_y_ = 0.0f;
    bool have_camera_order_ = false;
    int player_house_ = -1;
    PathFinder pathfinder_;
    MapClass* logic_map_ = nullptr;
    UnitModelDB* models_ = nullptr;
    MapFile* map_file_ = nullptr;
    std::vector<MapBaseNode> empty_nodes_;  ///< Base_Nodes() 没 map 时的空 fallback
    std::vector<MapTrigger> triggers_;
    std::vector<MapTeamType> team_types_;
    int logic_frame_ = 0;
    int player_credits_ = 0;
    std::vector<int> house_credits_;
    std::vector<int> house_iq_;
    std::vector<uint8_t> house_human_;  ///< PlayerControl=yes → 1
    int power_drain_ = 0;
    int power_output_ = 0;
    int credit_delta_ = 0;
    MatchOutcome outcome_ = MatchOutcome::Playing;
    /// Scenario 全局变量（+0x1cb0，stride 0x28，最多 50 @0x689760）。
    uint8_t global_vars_[50] = {};
    /// Scenario 局部变量（+0x24b2，stride 0x28，最多 100 @0x689a00）。
    uint8_t local_vars_[100] = {};
    /// TAction 19/99/108 → 0x750920 音效认领计数（设备未接）。
    int sound_play_count_ = 0;
    /// 真实音频回放钩子（注册后 TAction 19/99/108/113 会调它）。
    Sound_Player sound_player_ = nullptr;
    /// Scenario 任务计时器：start=+0x11E8（-1 空闲），dur=+0x11F0（帧）。
    int mission_timer_start_ = -1;
    int mission_timer_dur_ = 0;
};

}  // namespace ra2
