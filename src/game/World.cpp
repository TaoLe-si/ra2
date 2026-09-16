// World.cpp

#include "game/World.h"

#include "data/UnitModel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

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
        case Mission::Harvest:    return "Harvest";
        case Mission::Stop:       return "Stop";
        case Mission::Hunt:       return "Hunt";
    }
    return "?";
}

// ---------------------------------------------------------------------------
// Build
// ---------------------------------------------------------------------------

bool World::Build(const MapFile& map) {
    objects_.clear();
    bullets_.clear();
    waypoints_ = map.Waypoints();
    selected_count_ = 0;
    next_id_ = 1;
    houses_ = map.Houses();
    player_house_ = -1;
    house_defeated_.assign(houses_.size(), 0);
    house_active_.assign(houses_.size(), 0);
    house_allies_.assign(houses_.size(), 0);
    house_credits_.assign(houses_.size(), 0);
    house_iq_.assign(houses_.size(), 0);
    house_human_.assign(houses_.size(), 0);
    outcome_ = MatchOutcome::Playing;
    logic_frame_ = 0;
    triggers_.clear();
    team_types_ = map.Team_Types();
    std::memset(global_vars_, 0, sizeof(global_vars_));
    std::memset(local_vars_, 0, sizeof(local_vars_));

    // Allies= 位掩码（gamemd 0x005010A8 / 0x00475260）：名列表 → 1<<HouseTypeIndex。
    for (size_t i = 0; i < houses_.size(); ++i) {
        uint32_t mask = (1u << static_cast<unsigned>(i));  // 总是与自己同盟
        const std::string body = map.Raw_Section(houses_[i].c_str());
        // 扫 Allies=
        for (size_t p = 0; p + 6 < body.size(); ++p) {
            if (_strnicmp(body.c_str() + p, "Allies", 6) != 0) {
                continue;
            }
            const char* v = body.c_str() + p + 6;
            while (*v == ' ' || *v == '\t') {
                ++v;
            }
            if (*v != '=') {
                continue;
            }
            ++v;
            std::string cur;
            auto flush = [&]() {
                while (!cur.empty() && (cur.back() == ' ' || cur.back() == '\t' ||
                                       cur.back() == '\r')) {
                    cur.pop_back();
                }
                size_t a = 0;
                while (a < cur.size() && (cur[a] == ' ' || cur[a] == '\t')) {
                    ++a;
                }
                cur = cur.substr(a);
                if (cur.empty()) {
                    return;
                }
                for (size_t j = 0; j < houses_.size(); ++j) {
                    if (_stricmp(houses_[j].c_str(), cur.c_str()) == 0) {
                        mask |= (1u << static_cast<unsigned>(j));
                        break;
                    }
                }
                cur.clear();
            };
            for (; *v && *v != '\n' && *v != '\r'; ++v) {
                if (*v == ',' || *v == '|') {
                    flush();
                } else {
                    cur.push_back(*v);
                }
            }
            flush();
            break;
        }
        house_allies_[i] = mask;
    }

    // 读各房屋 PlayerControl / IQ=（0x00500B40 / 0x00501210）。
    for (size_t i = 0; i < houses_.size(); ++i) {
        if (houses_[i].empty() || houses_[i] == "Neutral" || houses_[i] == "Special") {
            continue;
        }
        const std::string body = map.Raw_Section(houses_[i].c_str());
        if (body.empty()) {
            continue;
        }
        bool yes = false;
        for (size_t p = 0; p + 14 < body.size(); ++p) {
            if (_strnicmp(body.c_str() + p, "PlayerControl", 13) != 0) {
                continue;
            }
            const char* v = body.c_str() + p + 13;
            while (*v == ' ' || *v == '\t') {
                ++v;
            }
            if (*v != '=') {
                continue;
            }
            ++v;
            while (*v == ' ' || *v == '\t') {
                ++v;
            }
            if (_strnicmp(v, "yes", 3) == 0) {
                yes = true;
            }
            break;
        }
        house_human_[i] = yes ? 1 : 0;
        if (yes && player_house_ < 0) {
            player_house_ = static_cast<int>(i);
        }
        for (size_t p = 0; p + 3 < body.size(); ++p) {
            if (_strnicmp(body.c_str() + p, "IQ", 2) != 0) {
                continue;
            }
            const char cprev = (p > 0) ? body[p - 1] : '\n';
            if (cprev != '\n' && cprev != '\r' && cprev != ' ' && cprev != '\t') {
                continue;  // 避免误匹配 UIName 等
            }
            const char* v = body.c_str() + p + 2;
            while (*v == ' ' || *v == '\t') {
                ++v;
            }
            if (*v != '=') {
                continue;
            }
            ++v;
            house_iq_[i] = std::atoi(v);
            break;
        }
    }

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
        o.is_mine = false;  // player_house_ 定后再填
        objects_.push_back(std::move(o));
    }

    // 玩家阵营：若上面没读到 PlayerControl，退回第一个非中立且真有东西的阵营
    if (player_house_ < 0) {
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
    }
    // is_mine = 本地玩家房屋（可被选中/下令）
    for (Object& o : objects_) {
        o.is_mine = o.selectable && o.house == player_house_;
    }
    // 阵营表可能在灌对象时扩容，与 defeated/active 对齐。
    if (house_defeated_.size() < houses_.size()) {
        house_defeated_.resize(houses_.size(), 0);
    }
    if (house_active_.size() < houses_.size()) {
        house_active_.resize(houses_.size(), 0);
    }
    if (house_allies_.size() < houses_.size()) {
        house_allies_.resize(houses_.size(), 0);
        for (size_t i = 0; i < house_allies_.size(); ++i) {
            if (house_allies_[i] == 0) {
                house_allies_[i] = (1u << static_cast<unsigned>(i));
            }
        }
    }
    for (const Object& o : objects_) {
        if (o.house >= 0 && o.Is_Techno() && o.hp > 0 &&
            static_cast<size_t>(o.house) < house_active_.size()) {
            const std::string& hn = houses_[static_cast<size_t>(o.house)];
            if (hn != "Neutral" && hn != "Special") {
                house_active_[static_cast<size_t>(o.house)] = 1;
            }
        }
    }
    if (player_house_ >= 0 &&
        static_cast<size_t>(player_house_) < house_active_.size()) {
        house_active_[static_cast<size_t>(player_house_)] = 1;
    }
    return !objects_.empty();
}

void World::Set_Logic_Map(MapClass* map) {
    logic_map_ = map;
    pathfinder_.Set_Map(map);
    Init_Shroud();
}

void World::Apply_Type_Stats(UnitModelDB& db) {
    models_ = &db;
    Init_Shroud();
    // House IQ clamp vs MaxIQLevels（0x00500D9A）。
    const int max_iq = db.Max_IQ_Levels();
    for (size_t i = 0; i < house_iq_.size(); ++i) {
        if (house_iq_[i] > max_iq) {
            house_iq_[i] = max_iq;
        }
        if (house_iq_[i] < 0) {
            house_iq_[i] = 0;
        }
    }
    // rules Speed= 是整数。原版 15Hz 逻辑帧、1 格 = 256 lepton
    // （TacticalClass::CoordsToClient 0x006D1F10）。
    // Locomotion 的 lepton/帧乘数还没从 Drive/Walk 里抠完；先用
    // Speed=8 → 1.5 格/秒 把数值接上，乘数 15/80 标成待逆。
    constexpr float kSpeedScale = 15.0f / 80.0f;
    for (Object& o : objects_) {
        if (!o.Is_Techno()) {
            continue;
        }
        const UnitModel* um = db.Resolve(o.type.c_str());
        if (um == nullptr) {
            continue;
        }
        if (um->speed > 0 && o.kind != MapObjectKind::Building) {
            o.speed = static_cast<float>(um->speed) * kSpeedScale;
        }
        o.sight = um->sight;
        if (um->strength > 0) {
            o.hp_max = um->strength;
            o.hp = um->strength * o.hp / 256;
            if (o.hp < 1) {
                o.hp = 1;
            }
        }
        o.damage = um->damage;
        o.rof = um->rof;
        o.range = um->range;
        o.weapon_speed = um->weapon_speed;
        o.proj_inviso = um->proj_inviso;
        o.proj_arcing = um->proj_arcing;
        o.proj_proximity = um->proj_proximity;
        o.proj_image = um->proj_image;
        o.sight = um->sight;
        o.armor_index = um->armor_index;
        for (int i = 0; i < 11; ++i) {
            o.verses[i] = um->verses[i];
        }
        o.deploys_into = um->deploys_into;
        o.harvester = um->harvester;
        o.storage = um->storage;
        if (o.harvester && o.mission == Mission::Guard) {
            o.mission = Mission::Harvest;
        }
    }
}

void World::Follow_Path(Object& o, float x, float y) {
    o.path.clear();
    o.path_i = 0;
    const CellStruct from{static_cast<int16_t>(o.x + 0.5f),
                          static_cast<int16_t>(o.y + 0.5f)};
    const CellStruct to{static_cast<int16_t>(x + 0.5f),
                        static_cast<int16_t>(y + 0.5f)};
    const PathResult r = pathfinder_.Find_Path(from, to);
    if (r.found && r.waypoints.size() > 1) {
        o.path = r.waypoints;
        o.path_i = 1;  // 0 是当前格
        o.dest_x = static_cast<float>(o.path[1].X) + 0.5f;
        o.dest_y = static_cast<float>(o.path[1].Y) + 0.5f;
    } else {
        o.dest_x = x;
        o.dest_y = y;
    }
    o.has_dest = true;
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
        if (o.path_i + 1 < static_cast<int>(o.path.size())) {
            ++o.path_i;
            o.dest_x = static_cast<float>(o.path[static_cast<size_t>(o.path_i)].X) + 0.5f;
            o.dest_y = static_cast<float>(o.path[static_cast<size_t>(o.path_i)].Y) + 0.5f;
            return;
        }
        o.has_dest = false;
        o.path.clear();
        o.path_i = 0;
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

void World::Face_Toward(Object& o, float tx, float ty, float dt) {
    const float dx = tx - o.x;
    const float dy = ty - o.y;
    if (dx * dx + dy * dy < 1e-6f) {
        return;
    }
    const float want = static_cast<float>(
        static_cast<int>(std::atan2(dx, -dy) * 128.0f / 3.14159265358979f) & 255);
    int diff = static_cast<int>(want) - o.facing;
    while (diff > 128) {
        diff -= 256;
    }
    while (diff < -128) {
        diff += 256;
    }
    const int step = std::max(1, static_cast<int>(o.turn_rate * dt));
    if (diff > step) {
        o.facing = (o.facing + step) & 255;
    } else if (diff < -step) {
        o.facing = (o.facing - step) & 255;
    } else {
        o.facing = static_cast<int>(want) & 255;
    }
}

int World::Find_Enemy_In_Range(const Object& o, float range) const {
    if (range <= 0.0f || o.house < 0) {
        return -1;
    }
    const float r2 = range * range;
    int best = -1;
    float best_d = r2;
    for (const Object& t : objects_) {
        if (t.id == o.id || !t.Is_Techno() || t.hp <= 0) {
            continue;
        }
        if (t.house == o.house) {
            continue;
        }
        if (Is_Ally(o.house, t.house)) {
            continue;
        }
        // Neutral / Special 不当敌。
        if (t.house >= 0 && t.house < static_cast<int>(houses_.size())) {
            const std::string& hn = houses_[static_cast<size_t>(t.house)];
            if (hn == "Neutral" || hn == "Special") {
                continue;
            }
        }
        const float dx = t.x - o.x;
        const float dy = t.y - o.y;
        const float d2 = dx * dx + dy * dy;
        if (d2 <= best_d) {
            best_d = d2;
            best = t.id;
        }
    }
    return best;
}

bool World::Is_Ally(int house_a, int house_b) const {
    if (house_a < 0 || house_b < 0) {
        return false;
    }
    if (house_a == house_b) {
        return true;
    }
    if (static_cast<size_t>(house_a) >= house_allies_.size()) {
        return false;
    }
    return (house_allies_[static_cast<size_t>(house_a)] &
            (1u << static_cast<unsigned>(house_b))) != 0;
}

void World::Make_Ally(int house_a, int house_b) {
    // 0x004F9B70：or [house_a+0x5788], 1<<house_b.id
    if (house_a < 0 || house_b < 0) {
        return;
    }
    if (house_allies_.size() < houses_.size()) {
        house_allies_.resize(houses_.size(), 0);
        for (size_t i = 0; i < house_allies_.size(); ++i) {
            if (house_allies_[i] == 0) {
                house_allies_[i] = (1u << static_cast<unsigned>(i));
            }
        }
    }
    if (static_cast<size_t>(house_a) >= house_allies_.size() ||
        static_cast<size_t>(house_b) >= houses_.size()) {
        return;
    }
    house_allies_[static_cast<size_t>(house_a)] |=
        (1u << static_cast<unsigned>(house_b));
}

void World::Break_Ally(int house_a, int house_b) {
    // 0x004F9F90：and [house_a+0x5788], ~(1<<house_b.id)；不拆自联盟位。
    if (house_a < 0 || house_b < 0 || house_a == house_b) {
        return;
    }
    if (static_cast<size_t>(house_a) >= house_allies_.size()) {
        return;
    }
    house_allies_[static_cast<size_t>(house_a)] &=
        ~(1u << static_cast<unsigned>(house_b));
}

void World::Reveal_Map() {
    if (!shroud_.empty()) {
        std::fill(shroud_.begin(), shroud_.end(), static_cast<uint8_t>(1));
    }
}

int World::House_Credits(int house) const {
    if (house == player_house_) {
        return player_credits_;
    }
    if (house < 0 || static_cast<size_t>(house) >= house_credits_.size()) {
        return 0;
    }
    return house_credits_[static_cast<size_t>(house)];
}

void World::Set_House_Credits(int house, int credits) {
    if (house == player_house_) {
        player_credits_ = credits;
        return;
    }
    if (house < 0) {
        return;
    }
    if (house_credits_.size() < houses_.size()) {
        house_credits_.resize(houses_.size(), 0);
    }
    if (static_cast<size_t>(house) < house_credits_.size()) {
        house_credits_[static_cast<size_t>(house)] = credits;
    }
}

int World::Find_Nearest_Enemy(const Object& o) const {
    // Hunt：全图最近敌人（无射程限制）。
    return Find_Enemy_In_Range(o, 1.0e6f);
}

int World::Apply_Damage(Object& victim, int raw_damage, const Object& attacker) {
    return Apply_Damage_Verses(victim, raw_damage, attacker.verses);
}

int World::Apply_Damage_Verses(Object& victim, int raw_damage,
                               const int* verses) {
    if (raw_damage <= 0 || victim.hp <= 0 || verses == nullptr) {
        return 0;
    }
    int pct = 100;
    if (victim.armor_index >= 0 && victim.armor_index < 11) {
        pct = verses[victim.armor_index];
    }
    int dmg = raw_damage * pct / 100;
    if (dmg < 1) {
        dmg = 1;
    }
    victim.hp -= dmg;
    if (victim.hp < 0) {
        victim.hp = 0;
    }
    return dmg;
}

void World::Fire_Weapon(Object& attacker, Object& target) {
    // Weapon Speed @+0xa8：leptons/逻辑帧；1 格 = 256 leptons。
    // Inviso @BulletType+0x29e：无形弹，本帧结算（激光等）。
    // Speed<=0 同样无法飞行，按即时命中。
    if (attacker.proj_inviso || attacker.weapon_speed <= 0) {
        Apply_Damage(target, attacker.damage, attacker);
        return;
    }
    const float dx = target.x - attacker.x;
    const float dy = target.y - attacker.y;
    const float dist = std::sqrt(dx * dx + dy * dy);
    if (dist < 1e-4f) {
        Apply_Damage(target, attacker.damage, attacker);
        return;
    }
    const float speed_cells =
        static_cast<float>(attacker.weapon_speed) / 256.0f;
    Bullet b;
    b.x = attacker.x;
    b.y = attacker.y;
    b.vx = dx / dist * speed_cells;
    b.vy = dy / dist * speed_cells;
    b.target = target.id;
    b.owner = attacker.id;
    b.damage = attacker.damage;
    for (int i = 0; i < 11; ++i) {
        b.verses[i] = attacker.verses[i];
    }
    b.proximity = attacker.proj_proximity;
    b.arcing = attacker.proj_arcing;
    b.image = attacker.proj_image;
    b.dist_left = dist;
    b.alive = true;
    bullets_.push_back(b);
}

void World::Tick_Bullets() {
    // BulletClass::AI @0x004666E0：每帧推进；Proximity/到达则 Detonate。
    for (Bullet& b : bullets_) {
        if (!b.alive) {
            continue;
        }
        Object* tgt = Find(b.target);
        if (tgt == nullptr || tgt->hp <= 0) {
            b.alive = false;
            continue;
        }
        // 跟踪：重新对准当前目标（热导/普通弹在目标移动时仍飞向落点）。
        const float dx = tgt->x - b.x;
        const float dy = tgt->y - b.y;
        const float dist = std::sqrt(dx * dx + dy * dy);
        const float speed =
            std::sqrt(b.vx * b.vx + b.vy * b.vy);
        if (speed > 1e-6f && dist > 1e-4f) {
            b.vx = dx / dist * speed;
            b.vy = dy / dist * speed;
        }
        const float step = speed;
        const float hit_r = b.proximity ? 0.35f : 0.08f;
        if (dist <= hit_r || dist <= step) {
            Apply_Damage_Verses(*tgt, b.damage, b.verses);
            if (tgt->hp <= 0) {
                tgt->selectable = false;
                tgt->selected = false;
            }
            b.alive = false;
            continue;
        }
        b.x += b.vx;
        b.y += b.vy;
        b.dist_left = dist - step;
        if (b.arcing) {
            // 粗弧：路程中段抬高，仅供后续绘制；不影响命中。
            const float t = (b.dist_left > 0.0f)
                                ? (1.0f - b.dist_left / (b.dist_left + dist))
                                : 1.0f;
            b.arc_z = 4.0f * t * (1.0f - t);
        }
    }
    bullets_.erase(std::remove_if(bullets_.begin(), bullets_.end(),
                                  [](const Bullet& b) { return !b.alive; }),
                   bullets_.end());
}

void World::Tick_Combat(Object& o, float dt) {
    if (o.damage <= 0 || o.range <= 0.0f || o.hp <= 0) {
        return;
    }
    if (o.fire_cd > 0) {
        --o.fire_cd;
    }
    Object* tgt = Find(o.target);
    if (tgt == nullptr || tgt->hp <= 0 || !tgt->Is_Techno()) {
        o.target = -1;
        if (o.mission == Mission::Attack) {
            o.mission = Mission::Guard;
        }
        return;
    }
    const float dx = tgt->x - o.x;
    const float dy = tgt->y - o.y;
    const float dist = std::sqrt(dx * dx + dy * dy);
    Face_Toward(o, tgt->x, tgt->y, dt);
    if (dist > o.range) {
        // 追击：步兵/载具走过去；建筑原地干瞪眼。
        if (o.kind != MapObjectKind::Building && o.speed > 0.0f) {
            if (!o.has_dest ||
                std::fabs(o.dest_x - tgt->x) > 0.5f ||
                std::fabs(o.dest_y - tgt->y) > 0.5f) {
                Follow_Path(o, tgt->x, tgt->y);
            }
            Move_Toward(o, dt);
        }
        return;
    }
    // 进射程：停步开火
    o.has_dest = false;
    o.path.clear();
    if (o.fire_cd > 0) {
        return;
    }
    Fire_Weapon(o, *tgt);
    o.fire_cd = (o.rof > 0) ? o.rof : 15;
    // 目标死亡改由弹着结算；Inviso 已即时结算。
    if (tgt->hp <= 0) {
        tgt->selectable = false;
        tgt->selected = false;
        o.target = -1;
        if (o.mission == Mission::Attack) {
            o.mission = Mission::Guard;
        }
    }
}

void World::Tick_Guard(Object& o) {
    if (o.damage <= 0 || o.range <= 0.0f || o.hp <= 0) {
        return;
    }
    const float hunt =
        (o.mission == Mission::AreaGuard) ? o.range * 1.5f : o.range;
    const int eid = Find_Enemy_In_Range(o, hunt);
    if (eid >= 0) {
        o.target = eid;
        o.mission = Mission::Attack;
    }
}

void World::Tick_Deploy(Object& o) {
    if (o.deploys_into.empty() || models_ == nullptr) {
        o.mission = Mission::Guard;
        return;
    }
    const UnitModel* into = models_->Resolve(o.deploys_into.c_str());
    if (into == nullptr) {
        o.mission = Mission::Guard;
        return;
    }
    // 展开：换成建筑，占脚点中心（简化：原地变类型）。
    o.type = o.deploys_into;
    o.kind = MapObjectKind::Building;
    o.speed = 0.0f;
    o.has_dest = false;
    o.path.clear();
    o.mission = Mission::Guard;
    o.damage = into->damage;
    o.rof = into->rof;
    o.range = into->range;
    o.weapon_speed = into->weapon_speed;
    o.proj_inviso = into->proj_inviso;
    o.proj_arcing = into->proj_arcing;
    o.proj_proximity = into->proj_proximity;
    o.proj_image = into->proj_image;
    o.sight = into->sight;
    o.armor_index = into->armor_index;
    for (int i = 0; i < 11; ++i) {
        o.verses[i] = into->verses[i];
    }
    o.deploys_into.clear();
    o.harvester = false;
    o.storage = into->storage;
    o.cargo = 0;
    if (into->strength > 0) {
        o.hp_max = into->strength;
        o.hp = into->strength;
    }
}

bool World::Find_Ore_Near(float x, float y, float* ox, float* oy) const {
    if (map_file_ == nullptr || models_ == nullptr || ox == nullptr || oy == nullptr) {
        return false;
    }
    const int iso_w = map_file_->Iso_Width();
    const int H = map_file_->Height();
    const int cx0 = static_cast<int>(x);
    const int cy0 = static_cast<int>(y);
    int best_d2 = 999999;
    int bx = -1, by = -1;
    // 由近及远扫 24 格半径（够用，全图扫太贵）。
    constexpr int kR = 24;
    for (int dy = -kR; dy <= kR; ++dy) {
        for (int dx = -kR; dx <= kR; ++dx) {
            const int cx = cx0 + dx;
            const int cy = cy0 + dy;
            if (cx < 0 || cy < 0 || cx >= iso_w || cy >= H) {
                continue;
            }
            const int ov = map_file_->Overlay_At(cx, cy);
            if (ov == 0xFF || !models_->Overlay_Is_Ore(ov)) {
                continue;
            }
            const int d2 = dx * dx + dy * dy;
            if (d2 < best_d2) {
                best_d2 = d2;
                bx = cx;
                by = cy;
            }
        }
    }
    if (bx < 0) {
        return false;
    }
    *ox = static_cast<float>(bx) + 0.5f;
    *oy = static_cast<float>(by) + 0.5f;
    return true;
}

bool World::Find_Refinery(int house, float* rx, float* ry) const {
    if (rx == nullptr || ry == nullptr) {
        return false;
    }
    const std::vector<std::string>* names = nullptr;
    static const std::vector<std::string> kFallback = {"GAREFN", "NAREFN", "YAREFN"};
    if (models_ != nullptr) {
        names = &models_->Prereq_Group("PROC");
    }
    if (names == nullptr || names->empty()) {
        names = &kFallback;
    }
    float best_d2 = 1e12f;
    bool found = false;
    float best_x = 0.0f, best_y = 0.0f;
    for (const Object& o : objects_) {
        if (o.house != house || o.hp <= 0 || o.kind != MapObjectKind::Building) {
            continue;
        }
        bool hit = false;
        for (const std::string& n : *names) {
            if (_stricmp(o.type.c_str(), n.c_str()) == 0) {
                hit = true;
                break;
            }
        }
        if (!hit) {
            continue;
        }
        const float dx = o.x - *rx;
        const float dy = o.y - *ry;
        const float d2 = dx * dx + dy * dy;
        if (!found || d2 < best_d2) {
            best_d2 = d2;
            best_x = o.x;
            best_y = o.y;
            found = true;
        }
    }
    if (found) {
        *rx = best_x;
        *ry = best_y;
    }
    return found;
}

void World::Tick_Harvest(Object& o, float dt) {
    if (!o.harvester || o.storage <= 0 || o.hp <= 0 || models_ == nullptr) {
        return;
    }
    ++o.harvest_timer;

    // 满载或无矿可挖 → 回矿厂卸货（DumpRate*900 帧/bail，0x0073E361）
    const bool full = (o.cargo >= o.storage);
    float ox = 0.0f, oy = 0.0f;
    const bool have_ore = !full && Find_Ore_Near(o.x, o.y, &ox, &oy);

    if (full || !have_ore) {
        float rx = o.x, ry = o.y;
        if (!Find_Refinery(o.house, &rx, &ry)) {
            return;
        }
        const float dx = rx - o.x;
        const float dy = ry - o.y;
        if (dx * dx + dy * dy > 2.25f) {
            if (!o.has_dest || std::fabs(o.dest_x - rx) > 0.5f) {
                Follow_Path(o, rx, ry);
            }
            Move_Toward(o, dt);
            return;
        }
        o.has_dest = false;
        if (o.cargo <= 0) {
            o.harvest_timer = 0;
            return;
        }
        const int dump_iv = models_->Harvester_Dump_Interval_Frames();
        if (o.harvest_timer < dump_iv) {
            return;
        }
        o.harvest_timer = 0;
        --o.cargo;
        if (o.house == player_house_) {
            credit_delta_ += models_->Ore_Bail_Value();
        } else if (o.house >= 0 &&
                   static_cast<size_t>(o.house) < house_credits_.size()) {
            house_credits_[static_cast<size_t>(o.house)] +=
                models_->Ore_Bail_Value();
        }
        return;
    }

    // 装矿：走到矿格，每 LoadRate*3 帧挖 1 bail（0x0073D515）
    const int cx = static_cast<int>(o.x);
    const int cy = static_cast<int>(o.y);
    bool on_ore = false;
    if (map_file_ != nullptr) {
        const int ov = map_file_->Overlay_At(cx, cy);
        on_ore = (ov != 0xFF && models_->Overlay_Is_Ore(ov));
    }
    if (!on_ore) {
        if (!o.has_dest || std::fabs(o.dest_x - ox) > 0.5f ||
            std::fabs(o.dest_y - oy) > 0.5f) {
            Follow_Path(o, ox, oy);
        }
        Move_Toward(o, dt);
        return;
    }
    o.has_dest = false;
    const int load_iv = models_->Harvester_Load_Interval_Frames();
    if (o.harvest_timer < load_iv) {
        return;
    }
    o.harvest_timer = 0;
    int data = map_file_->Overlay_Data_At(cx, cy);
    if (data == 0xFF) {
        data = 0;
    }
    // 从 OverlayData 扣密度；耗尽则清 Overlay（与原版挖空格一致的方向）。
    if (data > 0) {
        --data;
        map_file_->Set_Overlay_Data_At(cx, cy, static_cast<uint8_t>(data));
    }
    if (data <= 0) {
        map_file_->Set_Overlay_At(cx, cy, 0xFF);
        map_file_->Set_Overlay_Data_At(cx, cy, 0);
    }
    if (o.cargo < o.storage) {
        ++o.cargo;
    }
}

void World::Update(float dt) {
    // 先清掉死透的选中计数
    selected_count_ = 0;
    for (Object& o : objects_) {
        if (o.selected && o.hp > 0) {
            ++selected_count_;
        } else if (o.hp <= 0) {
            o.selected = false;
        }
    }

    for (Object& o : objects_) {
        if (o.hp <= 0) {
            continue;
        }
        if (o.mission == Mission::Sleep || o.mission == Mission::Sticky) {
            continue;
        }
        if (o.mission == Mission::Deploy) {
            Tick_Deploy(o);
            continue;
        }
        if (o.mission == Mission::Harvest) {
            Tick_Harvest(o, dt);
            continue;
        }
        if (o.mission == Mission::Guard || o.mission == Mission::AreaGuard) {
            if (o.harvester) {
                o.mission = Mission::Harvest;
                Tick_Harvest(o, dt);
                continue;
            }
            Tick_Guard(o);
        }
        if (o.mission == Mission::Attack) {
            Tick_Combat(o, dt);
            continue;
        }
        if (o.mission == Mission::AttackMove) {
            // 边走边打：射程内有敌人则切 Attack，走完回 Guard。
            if (o.damage > 0 && o.range > 0.0f) {
                const int eid = Find_Enemy_In_Range(o, o.range);
                if (eid >= 0) {
                    o.target = eid;
                    Tick_Combat(o, dt);
                    continue;
                }
            }
            Move_Toward(o, dt);
            continue;
        }
        if (o.mission == Mission::Hunt) {
            Tick_Hunt(o, dt);
            continue;
        }
        if (o.mission == Mission::Move) {
            Move_Toward(o, dt);
        } else if (o.mission == Mission::Scatter) {
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
    Tick_Bullets();
    Tick_Defeat();
    ++logic_frame_;
    Tick_Computer_AI();
    Tick_Shroud();
    Tick_Triggers();
}

void World::Set_Map_File(MapFile* map) {
    map_file_ = map;
    triggers_.clear();
    logic_frame_ = 0;
    if (map == nullptr) {
        team_types_.clear();
        return;
    }
    triggers_ = map->Triggers();
    team_types_ = map->Team_Types();
    for (MapTrigger& t : triggers_) {
        Arm_Trigger_Timers(t);
    }
}

void World::Arm_Trigger_Timers(MapTrigger& trig) {
    // Trigger::Timer_Reset @0x00726400：type==13 时
    //   start = Frame；duration = param * 15（lea eax,[eax+eax*2]; lea eax,[eax+eax*4]）。
    for (size_t i = 0; i < trig.events.size(); ++i) {
        const MapTriggerEvent& ev = trig.events[i];
        if (ev.type != 13 && ev.type != 51) {
            continue;
        }
        trig.timer_start = logic_frame_;
        trig.timer_duration = ev.param * 15;
        trig.events_done &= ~(1u << static_cast<unsigned>(i));
    }
}

bool World::Type_Exists(const char* type) const {
    if (type == nullptr || type[0] == '\0') {
        return false;
    }
    for (const Object& o : objects_) {
        if (o.hp > 0 && _stricmp(o.type.c_str(), type) == 0) {
            return true;
        }
    }
    return false;
}

bool World::House_Owns_Type(int house, const char* type) const {
    if (type == nullptr || type[0] == '\0' || house < 0) {
        return false;
    }
    for (const Object& o : objects_) {
        if (o.hp > 0 && o.house == house &&
            _stricmp(o.type.c_str(), type) == 0) {
            return true;
        }
    }
    return false;
}

bool World::Event_Has_Occurred(const MapTrigger& trig,
                               const MapTriggerEvent& ev) const {
    // TEvent::HasOccurred @0x0071E940。未接线的 type 一律 false（禁止编造）。
    switch (ev.type) {
        case 0:  // None → 0x71ec63 入口 test ecx,ecx; je fail
            return false;
        case 8:  // StageD slot9 → 0x71f1b1 立即成功（Arena 环境音触发）
            return true;
        case 13:    // Elapsed Time @0x71ec33
        case 51: {  // 同 stage0→0x71ec33（表 0x71f248）
            if (trig.timer_start < 0) {
                return trig.timer_duration == 0;
            }
            const int elapsed = logic_frame_ - trig.timer_start;
            return elapsed >= trig.timer_duration;
        }
        case 14: {  // Scenario Mission Timer @0x71ebe9
            if (mission_timer_start_ < 0) {
                return false;
            }
            return (logic_frame_ - mission_timer_start_) >= mission_timer_dur_;
        }
        case 23:  // Building Exists @0x71eea9（第三表 type-0xC）
            if (ev.kind == 1) {
                return Type_Exists(ev.type_name.c_str());
            }
            return false;
        case 32: {  // House+0x5550 vector[param] nonzero @0x71efa8
            // 有 type_name（kind=1）时按拥有该类型计；纯索引向量未全逆前不编造。
            int hi = -1;
            for (size_t i = 0; i < houses_.size(); ++i) {
                if (_stricmp(houses_[i].c_str(), trig.house.c_str()) == 0) {
                    hi = static_cast<int>(i);
                    break;
                }
            }
            if (hi < 0 || ev.type_name.empty()) {
                return false;
            }
            return House_Owns_Type(hi, ev.type_name.c_str());
        }
        case 57: {  // House+0x5550 vector[param]==0 @0x71efcb
            int hi = -1;
            for (size_t i = 0; i < houses_.size(); ++i) {
                if (_stricmp(houses_[i].c_str(), trig.house.c_str()) == 0) {
                    hi = static_cast<int>(i);
                    break;
                }
            }
            if (hi < 0 || ev.type_name.empty()) {
                return false;
            }
            return !House_Owns_Type(hi, ev.type_name.c_str());
        }
        case 27:  // Global set @0x71eb53 → 0x689760
            if (ev.param >= 0 && ev.param < 50) {
                return global_vars_[ev.param] != 0;
            }
            return false;
        case 28:  // Global clear @0x71eb75（sete = 取反）
            if (ev.param >= 0 && ev.param < 50) {
                return global_vars_[ev.param] == 0;
            }
            return false;
        case 30:  // Power OK @0x71f099 → 0x4FCE30 output>=drain
            return power_drain_ <= 0 || power_output_ >= power_drain_;
        case 58:  // Low power @0x71f0bd（0x4FCE30 反测）
            return power_drain_ > 0 && power_output_ < power_drain_;
        case 36:  // Local set @0x71eb9e → 0x689a00
            if (ev.param >= 0 && ev.param < 100) {
                return local_vars_[ev.param] != 0;
            }
            return false;
        case 37:  // Local clear @0x71ebc0
            if (ev.param >= 0 && ev.param < 100) {
                return local_vars_[ev.param] == 0;
            }
            return false;
        case 45:  // Credits <= @0x71eae7（Scenario+0x352c）
            return player_credits_ <= ev.param;
        case 46:  // Credits >= @0x71eb06
            return player_credits_ >= ev.param;
        case 47: {  // Frame/15 >= param @0x71eb28（0x88888889 魔数 ÷15）
            const int seconds = logic_frame_ / 15;
            return seconds >= ev.param;
        }
        case 60: {  // BuildingType count >= N @0x71e96b
            if (ev.type_name.empty()) {
                return false;
            }
            int n = 0;
            for (const Object& o : objects_) {
                if (o.hp > 0 && o.kind == MapObjectKind::Building &&
                    _stricmp(o.type.c_str(), ev.type_name.c_str()) == 0) {
                    ++n;
                }
            }
            return n >= ev.param;
        }
        case 61: {  // BuildingType absent @0x71ea30
            if (ev.type_name.empty()) {
                return false;
            }
            for (const Object& o : objects_) {
                if (o.hp > 0 && o.kind == MapObjectKind::Building &&
                    _stricmp(o.type.c_str(), ev.type_name.c_str()) == 0) {
                    return false;
                }
            }
            return true;
        }
        default:
            return false;
    }
}

void World::Init_Shroud() {
    shroud_.clear();
    shroud_w_ = 0;
    shroud_h_ = 0;
    if (logic_map_ == nullptr) {
        return;
    }
    shroud_w_ = logic_map_->Width();
    shroud_h_ = logic_map_->Height();
    if (shroud_w_ <= 0 || shroud_h_ <= 0) {
        return;
    }
    const bool fog = models_ != nullptr && models_->Fog_Of_War();
    shroud_.assign(static_cast<size_t>(shroud_w_) * shroud_h_, fog ? 0 : 1);
    if (!fog) {
        return;
    }
    Tick_Shroud();
}

bool World::Cell_Revealed(int cx, int cy) const noexcept {
    if (shroud_.empty() || shroud_w_ <= 0) {
        return true;
    }
    if (cx < 0 || cy < 0 || cx >= shroud_w_ || cy >= shroud_h_) {
        return false;
    }
    return shroud_[static_cast<size_t>(cy) * shroud_w_ + cx] != 0;
}

void World::Reveal_Around(float x, float y, int radius) {
    if (shroud_.empty() || radius < 0) {
        return;
    }
    const int cx = static_cast<int>(x);
    const int cy = static_cast<int>(y);
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (dx * dx + dy * dy > radius * radius) {
                continue;
            }
            const int gx = cx + dx;
            const int gy = cy + dy;
            if (gx < 0 || gy < 0 || gx >= shroud_w_ || gy >= shroud_h_) {
                continue;
            }
            shroud_[static_cast<size_t>(gy) * shroud_w_ + gx] = 1;
        }
    }
}

void World::Tick_Shroud() {
    // FogOfWar=no：Init 已全亮。=yes：按己方/同盟 techno 的 Sight= 揭开。
    if (models_ == nullptr || !models_->Fog_Of_War() || shroud_.empty()) {
        return;
    }
    for (const Object& o : objects_) {
        if (o.hp <= 0 || !o.Is_Techno()) {
            continue;
        }
        if (o.house != player_house_ && !Is_Ally(player_house_, o.house)) {
            continue;
        }
        int r = o.sight;
        if (r <= 0) {
            r = 1;
        }
        Reveal_Around(o.x, o.y, r);
    }
}

void World::Tick_Computer_AI() {
    // LogicClass::AI → House::AI @0x004F8440：非人类走电脑脑。
    // @0x004F85A3：IQ(+0x24C) >= Rules [IQ] Production(+0x143C) 时
    //   置 House+0x1F3/+0x1EE/+0x1EF。
    // Unload：Unit AI @0x007409F8（DeploysInto∈BuildingTypes 且 +0x1F3 且 !human）。
    //   AIAutoDeployFrameDelay(Rules+0xE2C) 无 .text Unload 消费者（勿接延迟）。
    // All_To_Hunt @0x00501400：同门控下有可战斗单位则 Hunt。
    // GuardArea @0x0051CCE4：IQ < GuardArea 时压 Mission=Guard(5)；
    //   IQ >= GuardArea 则允许 Area Guard 路径。
    //
    // 生产（勿从本函数直调 Begin_Production @0x4FA350）：
    //   BaseNode 0x4FE3E0 写 House+0x564C；FindSuitableFactory 0x5F7900 只找厂。
    //   Building AI 0x4500F0：FactoryType(+0xEB8) 且 House+0x1EE → 0x4FBD80
    //   取 pending → Factory 0x4C98B0/0x4C9C70/0x4C9EA0（与 BP 内部同源）。
    //   玩家：Event#14 → 0x4C711B → Begin_Production。BaseNode 表未接前不造 AI 单。
    if (outcome_ != MatchOutcome::Playing) {
        return;
    }
    const int prod_iq =
        (models_ != nullptr) ? models_->IQ_Production() : 5;
    const int guard_iq =
        (models_ != nullptr) ? models_->IQ_Guard_Area() : 4;
    for (size_t hi = 0; hi < houses_.size(); ++hi) {
        if (static_cast<int>(hi) == player_house_) {
            continue;
        }
        if (hi < house_human_.size() && house_human_[hi]) {
            continue;
        }
        if (hi >= house_iq_.size()) {
            continue;
        }
        const int iq = house_iq_[hi];
        if (iq < guard_iq) {
            continue;
        }
        const bool can_prod = iq >= prod_iq;
        bool need_hunt = false;
        for (Object& o : objects_) {
            if (o.house != static_cast<int>(hi) || o.hp <= 0) {
                continue;
            }
            if (o.kind == MapObjectKind::Building) {
                continue;
            }
            if (can_prod && Is_Base_Unit(o) && !o.deploys_into.empty() &&
                o.mission != Mission::Deploy) {
                o.mission = Mission::Deploy;
                o.has_dest = false;
                continue;
            }
            if (Is_Base_Unit(o)) {
                continue;
            }
            if (can_prod && o.damage > 0 && o.mission != Mission::Hunt) {
                need_hunt = true;
                break;
            }
            if (!can_prod && o.damage > 0 && o.mission == Mission::Guard) {
                o.mission = Mission::AreaGuard;
            }
        }
        if (need_hunt) {
            All_To_Hunt(static_cast<int>(hi));
        }
    }
}

void World::Tick_Triggers() {
    // Trigger::Spring @0x007264C0：+0x44 须非 0、+0x30 须为 0；逐事件 HasOccurred。
    if (outcome_ != MatchOutcome::Playing) {
        return;
    }
    for (MapTrigger& trig : triggers_) {
        if (!trig.enabled || trig.fired || trig.events.empty()) {
            continue;
        }
        bool all = true;
        for (size_t i = 0; i < trig.events.size(); ++i) {
            if (trig.events_done & (1u << static_cast<unsigned>(i))) {
                continue;
            }
            if (!Event_Has_Occurred(trig, trig.events[i])) {
                all = false;
                break;
            }
            trig.events_done |= (1u << static_cast<unsigned>(i));
        }
        if (!all) {
            continue;
        }
        std::printf("  Trigger fire id=%s name=%s\n", trig.id.c_str(),
                    trig.name.c_str());
        Run_Trigger_Actions(trig);
        // 弹簧后闩 +0x30（对齐 0x726720 语义）；53 Enable 会清闩并重装计时。
        trig.fired = true;
        if (outcome_ != MatchOutcome::Playing) {
            return;
        }
    }
}

void World::Run_Trigger_Actions(const MapTrigger& trig) {
    // Actions：count, then Type,P1,P2,P3,P4,P5,P6,WP  （每条 8 字段；Bermuda/Arena 实测）
    // 字段可为整数或 Voc/Theme/TriggerID 名；不能整表 atoi。
    if (trig.actions_raw.empty()) {
        return;
    }
    std::vector<std::string> tokens;
    {
        std::string cur;
        for (char ch : trig.actions_raw) {
            if (ch == ',') {
                tokens.push_back(cur);
                cur.clear();
            } else {
                cur.push_back(ch);
            }
        }
        if (!cur.empty()) {
            tokens.push_back(cur);
        }
    }
    if (tokens.empty()) {
        return;
    }
    int house = -1;
    for (size_t i = 0; i < houses_.size(); ++i) {
        if (_stricmp(houses_[i].c_str(), trig.house.c_str()) == 0) {
            house = static_cast<int>(i);
            break;
        }
    }
    const int count = std::atoi(tokens[0].c_str());
    size_t i = 1;
    for (int a = 0; a < count && i < tokens.size(); ++a) {
        const int action = std::atoi(tokens[i].c_str());
        const int param1 =
            (i + 1 < tokens.size()) ? std::atoi(tokens[i + 1].c_str()) : 0;
        // p2：Voc 名或 Trigger ID（53/54）；纯数字则不当作名。
        std::string link;
        if (i + 2 < tokens.size()) {
            link = tokens[i + 2];
        }
        std::string voc;
        if (!link.empty() && (link[0] < '0' || link[0] > '9') && link[0] != '-') {
            voc = link;
        }
        if (voc.empty()) {
            for (size_t k = i + 1; k < i + 8 && k < tokens.size(); ++k) {
                const std::string& t = tokens[k];
                if (!t.empty() && ((t[0] >= 'A' && t[0] <= 'Z') ||
                                   (t[0] >= 'a' && t[0] <= 'z') || t[0] == '_')) {
                    voc = t;
                    break;
                }
            }
        }
        Dispatch_TAction(action, house, param1, voc.c_str(), link.c_str());
        i += 8;
    }
}


int World::Spawn(const char* type, MapObjectKind kind, int house, float x,
                 float y) {
    if (type == nullptr || type[0] == '\0') {
        return -1;
    }
    Object o;
    o.id = next_id_++;
    o.kind = kind;
    o.type = type;
    o.house = house;
    o.x = x;
    o.y = y;
    o.hp = 256;
    o.hp_max = 256;
    o.speed = Default_Speed(kind);
    o.turn_rate = Default_Turn(kind);
    o.mission = Mission::Guard;
    o.selectable = o.Is_Techno();
    o.is_mine = o.selectable && house == player_house_;
    // [IQ] GuardArea=：新建单位进 Area Guard（规则注释；门控 @0x51CCE4）。
    if (models_ != nullptr && kind != MapObjectKind::Building &&
        house != player_house_ &&
        house >= 0 && static_cast<size_t>(house) < house_iq_.size() &&
        !(house < static_cast<int>(house_human_.size()) && house_human_[house]) &&
        house_iq_[static_cast<size_t>(house)] >= models_->IQ_Guard_Area()) {
        o.mission = Mission::AreaGuard;
    }
    if (models_ != nullptr) {
        const UnitModel* um = models_->Resolve(type);
        if (um != nullptr) {
            if (um->strength > 0) {
                o.hp = o.hp_max = um->strength;
            }
            if (um->speed > 0 && kind != MapObjectKind::Building) {
                o.speed = static_cast<float>(um->speed) * (15.0f / 80.0f);
            }
            o.damage = um->damage;
            o.rof = um->rof;
            o.range = um->range;
            o.weapon_speed = um->weapon_speed;
            o.proj_inviso = um->proj_inviso;
            o.proj_arcing = um->proj_arcing;
            o.proj_proximity = um->proj_proximity;
            o.proj_image = um->proj_image;
            o.sight = um->sight;
            o.armor_index = um->armor_index;
            for (int i = 0; i < 11; ++i) {
                o.verses[i] = um->verses[i];
            }
            o.deploys_into = um->deploys_into;
            o.harvester = um->harvester;
            o.storage = um->storage;
            if (o.harvester) {
                o.mission = Mission::Harvest;
            }
        }
    }
    if (house >= 0) {
        if (house_defeated_.size() < houses_.size()) {
            house_defeated_.resize(houses_.size(), 0);
        }
        if (house_active_.size() < houses_.size()) {
            house_active_.resize(houses_.size(), 0);
        }
        if (house_allies_.size() < houses_.size()) {
            house_allies_.resize(houses_.size(), 0);
            for (size_t i = 0; i < house_allies_.size(); ++i) {
                if (house_allies_[i] == 0) {
                    house_allies_[i] = (1u << static_cast<unsigned>(i));
                }
            }
        }
        if (static_cast<size_t>(house) < house_active_.size()) {
            const std::string& hn = houses_[static_cast<size_t>(house)];
            if (hn != "Neutral" && hn != "Special") {
                house_active_[static_cast<size_t>(house)] = 1;
            }
        }
    }
    objects_.push_back(std::move(o));
    return objects_.back().id;
}

bool World::House_Has_Suitable_Factory(int house, const char* produce_type) const {
    // BuildingType::FindSuitableFactory @0x5F7900：扫房屋建筑，
    // BuildingType+0xEB8（Factory=）须匹配待产类型的 Abstract 槽。
    if (house < 0 || produce_type == nullptr || produce_type[0] == '\0' ||
        models_ == nullptr) {
        return false;
    }
    const UnitModel* want = models_->Resolve(produce_type);
    if (want == nullptr) {
        return false;
    }
    const char* need = UnitModelDB::Needed_Factory_For_Category(want->category);
    if (need == nullptr || need[0] == '\0') {
        return true;  // 无 Factory 需求（非生产页）
    }
    for (const Object& o : objects_) {
        if (o.house != house || o.hp <= 0 || o.kind != MapObjectKind::Building) {
            continue;
        }
        const UnitModel* um = models_->Resolve(o.type.c_str());
        if (um != nullptr && !um->factory.empty() &&
            _stricmp(um->factory.c_str(), need) == 0) {
            return true;
        }
    }
    return false;
}

bool World::House_Has_Type(int house, const char* type) const {
    if (type == nullptr || house < 0) {
        return false;
    }
    for (const Object& o : objects_) {
        if (o.house == house && o.hp > 0 && _stricmp(o.type.c_str(), type) == 0) {
            return true;
        }
    }
    return false;
}

bool World::House_Meets_Prereq(int house, const char* token) const {
    if (token == nullptr || token[0] == '\0' || house < 0) {
        return false;
    }
    if (_stricmp(token, "none") == 0) {
        return true;
    }
    // 抽象组：rules [General] PrerequisitePower=…（gamemd 0x0066D530 读入）。
    if (models_ != nullptr) {
        const std::vector<std::string>& group = models_->Prereq_Group(token);
        if (!group.empty()) {
            for (const std::string& n : group) {
                if (House_Has_Type(house, n.c_str())) {
                    return true;
                }
            }
            return false;
        }
    }
    return House_Has_Type(house, token);
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
        if (!Cell_Revealed(static_cast<int>(o.x), static_cast<int>(o.y))) {
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
        // 只选己方（is_mine）；敌方点选走攻击命令，不进选择集。
        if (!o.selectable || !o.is_mine || o.selected) {
            continue;
        }
        if (!Cell_Revealed(static_cast<int>(o.x), static_cast<int>(o.y))) {
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
    if (o == nullptr || !o->is_mine) {
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
        if (o.selectable && o.is_mine && !o.selected && o.type == want) {
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
        if (!o.selectable || !o.is_mine) {
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
        if (!o.selectable || !o.is_mine) {
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
    return o.selected && o.selectable && o.is_mine && o.hp > 0 &&
           o.mission != Mission::Sleep && o.mission != Mission::Sticky;
}

int World::Order_Move(float x, float y) {
    int n = 0;
    for (Object& o : objects_) {
        if (!Can_Order(o) || o.speed <= 0.0f) {
            continue;
        }
        o.mission = Mission::Move;
        Follow_Path(o, x, y);
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
    Object* tgt = Find(target_id);
    if (tgt == nullptr || tgt->hp <= 0) {
        return 0;
    }
    int n = 0;
    for (Object& o : objects_) {
        if (!Can_Order(o) || o.damage <= 0) {
            continue;
        }
        if (Is_Ally(o.house, tgt->house)) {
            continue;
        }
        o.mission = Mission::Attack;
        o.target = target_id;
        o.has_dest = false;
        o.path.clear();
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
        o.mission = Mission::Guard;
        o.has_dest = false;
        o.path.clear();
        o.path_i = 0;
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
        if (!Can_Order(o) || o.deploys_into.empty()) {
            continue;
        }
        o.mission = Mission::Deploy;
        o.has_dest = false;
        ++n;
    }
    return n;
}

int World::Order_Harvest() {
    int n = 0;
    for (Object& o : objects_) {
        if (!Can_Order(o) || !o.harvester) {
            continue;
        }
        o.mission = Mission::Harvest;
        o.has_dest = false;
        o.target = -1;
        ++n;
    }
    return n;
}

void World::Tick_Hunt(Object& o, float dt) {
    // All_To_Hunt → Mission::Hunt(0xF)：追打最近敌人；进射程开火。
    if (o.damage > 0 && o.range > 0.0f) {
        const int eid = Find_Enemy_In_Range(o, o.range);
        if (eid >= 0) {
            o.target = eid;
            Tick_Combat(o, dt);
            return;
        }
    }
    const int near = Find_Nearest_Enemy(o);
    if (near < 0) {
        o.has_dest = false;
        return;
    }
    const Object* t = Find(near);
    if (t == nullptr) {
        return;
    }
    o.target = near;
    Follow_Path(o, t->x, t->y);
    Move_Toward(o, dt);
}

bool World::Is_Base_Unit(const Object& o) const {
    if (models_ == nullptr || o.kind != MapObjectKind::Unit || o.hp <= 0) {
        return false;
    }
    for (const std::string& b : models_->Base_Units()) {
        if (_stricmp(o.type.c_str(), b.c_str()) == 0) {
            return true;
        }
    }
    return false;
}

int World::Count_House_Buildings(int house) const {
    int n = 0;
    for (const Object& o : objects_) {
        if (o.house == house && o.kind == MapObjectKind::Building && o.hp > 0) {
            ++n;
        }
    }
    return n;
}

int World::Count_House_Base_Units(int house) const {
    int n = 0;
    for (const Object& o : objects_) {
        if (o.house == house && Is_Base_Unit(o)) {
            ++n;
        }
    }
    return n;
}

int World::Count_House_Technos(int house) const {
    int n = 0;
    for (const Object& o : objects_) {
        if (o.house != house || o.hp <= 0 || !o.Is_Techno()) {
            continue;
        }
        ++n;
    }
    return n;
}

bool World::House_Is_Defeated(int house) const {
    if (house < 0 || house >= static_cast<int>(houses_.size())) {
        return true;
    }
    const std::string& hn = houses_[static_cast<size_t>(house)];
    if (hn == "Neutral" || hn == "Special") {
        return false;
    }
    // 未入局空席（无 MCV/单位）不按 ShortGame 判负，否则联机图会秒胜。
    if (static_cast<size_t>(house) >= house_active_.size() ||
        !house_active_[static_cast<size_t>(house)]) {
        return false;
    }
    if (models_ != nullptr && models_->Short_Game()) {
        // 0x004F8EC6：building_count==0 且 BaseUnit 计数==0 → 败。
        return Count_House_Buildings(house) <= 0 &&
               Count_House_Base_Units(house) <= 0;
    }
    // 0x004F8F21：建筑 + 载具 + 步兵 + 飞机 计数和 == 0。
    return Count_House_Technos(house) <= 0;
}

bool World::House_Defeated_Flag(int house) const {
    if (house < 0 || house >= static_cast<int>(house_defeated_.size())) {
        return false;
    }
    return house_defeated_[static_cast<size_t>(house)] != 0;
}

void World::Flag_To_Win() {
    // 0x004FC9E0：house+0x1f7 = 1（本机结局）。
    if (outcome_ != MatchOutcome::Playing) {
        return;
    }
    outcome_ = MatchOutcome::Won;
    std::printf("  Flag_To_Win (TXT_SCENARIO_WON)\n");
}

void World::Flag_To_Lose() {
    // 0x004FCBD0：house+0x1f8 = 1，清 0x1f7。
    if (outcome_ != MatchOutcome::Playing) {
        return;
    }
    outcome_ = MatchOutcome::Lost;
    std::printf("  Flag_To_Lose (TXT_SCENARIO_LOST)\n");
}

void World::Force_House_IQ(int house, int iq) {
    if (house < 0 || house >= static_cast<int>(house_iq_.size())) {
        return;
    }
    int v = iq;
    if (models_ != nullptr) {
        const int mx = models_->Max_IQ_Levels();
        if (v > mx) {
            v = mx;
        }
    }
    if (v < 0) {
        v = 0;
    }
    house_iq_[static_cast<size_t>(house)] = v;
}

void World::All_To_Hunt(int house) {
    // 0x00501400：Assign_Mission(0xF=Hunt)。
    for (Object& o : objects_) {
        if (o.house != house || o.hp <= 0 || !o.Is_Techno()) {
            continue;
        }
        if (o.kind == MapObjectKind::Building) {
            continue;
        }
        o.mission = Mission::Hunt;
        o.has_dest = false;
        o.target = -1;
    }
}

int World::Crowd_Cheer(int house) {
    // TAction 113 → 0x006DF848 → 0x50C8C0（Crowd Cheer）：
    //   0x0050C8C3  edi = *(int*)0xA8EC88   // TechnoClass 数组容量
    //   0x0050C8D1  eax = *(int*)0xA8EC7C   // TechnoClass 数组首址
    //   0x0050C8DD  if (t->house(+0x21C) != ebx) skip
    //   0x0050C8E9  call dword ptr [t->vt + 0x388]    // TechnoClass::Crowd_Cheer(arg=0)
    //   末尾调 0x750920 Play_Voc（0x8871E0[+0x1C8] 词条）
    // 0x0050C8F4  eax = *(int*)0x8871E0     // VocClass 数组首址
    //   0x0050C905  ecx = eax->voc_name(+0x1C8)
    //   0x0050C90B  call 0x750920(0, 1.0f, 0, 0x2000)
    //
    // 严格逆向：原版 vtable+0x388 是 TechnoClass::Crowd_Cheer()，
    // 真正表现是"短暂欢呼动作"。我们没有 AnimClass 注入路径，
    // 所以**只数这间房屋会受影响的 Techno 个数** —— 与原版扫描到的集合对齐，
    // 不发明动作。
    int affected = 0;
    for (const Object& o : objects_) {
        if (o.house != house || o.hp <= 0) {
            continue;
        }
        if (o.kind == MapObjectKind::Building) {
            continue;  // 原版同样跳建筑（建筑不动）
        }
        if (!o.Is_Techno()) {
            continue;
        }
        ++affected;
    }
    return affected;
}

void World::Add_Team_Type(MapTeamType tt) {
    if (tt.id.empty()) {
        return;
    }
    for (MapTeamType& e : team_types_) {
        if (_stricmp(e.id.c_str(), tt.id.c_str()) == 0) {
            e = std::move(tt);
            return;
        }
    }
    team_types_.push_back(std::move(tt));
}

const MapTeamType* World::Find_Team_Type(const char* id) const {
    if (id == nullptr || id[0] == '\0') {
        return nullptr;
    }
    for (const MapTeamType& tt : team_types_) {
        if (_stricmp(tt.id.c_str(), id) == 0) {
            return &tt;
        }
        if (!tt.name.empty() && _stricmp(tt.name.c_str(), id) == 0) {
            return &tt;
        }
    }
    return nullptr;
}

int World::Resolve_Team_House(const MapTeamType& tt, int fallback_house) const {
    // TeamType::Get_House @0x6F2070：+0xC4 House* 优先；否则特殊 index；否则空。
    // House=<none>/空 → 回退到触发器房屋（TAction 传入的 house）。
    if (!tt.house.empty() && _stricmp(tt.house.c_str(), "<none>") != 0) {
        for (size_t i = 0; i < houses_.size(); ++i) {
            if (_stricmp(houses_[i].c_str(), tt.house.c_str()) == 0) {
                return static_cast<int>(i);
            }
        }
    }
    return fallback_house;
}

MapObjectKind World::Kind_For_Type(const char* type) const {
    if (models_ == nullptr || type == nullptr) {
        return MapObjectKind::Unit;
    }
    const UnitModel* um = models_->Resolve(type);
    if (um == nullptr) {
        return MapObjectKind::Unit;
    }
    // UnitModelDB：InfantryTypes→2，BuildingTypes→0/1，Vehicle/Aircraft→3。
    if (um->category == 2) {
        return MapObjectKind::Infantry;
    }
    if (um->category == 0 || um->category == 1) {
        return MapObjectKind::Building;
    }
    return MapObjectKind::Unit;
}

int World::Spawn_Team_Type(const MapTeamType& tt, int house) {
    // Reinforce 终态 / Create_One_Of 投放：TaskForce 成员落到 Waypoint 格。
    // 完整 PDPLANE 空投路径（0x65D8E0 中段）未接；此处对齐到格投放。
    float x = 40.0f, y = 40.0f;
    bool have = false;
    if (tt.waypoint >= 0) {
        have = Spawn_Cell(tt.waypoint, &x, &y);
    }
    if (!have) {
        // TeamType+0xD4==-1 时 0x6F18A0 退到默认 cell；用座位 0。
        have = Spawn_Cell(0, &x, &y);
    }
    if (!have && !objects_.empty()) {
        x = objects_[0].x;
        y = objects_[0].y;
        have = true;
    }
    if (!have) {
        return 0;
    }
    int spawned = 0;
    int slot = 0;
    for (const MapTaskForceEntry& e : tt.members) {
        const MapObjectKind kind = Kind_For_Type(e.type.c_str());
        for (int n = 0; n < e.count; ++n) {
            const float ox = x + static_cast<float>((slot % 3) - 1) * 0.35f;
            const float oy = y + static_cast<float>((slot / 3) % 3 - 1) * 0.35f;
            const int id = Spawn(e.type.c_str(), kind, house, ox, oy);
            if (id >= 0) {
                ++spawned;
            }
            ++slot;
        }
    }
    return spawned;
}

bool World::Dispatch_TAction(int action, int house, int param1,
                             const char* voc_name, const char* link_id) {
    // TActionClass::Execute jmp [ (action-1)*4 + 0x6DFDEC ]
    switch (action) {
        case 1:  // Win @0x006DEA37
            if (house == player_house_) {
                Flag_To_Win();
            } else {
                Flag_To_Lose();
            }
            return true;
        case 2:  // Lose @0x006DEA67 → 相反房屋检查后 Flag
            if (house == player_house_) {
                Flag_To_Lose();
            } else {
                Flag_To_Win();
            }
            return true;
        case 3:  // @0x006DEA97：House+0x1EE = 1；与 Production IQ 门控同源旗
            if (house >= 0 && house < static_cast<int>(house_iq_.size())) {
                const int need =
                    (models_ != nullptr) ? models_->IQ_Production() : 1;
                if (house_iq_[static_cast<size_t>(house)] < need) {
                    house_iq_[static_cast<size_t>(house)] = need;
                }
            }
            return true;
        case 4: {  // Create Team @0x6DEB57 → 0x6F09C0 Create_One_Of
            // TAction+0x30 = TeamType*；link_id = TeamType ID 串。
            const char* tid =
                (link_id != nullptr && link_id[0] != '\0') ? link_id : voc_name;
            const MapTeamType* tt = Find_Team_Type(tid);
            if (tt == nullptr) {
                std::printf("  TAction4 CreateTeam missing id=%s\n",
                            tid ? tid : "?");
                return true;  // 原版空指针则跳过仍返回 true
            }
            const int owner = Resolve_Team_House(*tt, house);
            const int n = Spawn_Team_Type(*tt, owner);
            std::printf("  TAction4 CreateTeam id=%s name=%s house=%d spawned=%d\n",
                        tt->id.c_str(), tt->name.c_str(), owner, n);
            return true;
        }
        case 7: {  // Reinforcement @0x6DEBAE → 0x65D8E0(edx=-1)
            // edx==-1：Waypoint 取 TeamType+0xD4（0x6F18A0），非 Actions WP。
            const char* tid =
                (link_id != nullptr && link_id[0] != '\0') ? link_id : voc_name;
            const MapTeamType* tt = Find_Team_Type(tid);
            if (tt == nullptr) {
                std::printf("  TAction7 Reinforce missing id=%s\n",
                            tid ? tid : "?");
                return true;
            }
            const int owner = Resolve_Team_House(*tt, house);
            const int n = Spawn_Team_Type(*tt, owner);
            std::printf("  TAction7 Reinforce id=%s wp=%d house=%d spawned=%d\n",
                        tt->id.c_str(), tt->waypoint, owner, n);
            return true;
        }
        case 6:  // AllToHunt：table[5]→0x6e45e0 取 House 后 vcall；语义对齐 All_To_Hunt
            All_To_Hunt(house);
            return true;
        case 9:  // @0x006DEAD6：House+0x250 = 4
            // 具体 AI 枚举名未在字符串侧钉死；写侧已证。
            return true;
        case 42:  // @0x006DECC2 → 0x6E2390：waypoint 上 Overlay/Smudge（类型表 0x88756C[param]）
            ++sound_play_count_;
            std::printf("  Overlay stub action=42 param=%d\n", param1);
            return true;
        case 12:  // @0x006DF051 → 0x726720：目标 Trigger+0x30 = 1（弹簧闩）
            if (link_id != nullptr && link_id[0] != '\0') {
                for (MapTrigger& t : triggers_) {
                    if (_stricmp(t.id.c_str(), link_id) == 0) {
                        t.fired = true;
                        break;
                    }
                }
            }
            return true;
        case 13:  // @0x006DEB18：House+0x1EF = 1（与 +0x1EE 成对）
            if (house >= 0 && house < static_cast<int>(house_iq_.size())) {
                const int need =
                    (models_ != nullptr) ? models_->IQ_Production() : 1;
                if (house_iq_[static_cast<size_t>(house)] < need) {
                    house_iq_[static_cast<size_t>(house)] = need;
                }
            }
            return true;
        case 15:  // empty @0x6DFDDD
        case 35:  // empty
        case 39:  // empty @0x6DFDDB
            return true;
        case 16:  // Map shroud reveal sweep @0x006DE3A8
            Reveal_Map();
            return true;
        case 17:  // Reveal around waypoint @0x006DE… → 0x6E0FE0
        case 18: {  // Reveal waypoint area (alt) → 0x6E11C0
            float wx = 0.0f, wy = 0.0f;
            if (Spawn_Cell(param1, &wx, &wy)) {
                // 半径未在本任务钉死 rules 缺省；用 10 格与常见单位 Sight 同量级。
                Reveal_Around(wx, wy, 10);
            }
            return true;
        }
        case 23:  // Timer start if idle @0x006DE418
            if (mission_timer_start_ < 0) {
                mission_timer_start_ = logic_frame_;
                if (mission_timer_dur_ <= 0) {
                    mission_timer_dur_ = param1 > 0 ? param1 * 15 : 0;
                }
            }
            return true;
        case 24:  // Timer stop @0x006DE457
            mission_timer_start_ = -1;
            return true;
        case 25:  // Timer extend @0x006DE4A0：param*15 加到 dur
            if (mission_timer_start_ < 0) {
                mission_timer_start_ = logic_frame_;
                mission_timer_dur_ = 0;
            }
            mission_timer_dur_ += (param1 > 0 ? param1 * 15 : 0);
            return true;
        case 26: {  // Timer shorten @0x006DE501
            const int cut = param1 > 0 ? param1 * 15 : 0;
            if (mission_timer_dur_ > cut) {
                mission_timer_dur_ -= cut;
            } else {
                mission_timer_dur_ = 0;
            }
            return true;
        }
        case 27:  // Timer set @0x006DE5C9
            mission_timer_start_ = logic_frame_;
            mission_timer_dur_ = param1 > 0 ? param1 * 15 : 0;
            return true;
        case 28:  // Set Global = 1 @0x006DE2CE → 0x689670(index, 1)
            if (param1 >= 0 && param1 < 50) {
                global_vars_[param1] = 1;
            }
            return true;
        case 29:  // Set Global = 0 @0x006DE2F1 → 0x689670(index, 0)
            if (param1 >= 0 && param1 < 50) {
                global_vars_[param1] = 0;
            }
            return true;
        case 30:  // @0x006DE206：House+0x1F3 = 1（Unload 门控；Production IQ 同源）
            if (house >= 0 && house < static_cast<int>(house_iq_.size())) {
                const int need =
                    (models_ != nullptr) ? models_->IQ_Production() : 1;
                if (house_iq_[static_cast<size_t>(house)] < need) {
                    house_iq_[static_cast<size_t>(house)] = need;
                }
            }
            return true;
        case 37:  // Make Ally @0x006DE136 → 双边 0x4F9B70
            Make_Ally(house, param1);
            Make_Ally(param1, house);
            return true;
        case 38:  // Make Enemy @0x006DE189 → 双边 0x4F9F90
            Break_Ally(house, param1);
            Break_Ally(param1, house);
            return true;
        case 53:  // Enable Trigger @0x006DF137 → 0x7268F0：+0x44=1 并 0x726400 重装
            if (link_id != nullptr && link_id[0] != '\0') {
                for (MapTrigger& t : triggers_) {
                    if (_stricmp(t.id.c_str(), link_id) == 0) {
                        t.enabled = true;
                        t.fired = false;
                        Arm_Trigger_Timers(t);
                        std::printf("  TAction53 Enable id=%s\n", link_id);
                        break;
                    }
                }
            }
            return true;
        case 54:  // Disable Trigger @0x006DF164 → 0x726900：+0x44=0
            if (link_id != nullptr && link_id[0] != '\0') {
                for (MapTrigger& t : triggers_) {
                    if (_stricmp(t.id.c_str(), link_id) == 0) {
                        t.enabled = false;
                        std::printf("  TAction54 Disable id=%s\n", link_id);
                        break;
                    }
                }
            }
            return true;
        case 56:  // Set Local = 1 @0x006DF1DE → 0x689910(index, 1)
            if (param1 >= 0 && param1 < 100) {
                local_vars_[param1] = 1;
            }
            return true;
        case 57:  // Set Local = 0 @0x006DF201 → 0x689910(index, 0)
            if (param1 >= 0 && param1 < 100) {
                local_vars_[param1] = 0;
            }
            return true;
        case 67:  // 无条件 Flag_To_Win @0x006DDD77 → Player 0x4FC9E0
            Flag_To_Win();
            return true;
        case 68:  // 无条件 Flag_To_Lose @0x006DDD93 → Player 0x4FCBD0
            Flag_To_Lose();
            return true;
        case 69:  // @0x006DDDAF → 0x4FCDC0：未终局则 Flag_To_Win(0)
            Flag_To_Win();
            return true;
        case 19:  // Play Voc @0x006DE780 → 0x750920
        case 20:  // QueueSong
        case 21:  // EVA speech
        case 99:  // 环境音 @0x006DE845（waypoint + Voc）
        case 108: // @0x006DF69B（cell + 音效）
        case 113: // @0x006DF848 → 0x50C8C0：房屋 techno 扫描 + Voc（Crowd Cheer）
            // 音频设备未接：对齐原版认领 action，记录 Voc 名供验证（不发明混音）。
            // TAction 113 多走一步：原版 0x50C8C0 先扫描该阵营的 Techno，
            // 我们用 Crowd_Cheer 复现这个扫描（仅数个），然后再走音频认领。
            if (action == 113) {
                const int n = Crowd_Cheer(house);
                std::printf("  Crowd_Cheer house=%d techno=%d\n", house, n);
            }
            ++sound_play_count_;
            if (voc_name != nullptr && voc_name[0] != '\0') {
                std::printf("  Sound stub action=%d voc=%s\n", action, voc_name);
                if (sound_player_ != nullptr) {
                    sound_player_(voc_name);
                }
            } else {
                std::printf("  Sound stub action=%d param=%d\n", action, param1);
            }
            return true;
        case 102: // @0x006DD9EC：waypoint 格上 Anim（风暴闪电路径；Anim 类型未钉名前认领）
            ++sound_play_count_;
            std::printf("  Anim stub action=102 param=%d\n", param1);
            return true;
        default:
            return false;
    }
}

void World::MPlayer_Defeated(int house) {
    // 0x004FC0B0：标 +0x1f5，再数剩余阵营；玩家未败 → Flag_To_Win，已败 → Flag_To_Lose。
    if (house < 0 || house >= static_cast<int>(house_defeated_.size())) {
        return;
    }
    if (house_defeated_[static_cast<size_t>(house)]) {
        return;
    }
    house_defeated_[static_cast<size_t>(house)] = 1;
    std::printf("  MPlayer_Defeated house=%d (%s)\n", house,
                houses_[static_cast<size_t>(house)].c_str());

    int living = 0;
    for (size_t i = 0; i < houses_.size(); ++i) {
        if (house_defeated_[i]) {
            continue;
        }
        if (i >= house_active_.size() || !house_active_[i]) {
            continue;
        }
        const std::string& hn = houses_[i];
        if (hn.empty() || hn == "Neutral" || hn == "Special") {
            continue;
        }
        if (House_Is_Defeated(static_cast<int>(i))) {
            continue;
        }
        ++living;
    }
    // 0x004FC580：剩余可战阵营压到 1 路时收束结局。
    if (living > 1) {
        return;
    }
    if (player_house_ >= 0 &&
        house_defeated_[static_cast<size_t>(player_house_)]) {
        Flag_To_Lose();
    } else {
        Flag_To_Win();
    }
}

void World::Tick_Defeat() {
    if (outcome_ != MatchOutcome::Playing) {
        return;
    }
    for (size_t i = 0; i < houses_.size(); ++i) {
        if (house_defeated_[i]) {
            continue;
        }
        if (i >= house_active_.size() || !house_active_[i]) {
            continue;
        }
        const std::string& hn = houses_[i];
        if (hn.empty() || hn == "Neutral" || hn == "Special") {
            continue;
        }
        if (House_Is_Defeated(static_cast<int>(i))) {
            MPlayer_Defeated(static_cast<int>(i));
            if (outcome_ != MatchOutcome::Playing) {
                return;
            }
        }
    }
}

bool World::Can_Place_Building(const char* type, float x, float y) const {
    // BuildingType::CanPlaceHere @0x00464AC0：
    //   +0x1703 PlaceAnywhere → true
    //   else foundation 循环 @0x00716150 → CellClass check @0x0047C620
    //   SpeedType==-1 时 @0x0047CA33 读 Land.Buildable（经 TMP→0x8288e4 映射后的 LandType）。
    // 邻近：0x004A8EB0，半径 Adjacent+1，需同 house 且 BaseNormal 建筑。
    if (type == nullptr || models_ == nullptr) {
        return false;
    }
    const UnitModel* um = models_->Resolve(type);
    if (um == nullptr) {
        return false;
    }
    if (um->place_anywhere) {
        return true;
    }
    const int fw = um->width > 0 ? um->width : 1;
    const int fh = um->height > 0 ? um->height : 1;
    const int ax = static_cast<int>(std::floor(x));
    const int ay = static_cast<int>(std::floor(y));
    const int house = player_house_;

    for (int dy = 0; dy < fh; ++dy) {
        for (int dx = 0; dx < fw; ++dx) {
            const int cx = ax + dx;
            const int cy = ay + dy;
            if (logic_map_ != nullptr) {
                const CellClass* cell = logic_map_->Cell_At(
                    CellStruct{static_cast<int16_t>(cx),
                               static_cast<int16_t>(cy)});
                if (cell == nullptr) {
                    return false;
                }
                if (!models_->Land_Buildable(static_cast<int>(cell->Land()))) {
                    return false;
                }
            }
            for (const Object& o : objects_) {
                if (o.hp <= 0 || o.kind != MapObjectKind::Building) {
                    continue;
                }
                int ow = 1, oh = 1;
                const UnitModel* ou = models_->Resolve(o.type.c_str());
                if (ou != nullptr) {
                    ow = ou->width > 0 ? ou->width : 1;
                    oh = ou->height > 0 ? ou->height : 1;
                }
                const int ox = static_cast<int>(std::floor(o.x));
                const int oy = static_cast<int>(std::floor(o.y));
                if (cx >= ox && cx < ox + ow && cy >= oy && cy < oy + oh) {
                    return false;
                }
            }
        }
    }

    // 0x004A8EB0：仅对本机玩家 house 强制；半径 = Adjacent+1。
    // foundation 外扩 rad 后与同 house、BaseNormal 建筑足迹相交则过。
    {
        const int rad = um->adjacent + 1;
        bool near = false;
        bool any_base = false;
        const int x0 = ax - rad;
        const int y0 = ay - rad;
        const int x1 = ax + fw + rad;
        const int y1 = ay + fh + rad;
        for (const Object& o : objects_) {
            if (o.hp <= 0 || o.kind != MapObjectKind::Building ||
                o.house != house) {
                continue;
            }
            const UnitModel* ou = models_->Resolve(o.type.c_str());
            if (ou == nullptr || !ou->base_normal) {
                continue;
            }
            any_base = true;
            const int ow = ou->width > 0 ? ou->width : 1;
            const int oh = ou->height > 0 ? ou->height : 1;
            const int ox = static_cast<int>(std::floor(o.x));
            const int oy = static_cast<int>(std::floor(o.y));
            if (ox < x1 && ox + ow > x0 && oy < y1 && oy + oh > y0) {
                near = true;
                break;
            }
        }
        // 尚无 BaseNormal 建筑（开局第一座 CY）：邻近链直接放行。
        if (any_base && !near) {
            return false;
        }
    }
    return true;
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
