// World.cpp

#include "game/World.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ra2 {
namespace {

/// 拾取半径（格）。比半格大一点，否则小单位很难点中。
constexpr float kPickRadius = 0.75f;

/// 各兵种的默认速度（格/秒）。
/// 【这是估的，不是逆出来的】原版速度在 rules.ini 的 Speed= 里，
/// 单位是"每逻辑帧走多少个 leptons 的 1/256"，换算链还没逆完。
/// 等 RulesFile 接进来改成从 rules 读；这里先给个能看得出快慢的值。
float Default_Speed(MapObjectKind kind) {
    switch (kind) {
        case MapObjectKind::Infantry: return 2.0f;
        case MapObjectKind::Unit:     return 3.5f;
        case MapObjectKind::Aircraft: return 6.0f;
        default:                      return 0.0f;
    }
}

/// 转向速度（朝向单位/秒，256 为一圈）。
float Default_Turn(MapObjectKind kind) {
    switch (kind) {
        case MapObjectKind::Infantry: return 256.0f;   // 步兵转身几乎瞬间
        case MapObjectKind::Unit:     return 180.0f;
        case MapObjectKind::Aircraft: return 120.0f;
        default:                      return 0.0f;
    }
}

}  // namespace

const char* Mission_Name(Mission m) {
    switch (m) {
        case Mission::None:       return "None";
        case Mission::Sleep:      return "Sleep";
        case Mission::Sticky:     return "Sticky";
        case Mission::Guard:      return "Guard";
        case Mission::AreaGuard:  return "AreaGuard";
        case Mission::Move:       return "Move";
        case Mission::Attack:     return "Attack";
        case Mission::AttackMove: return "AttackMove";
        case Mission::Scatter:    return "Scatter";
        case Mission::Deploy:     return "Deploy";
        case Mission::Stop:       return "Stop";
    }
    return "?";
}

// ---------------------------------------------------------------------------
// Build
// ---------------------------------------------------------------------------

bool World::Build(const MapFile& map) {
    objects_.clear();
    waypoints_ = map.Waypoints();
    selected_count_ = 0;
    next_id_ = 1;
    houses_ = map.Houses();
    player_house_ = -1;

    objects_.reserve(map.Objects().size());
    for (const MapObject& src : map.Objects()) {
        Object o;
        o.id = next_id_++;
        o.kind = src.kind;
        o.type = src.type;
        o.x = static_cast<float>(src.cx);
        o.y = static_cast<float>(src.cy);
        o.facing = src.facing;
        o.hp = src.hp;
        o.hp_max = src.hp;
        o.speed = Default_Speed(src.kind);
        o.turn_rate = Default_Turn(src.kind);

        // 阵营：对象段里存的是名字（如 Neutral / Russians），查 [Houses] 拿下标
        o.house = -1;
        for (size_t i = 0; i < houses_.size(); ++i) {
            if (houses_[i] == src.owner) {
                o.house = static_cast<int>(i);
                break;
            }
        }
        if (o.house < 0 && !src.owner.empty()) {
            // 地图里出现的阵营没在 [Houses] 里登记（实测有），补进去
            houses_.push_back(src.owner);
            o.house = static_cast<int>(houses_.size()) - 1;
        }

        // 任务：地图里写什么就是什么
        if (src.mission == "Sleep") {
            o.mission = Mission::Sleep;
        } else if (src.mission == "Sticky") {
            o.mission = Mission::Sticky;
        } else if (src.mission == "Area Guard" || src.mission == "AreaGuard") {
            o.mission = Mission::AreaGuard;
        } else if (src.mission == "Guard") {
            o.mission = Mission::Guard;
        } else {
            o.mission = (src.kind == MapObjectKind::Terrain) ? Mission::None
                                                             : Mission::Guard;
        }

        // 装饰物（树、路灯、交通灯）不能选中也不能动
        o.selectable = o.Is_Techno();
        o.is_mine = o.selectable && o.house >= 0 &&
                    houses_[static_cast<size_t>(o.house)] != "Neutral" &&
                    houses_[static_cast<size_t>(o.house)] != "Special";
        objects_.push_back(std::move(o));
    }

    // 玩家阵营：地图里第一个非中立、且真的有东西的阵营
    for (size_t i = 0; i < houses_.size(); ++i) {
        if (houses_[i] == "Neutral" || houses_[i] == "Special") {
            continue;
        }
        for (const Object& o : objects_) {
            if (o.house == static_cast<int>(i)) {
                player_house_ = static_cast<int>(i);
                break;
            }
        }
        if (player_house_ >= 0) {
            break;
        }
    }
    return !objects_.empty();
}

Object* World::Find(int id) {
    for (Object& o : objects_) {
        if (o.id == id) {
            return &o;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------

void World::Move_Toward(Object& o, float dt) {
    if (o.speed <= 0.0f || !o.has_dest) {
        return;
    }
    const float dx = o.dest_x - o.x;
    const float dy = o.dest_y - o.y;
    const float dist = std::sqrt(dx * dx + dy * dy);
    if (dist < 0.02f) {
        o.has_dest = false;
        o.mission = (o.mission == Mission::Move || o.mission == Mission::AttackMove)
                        ? Mission::Guard : o.mission;
        return;
    }
    // 先转向再走。原版是"朝向没转到位就减速"，这里简化成转向不阻塞移动。
    const float want = static_cast<float>(
        static_cast<int>(std::atan2(dx, -dy) * 128.0f / 3.14159265358979f) & 255);
    int diff = static_cast<int>(want) - o.facing;
    while (diff > 128) diff -= 256;
    while (diff < -128) diff += 256;
    const int step = static_cast<int>(o.turn_rate * dt);
    if (diff > step) {
        o.facing = (o.facing + step) & 255;
    } else if (diff < -step) {
        o.facing = (o.facing - step) & 255;
    } else {
        o.facing = static_cast<int>(want) & 255;
    }

    const float move = o.speed * dt;
    if (move >= dist) {
        o.x = o.dest_x;
        o.y = o.dest_y;
        o.has_dest = false;
    } else {
        o.x += dx / dist * move;
        o.y += dy / dist * move;
    }
}

void World::Update(float dt) {
    for (Object& o : objects_) {
        if (o.mission == Mission::Sleep || o.mission == Mission::Sticky) {
            continue;                      // 中立物件永远不动
        }
        if (o.mission == Mission::Move || o.mission == Mission::AttackMove) {
            Move_Toward(o, dt);
        } else if (o.mission == Mission::Scatter) {
            // 散开：往随机方向走一小段就停下。这里给个确定的偏移，
            // 免得每次跑出来不一样（锁步要求确定性）。
            if (!o.has_dest) {
                const float ang = static_cast<float>((o.id * 37) % 256) / 256.0f *
                                  6.28318530718f;
                o.dest_x = o.x + std::cos(ang) * 1.5f;
                o.dest_y = o.y + std::sin(ang) * 1.5f;
                o.has_dest = true;
                o.mission = Mission::Move;
            }
            Move_Toward(o, dt);
        }
    }
}

// ---------------------------------------------------------------------------
// 选择
// ---------------------------------------------------------------------------

int World::Pick_At(float x, float y) const {
    int best = -1;
    float best_d = kPickRadius * kPickRadius;
    for (const Object& o : objects_) {
        if (!o.selectable) {
            continue;
        }
        const float dx = o.x - x;
        const float dy = o.y - y;
        const float d = dx * dx + dy * dy;
        if (d < best_d) {
            best_d = d;
            best = o.id;
        }
    }
    return best;
}

void World::Select_None() {
    for (Object& o : objects_) {
        o.selected = false;
    }
    selected_count_ = 0;
}

int World::Select_In_Rect(float x0, float y0, float x1, float y1, bool add) {
    if (!add) {
        Select_None();
    }
    const float lx = (x0 < x1) ? x0 : x1;
    const float hx = (x0 < x1) ? x1 : x0;
    const float ly = (y0 < y1) ? y0 : y1;
    const float hy = (y0 < y1) ? y1 : y0;
    int n = 0;
    for (Object& o : objects_) {
        if (!o.selectable || o.selected) {
            continue;
        }
        if (o.x >= lx && o.x <= hx && o.y >= ly && o.y <= hy) {
            o.selected = true;
            ++n;
        }
    }
    selected_count_ += n;
    return n;
}

int World::Select_At(float x, float y, bool add) {
    const int id = Pick_At(x, y);
    if (id < 0) {
        if (!add) {
            Select_None();
        }
        return 0;
    }
    if (!add) {
        Select_None();
    }
    Object* o = Find(id);
    if (o == nullptr) {
        return 0;
    }
    if (add && o->selected) {
        o->selected = false;              // Shift 点已选中的 = 取消
        --selected_count_;
        return 0;
    }
    o->selected = true;
    ++selected_count_;
    return 1;
}

int World::Select_Same_Type() {
    // 以第一个选中单位的类型为准，全选同类型
    std::string want;
    for (const Object& o : objects_) {
        if (o.selected) {
            want = o.type;
            break;
        }
    }
    if (want.empty()) {
        return 0;
    }
    int n = 0;
    for (Object& o : objects_) {
        if (o.selectable && !o.selected && o.type == want) {
            o.selected = true;
            ++n;
        }
    }
    selected_count_ += n;
    return n;
}

int World::Select_All_Combat() {
    // P 是"集结所有有攻击力的部队"，属于**重选**而不是追加：
    // 先清掉当前选择，再挑车和步兵（建筑不算，原版也不算）。
    Select_None();
    int n = 0;
    for (Object& o : objects_) {
        if (!o.selectable) {
            continue;
        }
        if (o.kind == MapObjectKind::Unit || o.kind == MapObjectKind::Infantry) {
            o.selected = true;
            ++n;
        }
    }
    selected_count_ = n;
    return n;
}

int World::Select_By_Health(bool lowest) {
    // 原版是"按生命值/等级排序后依次选"，这里先实现成"选最极端的一个"
    int best = -1;
    float best_hp = lowest ? 2.0f : -1.0f;
    for (const Object& o : objects_) {
        if (!o.selectable) {
            continue;
        }
        const float frac = static_cast<float>(o.hp) /
                           static_cast<float>(o.hp_max > 0 ? o.hp_max : 1);
        if (lowest ? (frac < best_hp) : (frac > best_hp)) {
            best_hp = frac;
            best = o.id;
        }
    }
    if (best < 0) {
        return 0;
    }
    Select_None();
    Find(best)->selected = true;
    selected_count_ = 1;
    return 1;
}

std::vector<int> World::Selected_Ids() const {
    std::vector<int> out;
    for (const Object& o : objects_) {
        if (o.selected) {
            out.push_back(o.id);
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// 命令
// ---------------------------------------------------------------------------

static bool Can_Order(const Object& o) {
    return o.selected && o.selectable && o.mission != Mission::Sleep &&
           o.mission != Mission::Sticky;
}

int World::Order_Move(float x, float y) {
    int n = 0;
    for (Object& o : objects_) {
        if (!Can_Order(o) || o.speed <= 0.0f) {
            continue;
        }
        o.mission = Mission::Move;
        o.dest_x = x;
        o.dest_y = y;
        o.has_dest = true;
        o.target = -1;
        ++n;
    }
    return n;
}

int World::Order_Attack_Move(float x, float y) {
    const int n = Order_Move(x, y);
    for (Object& o : objects_) {
        if (o.selected && o.mission == Mission::Move) {
            o.mission = Mission::AttackMove;
        }
    }
    return n;
}

int World::Order_Attack(int target_id) {
    int n = 0;
    for (Object& o : objects_) {
        if (!Can_Order(o)) {
            continue;
        }
        o.mission = Mission::Attack;
        o.target = target_id;
        o.has_dest = false;
        ++n;
    }
    return n;
}

int World::Order_Stop() {
    int n = 0;
    for (Object& o : objects_) {
        if (!Can_Order(o)) {
            continue;
        }
        o.mission = Mission::Stop;
        o.has_dest = false;
        o.target = -1;
        ++n;
    }
    return n;
}

int World::Order_Guard() {
    int n = 0;
    for (Object& o : objects_) {
        if (!Can_Order(o)) {
            continue;
        }
        o.mission = Mission::Guard;
        o.has_dest = false;
        o.target = -1;
        ++n;
    }
    return n;
}

int World::Order_Scatter() {
    int n = 0;
    for (Object& o : objects_) {
        if (!Can_Order(o) || o.speed <= 0.0f) {
            continue;
        }
        o.mission = Mission::Scatter;
        o.has_dest = false;
        ++n;
    }
    return n;
}

int World::Order_Deploy() {
    int n = 0;
    for (Object& o : objects_) {
        if (!Can_Order(o)) {
            continue;
        }
        o.mission = Mission::Deploy;
        o.has_dest = false;
        ++n;
    }
    return n;
}

// ---------------------------------------------------------------------------
// 编队
// ---------------------------------------------------------------------------

void World::Team_Set(int slot) {
    if (slot < 0 || slot >= 10) {
        return;
    }
    teams_[static_cast<size_t>(slot)] = Selected_Ids();
}

void World::Team_Add(int slot) {
    if (slot < 0 || slot >= 10) {
        return;
    }
    Team& t = teams_[static_cast<size_t>(slot)];
    for (int id : Selected_Ids()) {
        if (std::find(t.begin(), t.end(), id) == t.end()) {
            t.push_back(id);
        }
    }
}

int World::Team_Select(int slot) {
    if (slot < 0 || slot >= 10) {
        return 0;
    }
    Select_None();
    int n = 0;
    for (int id : teams_[static_cast<size_t>(slot)]) {
        Object* o = Find(id);
        if (o != nullptr && o->selectable) {
            o->selected = true;
            ++n;
        }
    }
    selected_count_ = n;
    return n;
}

bool World::Team_Valid(int slot) const {
    return slot >= 0 && slot < 10 && !teams_[static_cast<size_t>(slot)].empty();
}

// ---------------------------------------------------------------------------
// 相机 / 出生点
// ---------------------------------------------------------------------------

bool World::Consume_Camera_Order(float* x, float* y) {
    if (!have_camera_order_) {
        return false;
    }
    have_camera_order_ = false;
    *x = camera_x_;
    *y = camera_y_;
    return true;
}

bool World::Spawn_Cell(int index, float* x, float* y) const {
    for (const MapWaypoint& w : waypoints_) {
        if (w.index == index) {
            *x = static_cast<float>(w.cx);
            *y = static_cast<float>(w.cy);
            return true;
        }
    }
    return false;
}

bool World::Home_Cell(float* x, float* y) const {
    // 主基地 = 玩家阵营的第一个建筑；没有就退化到第一个自己的单位
    for (const Object& o : objects_) {
        if (o.house == player_house_ && o.kind == MapObjectKind::Building) {
            *x = o.x;
            *y = o.y;
            return true;
        }
    }
    for (const Object& o : objects_) {
        if (o.house == player_house_) {
            *x = o.x;
            *y = o.y;
            return true;
        }
    }
    return false;
}

}  // namespace ra2
