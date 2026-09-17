// TypeDB.h -- P2 数据层：把 rules / art / sound / theme 全量装进类型表。
//
// UnitModelDB 只做到"模型组成"那一跳（哪个单位由哪几个 VXL 拼）。这一层往下走：
// **一个 TechnoType 的每一个 INI 键都要能被取出来**，而且取出来的值必须和
// 原文件逐字节一致。
//
// 【为什么是"类型化字段 + 原始键值表"双轨】
// 实测 rules 的单位段里有 **477 种不同的键**、art 的 Image 段有 **206 种**。
// 全部手写成结构体字段有两个问题：
//   1. 写错一个键名不会报错，只会静默取到缺省值 —— 这类 bug 最难发现；
//   2. 键是游戏数据的一部分，改版补丁会加键，写死的结构体接不住。
// 所以：
//   * `rules` / `art` 两个 ValueMap 存**全量**键值 —— 一个键都不丢，这是硬保证；
//   * 结构体字段是给类型化访问用的**视图**，字段与键的对应关系用 X-macro 声明，
//     声明一次同时生成"成员"和"填充代码"，两边不可能分叉。
//
// 【验收】（`ra2core.exe --typetable <mix...>`）
//   对 559 个单位的 rules 段与 art 段逐键对账：
//     * 段的**去重键数** == ValueMap 的 size（不许丢键、不许添键）
//     * 每个键的**值逐字节相等**（不许改值）
//   任一条不成立就报错，并把第一个不一致的 (单位, 来源, 键) 打出来。
//
// 【键名从哪来】不是查资料抄的，是 tools/inikeys.py 把四个真实 INI 扫了一遍
// 数出来的（输出留在 build/_inikeys.txt）。本文件里的每个键都能在那里找到。
// 语义没逆向出来的键，字段名就直接用键名的小写形式，注释只写"rules <键>="——
// 不做超出证据的断言（纪律见 docs/fabrication-audit.md）。

#pragma once

#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "data/Ini.h"

namespace ra2 {

class MixFileClass;
class UnitModelDB;

// ---------------------------------------------------------------------------
// ValueMap：大小写不敏感、**保序**、首个出现者胜的键值表。
// ---------------------------------------------------------------------------

/// 为什么是"首个出现者胜"而不是"末个"：`IniFile::Get_String` 的语义就是返回
/// 第一个同名键，两边必须一致，否则对账会莫名其妙地红。
class ValueMap {
public:
    void Clear() {
        items_.clear();
        index_.clear();
    }

    /// 已存在的键**不动**（保留第一次的值与位置）。
    void Add(const std::string& key, const std::string& value);

    size_t Size() const noexcept { return items_.size(); }
    bool Empty() const noexcept { return items_.empty(); }

    const std::string& Key(size_t i) const { return items_[i].first; }
    const std::string& Value(size_t i) const { return items_[i].second; }

    /// 找不到返回 nullptr（不是空串 —— 空串与"没有这个键"是两回事）。
    const std::string* Find(const char* key) const;
    bool Has(const char* key) const { return Find(key) != nullptr; }

    // ---- 类型化取值。语义与 IniFile::Get_* 完全一致 ----
    std::string Get(const char* key, const char* def = "") const;
    int Get_Int(const char* key, int def = 0) const;
    double Get_Double(const char* key, double def = 0.0) const;
    bool Get_Bool(const char* key, bool def = false) const;
    int Get_Int_List(const char* key, std::vector<int>* out) const;
    int Get_String_List(const char* key, std::vector<std::string>* out) const;

private:
    std::vector<std::pair<std::string, std::string>> items_;
    std::unordered_map<std::string, size_t> index_;   ///< 大写键 -> items_ 下标
};

// ---------------------------------------------------------------------------
// TechnoType 的字段清单（X-macro）
//
//   X(字段名, 来源, "INI 键", 类型, 缺省)
//
// 来源：'R' = rules(md).ini 的 [单位] 段；'A' = art(md).ini 的 [Image] 段。
// 类型：INT / DBL / BOOL / STR / ILIST / SLIST。
//
// 【缺省值从哪来】没写缺省的键，字段缺省 = 0/空 —— 表示"原文件没写这一项"。
// 这**不等于**引擎的运行时缺省（那在 C++ 构造函数里，是另一件事）。
// 两层缺省混在一起是最容易出错的，所以这里刻意只表达"文件里有没有"。
// ---------------------------------------------------------------------------

#define RA2_TECHNO_FIELDS(X)                                                          \
    /* ---- 身份 / 侧栏 / 经济 ---- */                                                \
    X(ui_name,        'R', "UIName",                     STR,   "")                   \
    X(cost,           'R', "Cost",                       INT,   0)                    \
    X(soylent,        'R', "Soylent",                    INT,   0)                    \
    X(tech_level,     'R', "TechLevel",                  INT,   0)                    \
    X(points,         'R', "Points",                     INT,   0)                    \
    X(build_cat,      'R', "BuildCat",                   STR,   "")                   \
    X(build_limit,    'R', "BuildLimit",                 INT,   0)                    \
    X(category_ini,   'R', "Category",                   STR,   "")                   \
    X(size,           'R', "Size",                       INT,   0)                    \
    X(physical_size,  'R', "PhysicalSize",               INT,   0)                    \
    X(weight,         'R', "Weight",                     DBL,   0.0)                  \
    X(unsellable,     'R', "Unsellable",                 BOOL,  false)                \
    X(dont_score,     'R', "DontScore",                  BOOL,  false)                \
    X(crate_goodie,   'R', "CrateGoodie",                BOOL,  false)                \
    X(carries_crate,  'R', "CarriesCrate",               BOOL,  false)                \
    X(insignificant,  'R', "Insignificant",              BOOL,  false)                \
    X(nominal,        'R', "Nominal",                    BOOL,  false)                \
    X(selectable,     'R', "Selectable",                 BOOL,  false)                \
    X(is_selectable_combatant, 'R', "IsSelectableCombatant", BOOL, false)             \
    X(trainable,      'R', "Trainable",                  BOOL,  false)                \
    X(veteran_abilities,  'R', "VeteranAbilities",       STR,   "")                   \
    X(elite_abilities,    'R', "EliteAbilities",         STR,   "")                   \
    X(repairable,     'R', "Repairable",                 BOOL,  false)                \
    X(click_repairable, 'R', "ClickRepairable",          BOOL,  false)                \
    X(unit_repair,    'R', "UnitRepair",                 BOOL,  false)                \
    X(unit_reload,    'R', "UnitReload",                 BOOL,  false)                \
    X(unit_absorb,    'R', "UnitAbsorb",                 BOOL,  false)                \
    X(infantry_absorb, 'R', "InfantryAbsorb",            BOOL,  false)                \
    X(owner,          'R', "Owner",                      SLIST, )                     \
    X(required_houses, 'R', "RequiredHouses",            SLIST, )                     \
    X(forbidden_houses, 'R', "ForbiddenHouses",          SLIST, )                     \
    X(secret_houses,  'R', "SecretHouses",               SLIST, )                     \
    X(requires_stolen_soviet_tech, 'R', "RequiresStolenSovietTech", BOOL, false)      \
    X(requires_stolen_allied_tech, 'R', "RequiresStolenAlliedTech", BOOL, false)      \
    X(requires_stolen_third_tech,  'R', "RequiresStolenThirdTech",  BOOL, false)      \
    X(prerequisite,   'R', "Prerequisite",               SLIST, )                     \
    X(prerequisite_override, 'R', "PrerequisiteOverride", SLIST, )                    \
    X(upgrades,       'R', "Upgrades",                   SLIST, )                     \
                                                                                      \
    /* ---- 物理 / 防御 ---- */                                                       \
    X(strength,       'R', "Strength",                   INT,   0)                    \
    X(armor,          'R', "Armor",                      STR,   "")                   \
    X(immune,         'R', "Immune",                     BOOL,  false)                \
    X(immune_to_veins, 'R', "ImmuneToVeins",             BOOL,  false)                \
    X(immune_to_radiation, 'R', "ImmuneToRadiation",     BOOL,  false)                \
    X(is_immune_to_radiation, 'R', "IsImmuneToRadiation", BOOL, false)                \
    X(immune_to_psionics, 'R', "ImmuneToPsionics",       BOOL,  false)                \
    X(immune_to_psionic_weapons, 'R', "ImmuneToPsionicWeapons", BOOL, false)          \
    X(immune_to_poison, 'R', "ImmuneToPoison",           BOOL,  false)                \
    X(tiberium_proof, 'R', "TiberiumProof",              BOOL,  false)                \
    X(self_healing,   'R', "SelfHealing",                BOOL,  false)                \
    X(infantry_gain_self_heal, 'R', "InfantryGainSelfHeal", BOOL, false)              \
    X(units_gain_self_heal, 'R', "UnitsGainSelfHeal",    BOOL,  false)                \
    X(damage_self,    'R', "DamageSelf",                 BOOL,  false)                \
    X(natural,        'R', "Natural",                    BOOL,  false)                \
    X(unnatural,      'R', "Unnatural",                  BOOL,  false)                \
    X(organic,        'R', "Organic",                    BOOL,  false)                \
    X(fearless,       'R', "Fearless",                   BOOL,  false)                \
    X(crushable,      'R', "Crushable",                  BOOL,  false)                \
    X(crusher,        'R', "Crusher",                    BOOL,  false)                \
    X(omni_crusher,   'R', "OmniCrusher",                BOOL,  false)                \
    X(omni_crush_resistant, 'R', "OmniCrushResistant",   BOOL,  false)                \
    X(deployed_crushable, 'R', "DeployedCrushable",      BOOL,  false)                \
    X(crush_sound,    'R', "CrushSound",                 STR,   "")                   \
    X(omnifire,       'R', "OmniFire",                   BOOL,  false)                \
    X(berserk_friendly, 'R', "BerserkFriendly",          BOOL,  false)                \
    X(damage_particle_systems, 'R', "DamageParticleSystems", STR, "")                 \
    X(damage_smoke_offset, 'R', "DamageSmokeOffset",     ILIST, )                     \
    X(half_damage_smoke_location1, 'R', "HalfDamageSmokeLocation1", ILIST, )          \
    X(natural_smoke_location, 'R', "NaturalSmokeLocation", ILIST, )                   \
                                                                                      \
    /* ---- 运动 ---- */                                                              \
    X(speed,          'R', "Speed",                      INT,   0)                    \
    X(sight,          'R', "Sight",                      INT,   0)                    \
    X(rot,            'R', "Rot",                        INT,   0)                    \
    X(turret,         'R', "Turret",                     BOOL,  false)                \
    X(locomotor,      'R', "Locomotor",                  STR,   "")                   \
    X(speed_type,     'R', "SpeedType",                  STR,   "")                   \
    X(movement_zone,  'R', "MovementZone",               STR,   "")                   \
    X(movement_restricted_to, 'R', "MovementRestrictedTo", SLIST, )                   \
    X(naval,          'R', "Naval",                      BOOL,  false)                \
    X(water_bound,    'R', "WaterBound",                 BOOL,  false)                \
    X(underwater,     'R', "Underwater",                 BOOL,  false)                \
    X(landable,       'R', "Landable",                   BOOL,  false)                \
    X(considered_aircraft, 'R', "ConsideredAircraft",    BOOL,  false)                \
    X(airport_bound,  'R', "AirportBound",               BOOL,  false)                \
    X(fighter,        'R', "Fighter",                    BOOL,  false)                \
    X(can_passive_acquire, 'R', "CanPassiveAquire",      BOOL,  false)                \
    X(accelerates,    'R', "Accelerates",                BOOL,  false)                \
    X(acceleration_factor, 'R', "AccelerationFactor",    DBL,   0.0)                  \
    X(deacceleration_factor, 'R', "DeaccelerationFactor", DBL,  0.0)                  \
    X(pitch_angle,    'R', "PitchAngle",                 DBL,   0.0)                  \
    X(pitch_speed,    'R', "PitchSpeed",                 DBL,   0.0)                  \
    X(fly_by,         'R', "FlyBy",                      BOOL,  false)                \
    X(fly_back,       'R', "FlyBack",                    BOOL,  false)                \
    X(too_big_to_fit_under_bridge, 'R', "TooBigToFitUnderBridge", BOOL, false)        \
    X(bunkerable,     'R', "Bunkerable",                 BOOL,  false)                \
    X(balloon_hover,  'R', "BalloonHover",               BOOL,  false)                \
    X(hover_attack,   'R', "HoverAttack",                BOOL,  false)                \
    X(jumpjet,        'R', "JumpJet",                    BOOL,  false)                \
    X(jumpjet_speed,  'R', "JumpJetSpeed",               INT,   0)                    \
    X(jumpjet_height, 'R', "JumpJetHeight",              INT,   0)                    \
    X(jumpjet_accel,  'R', "JumpJetAccel",               INT,   0)                    \
    X(jumpjet_turn_rate, 'R', "JumpJetTurnRate",         INT,   0)                    \
    X(jumpjet_turn,   'R', "JumpJetTurn",                BOOL,  false)                \
    X(jumpjet_wobbles, 'R', "JumpJetWobbles",            DBL,   0.0)                  \
    X(jumpjet_no_wobbles, 'R', "JumpJetNoWobbles",       BOOL,  false)                \
    X(jumpjet_deviation, 'R', "JumpJetDeviation",        INT,   0)                    \
    X(jumpjet_climb,  'R', "JumpJetClimb",               INT,   0)                    \
    X(jumpjet_crash,  'R', "JumpJetCrash",               INT,   0)                    \
    X(tilt_crash_jumpjet, 'R', "TiltCrashJumpjet",       BOOL,  false)                \
    X(teleporter,     'R', "Teleporter",                 BOOL,  false)                \
    X(no_shadow,      'R', "NoShadow",                   BOOL,  false)                \
                                                                                      \
    /* ---- 武器（rules 段里的武器名；武器自己的数值在 WeaponType）---- */            \
    X(primary,        'R', "Primary",                    STR,   "")                   \
    X(secondary,      'R', "Secondary",                  STR,   "")                   \
    X(elite_primary,  'R', "ElitePrimary",               STR,   "")                   \
    X(elite_secondary, 'R', "EliteSecondary",            STR,   "")                   \
    X(weapon1,        'R', "Weapon1",                    STR,   "")                   \
    X(weapon2,        'R', "Weapon2",                    STR,   "")                   \
    X(weapon3,        'R', "Weapon3",                    STR,   "")                   \
    X(weapon4,        'R', "Weapon4",                    STR,   "")                   \
    X(weapon5,        'R', "Weapon5",                    STR,   "")                   \
    X(weapon_count,   'R', "WeaponCount",                INT,   0)                    \
    X(turret_count,   'R', "TurretCount",                INT,   0)                    \
    X(occupier,       'R', "Occupier",                   BOOL,  false)                \
    X(occupy_weapon,  'R', "OccupyWeapon",               STR,   "")                   \
    X(elite_occupy_weapon, 'R', "EliteOccupyWeapon",     STR,   "")                   \
    X(assaulter,      'R', "Assaulter",                  BOOL,  false)                \
    X(death_weapon,   'R', "DeathWeapon",                STR,   "")                   \
    X(death_weapon_damage_modifier, 'R', "DeathWeaponDamageModifier", DBL, 0.0)       \
    X(explodes,       'R', "Explodes",                   BOOL,  false)                \
    X(explosion,      'R', "Explosion",                  STR,   "")                   \
    X(fire_angle,     'R', "FireAngle",                  INT,   0)                    \
    X(opportunity_fire, 'R', "OpportunityFire",          BOOL,  false)                \
    X(can_retaliate,  'R', "CanRetaliate",               BOOL,  false)                \
    X(prevent_attack_move, 'R', "PreventAttackMove",     BOOL,  false)                \
    X(distributed_fire, 'R', "DistributedFire",          BOOL,  false)                \
    X(deploy_fire,    'R', "DeployFire",                 BOOL,  false)                \
    X(open_topped,    'R', "OpenTopped",                 BOOL,  false)                \
    X(open_transport_weapon, 'R', "OpenTransportWeapon", INT,   0)                    \
    X(ammo,           'R', "Ammo",                       INT,   0)                    \
    X(spawns,         'R', "Spawns",                     STR,   "")                   \
    X(spawns_number,  'R', "SpawnsNumber",               INT,   0)                    \
    X(spawn_regen_rate, 'R', "SpawnRegenRate",           INT,   0)                    \
    X(spawn_reload_rate, 'R', "SpawnReloadRate",         INT,   0)                    \
    X(spawned,        'R', "Spawned",                    BOOL,  false)                \
    X(no_spawn_alt,   'R', "NoSpawnAlt",                 BOOL,  false)                \
    X(missile_spawn,  'R', "MissileSpawn",               BOOL,  false)                \
    X(weapon_stages,  'R', "WeaponStages",               INT,   0)                    \
    X(stage1,         'R', "Stage1",                     INT,   0)                    \
    X(stage2,         'R', "Stage2",                     INT,   0)                    \
    X(stage3,         'R', "Stage3",                     INT,   0)                    \
    X(elite_stage1,   'R', "EliteStage1",                INT,   0)                    \
    X(elite_stage2,   'R', "EliteStage2",                INT,   0)                    \
    X(elite_stage3,   'R', "EliteStage3",                INT,   0)                    \
    X(rate_up,        'R', "RateUp",                     INT,   0)                    \
    X(rate_down,      'R', "RateDown",                   INT,   0)                    \
    X(is_gattling,    'R', "IsGattling",                 BOOL,  false)                \
    X(close_range,    'R', "CloseRange",                 INT,   0)                    \
    X(air_range_bonus, 'R', "AirRangeBonus",             INT,   0)                    \
    X(guard_range,    'R', "GuardRange",                 INT,   0)                    \
    X(legal_target,   'R', "LegalTarget",                BOOL,  false)                \
    X(reveal_to_all,  'R', "RevealToAll",                BOOL,  false)                \
    X(target_coord_offset, 'R', "TargetCoordOffset",     ILIST, )                     \
    X(target_laser,   'R', "TargetLaser",                BOOL,  false)                \
    X(passengers,     'R', "Passengers",                 INT,   0)                    \
    X(size_limit,     'R', "SizeLimit",                  INT,   0)                    \
    X(parasiteable,   'R', "Parasiteable",               BOOL,  false)                \
    X(enslaves,       'R', "Enslaves",                   STR,   "")                   \
    X(slaves_number,  'R', "SlavesNumber",               INT,   0)                    \
    X(slave_regen_rate, 'R', "SlaveRegenRate",           INT,   0)                    \
    X(slave_reload_rate, 'R', "SlaveReloadRate",         INT,   0)                    \
                                                                                      \
    /* ---- 建造 / 部署 ---- */                                                       \
    X(power,          'R', "Power",                      INT,   0)                    \
    X(powered,        'R', "Powered",                    BOOL,  false)                \
    X(powered_unit,   'R', "PoweredUnit",                BOOL,  false)                \
    X(toggle_power,   'R', "TogglePower",                BOOL,  false)                \
    X(extra_power,    'R', "ExtraPower",                 INT,   0)                    \
    X(powered_special, 'R', "PoweredSpecial",            BOOL,  false)                \
    X(powers_unit,    'R', "PowersUnit",                 BOOL,  false)                \
    X(overpowerable,  'R', "Overpowerable",              BOOL,  false)                \
    X(factory,        'R', "Factory",                    STR,   "")                   \
    X(weapons_factory, 'R', "WeaponsFactory",            BOOL,  false)                \
    X(factory_plant,  'R', "FactoryPlant",               BOOL,  false)                \
    X(cloning,        'R', "Cloning",                    BOOL,  false)                \
    X(gate,           'R', "Gate",                       BOOL,  false)                \
    X(gate_close_delay, 'R', "GateCloseDelay",           INT,   0)                    \
    X(deploys_into,   'R', "DeploysInto",                STR,   "")                   \
    X(undeploys_into, 'R', "UndeploysInto",              STR,   "")                   \
    X(deployer,       'R', "Deployer",                   BOOL,  false)                \
    X(is_simple_deployer, 'R', "IsSimpleDeployer",       BOOL,  false)                \
    X(deploy_time,    'R', "DeployTime",                 DBL,   0.0)                  \
    X(deploy_facing,  'R', "DeployFacing",               INT,   0)                    \
    X(deploy_to_land, 'R', "DeployToLand",               BOOL,  false)                \
    X(deploying_anim, 'R', "DeployingAnim",              STR,   "")                   \
    X(refinery,       'R', "Refinery",                   BOOL,  false)                \
    X(dock_unload,    'R', "DockUnload",                 BOOL,  false)                \
    X(dock,           'R', "Dock",                       SLIST, )                     \
    X(number_of_docks, 'R', "NumberOfDocks",             INT,   0)                    \
    X(unloading_class, 'R', "UnloadingClass",            STR,   "")                   \
    X(resource_gatherer, 'R', "ResourceGatherer",        BOOL,  false)                \
    X(resource_destination, 'R', "ResourceDestination",  BOOL,  false)                \
    X(harvester,      'R', "Harvester",                  BOOL,  false)                \
    X(storage,        'R', "Storage",                    INT,   0)                    \
    X(harvest_rate,   'R', "HarvestRate",                INT,   0)                    \
    X(ore_purifier,   'R', "OrePurifier",                BOOL,  false)                \
    X(refinery_smoke_offset_one, 'R', "RefinerySmokeOffsetOne",   ILIST, )            \
    X(refinery_smoke_offset_two, 'R', "RefinerySmokeOffsetTwo",   ILIST, )            \
    X(refinery_smoke_offset_three, 'R', "RefinerySmokeOffsetThree", ILIST, )          \
    X(refinery_smoke_offset_four, 'R', "RefinerySmokeOffsetFour", ILIST, )            \
    X(refinery_smoke_frames, 'R', "RefinerySmokeFrames", INT,   0)                    \
    X(refinery_smoke_particle_system, 'R', "RefinerySmokeParticleSystem", STR, "")    \
    X(refn_smoke_offset_one, 'R', "RefnSmokeOffsetOne",  ILIST, )                     \
    X(refn_smoke_offset_two, 'R', "RefnSmokeOffsetTwo",  ILIST, )                     \
    X(free_unit,      'R', "FreeUnit",                   STR,   "")                   \
    X(helipad,        'R', "Helipad",                    BOOL,  false)                \
    X(carryall,       'R', "Carryall",                   BOOL,  false)                \
    X(construction_yard, 'R', "ConstructionYard",        BOOL,  false)                \
    X(hospital,       'R', "Hospital",                   BOOL,  false)                \
    X(gdi_barracks,   'R', "GDIBarracks",                BOOL,  false)                \
    X(nod_barracks,   'R', "NodBarracks",                BOOL,  false)                \
    X(yuri_barracks,  'R', "YuriBarracks",               BOOL,  false)                \
                                                                                      \
    /* ---- 身份补充 / 残骸 / 占地 / 可被交互 ---- */                                  \
    X(ini_name,       'R', "Name",                       STR,   "")                   \
    X(rules_image,    'R', "Image",                      STR,   "")                   \
    X(max_debris,     'R', "MaxDebris",                  INT,   0)                    \
    X(min_debris,     'R', "MinDebris",                  INT,   0)                    \
    X(debris_types,   'R', "DebrisTypes",                SLIST, )                     \
    X(debris_maximums, 'R', "DebrisMaximums",            ILIST, )                     \
    X(debris_anims,   'R', "DebrisAnims",                SLIST, )                     \
    X(debris_anim,    'R', "DebrisAnim",                 STR,   "")                   \
    X(destroy_anim,   'R', "DestroyAnim",                STR,   "")                   \
    X(leave_rubble,   'R', "LeaveRubble",                BOOL,  false)                \
    X(radar_invisible, 'R', "RadarInvisible",            BOOL,  false)                \
    X(radar_visible,  'R', "RadarVisible",               BOOL,  false)                \
    X(radar,          'R', "Radar",                      BOOL,  false)                \
    X(invisible_in_game, 'R', "InvisibleInGame",         BOOL,  false)                \
    X(threat_posed,   'R', "ThreatPosed",                INT,   0)                    \
    X(special_threat_value, 'R', "SpecialThreatValue",   DBL,   0.0)                  \
    X(can_be_occupied, 'R', "CanBeOccupied",             BOOL,  false)                \
    X(can_occupy_fire, 'R', "CanOccupyFire",             BOOL,  false)                \
    X(max_number_occupants, 'R', "MaxNumberOccupants",   INT,   0)                    \
    X(adjacent,       'R', "Adjacent",                   INT,   0)                    \
    X(capturable,     'R', "Capturable",                 BOOL,  false)                \
    X(spyable,        'R', "Spyable",                    BOOL,  false)                \
    X(bombable,       'R', "Bombable",                   BOOL,  false)                \
    X(drainable,      'R', "Drainable",                  BOOL,  false)                \
    X(crate_beneath,  'R', "CrateBeneath",               BOOL,  false)                \
    X(crate_beneath_is_money, 'R', "CrateBeneathIsMoney", BOOL,  false)               \
    X(to_protect,     'R', "ToProtect",                  BOOL,  false)                \
    X(protect_with_wall, 'R', "ProtectWithWall",         BOOL,  false)                \
    X(number_impassable_rows, 'R', "NumberImpassableRows", INT, 0)                    \
    X(needs_engineer, 'R', "NeedsEngineer",              BOOL,  false)                \
    X(default_to_guard_area, 'R', "DefaultToGuardArea",  BOOL,  false)                \
    X(has_stupid_guard_mode, 'R', "HasStupidGuardMode",  BOOL,  false)                \
    X(is_base_defense, 'R', "IsBaseDefense",             BOOL,  false)                \
    X(wall,           'R', "Wall",                       BOOL,  false)                \
    X(crew,           'R', "Crewed",                     BOOL,  false)                \
    X(not_human,      'R', "NotHuman",                   BOOL,  false)                \
    X(civilian,       'R', "Civilian",                   BOOL,  false)                \
    X(fraidy_cat,     'R', "FraidyCat",                  BOOL,  false)                \
    X(move_to_shroud, 'R', "MoveToShroud",               BOOL,  false)                \
    X(use_own_name,   'R', "UseOwnName",                 BOOL,  false)                \
    X(crashable,      'R', "Crashable",                  BOOL,  false)                \
    X(detect_disguise, 'R', "DetectDisguise",            BOOL,  false)                \
    X(sensors,        'R', "Sensors",                    BOOL,  false)                \
    X(sensors_sight,  'R', "SensorsSight",               INT,   0)                    \
    X(leadership_rating, 'R', "LeadershipRating",        INT,   0)                    \
    X(anti_infantry_value, 'R', "AntiInfantryValue",     INT,   0)                    \
    X(anti_armor_value, 'R', "AntiArmorValue",           INT,   0)                    \
    X(anti_air_value, 'R', "AntiAirValue",               INT,   0)                    \
    X(land_targeting, 'R', "LandTargeting",              INT,   0)                    \
    X(naval_targeting, 'R', "NavalTargeting",            INT,   0)                    \
    X(ifv_mode,       'R', "IFVMode",                    INT,   0)                    \
    X(pip,            'R', "Pip",                        STR,   "")                   \
    X(pip_scale,      'R', "PipScale",                   INT,   0)                    \
    X(z_fudge_tunnel, 'R', "ZFudgeTunnel",               INT,   0)                    \
    X(z_fudge_column, 'R', "ZFudgeColumn",               INT,   0)                    \
    X(exit_coord,     'R', "ExitCoord",                  ILIST, )                     \
    X(ai_base_planning_side, 'R', "AIBasePlanningSide",  STR,   "")                   \
    X(ai_build_this,  'R', "AIBuildThis",                BOOL,  false)                \
    X(allowed_to_start_in_multiplayer, 'R', "AllowedToStartInMultiplayer", BOOL, false)\
    X(build_time_multiplier, 'R', "BuildTimeMultiplier", DBL,   0.0)                  \
    X(light_visibility, 'R', "LightVisibility",          INT,   0)                    \
    X(light_intensity, 'R', "LightIntensity",            DBL,   0.0)                  \
    X(light_red_tint, 'R', "LightRedTint",               DBL,   0.0)                  \
    X(light_green_tint, 'R', "LightGreenTint",           DBL,   0.0)                  \
    X(light_blue_tint, 'R', "LightBlueTint",             DBL,   0.0)                  \
    X(super_weapon,   'R', "SuperWeapon",                STR,   "")                   \
    X(turret_anim,    'R', "TurretAnim",                 STR,   "")                   \
    X(turret_anim_is_voxel, 'R', "TurretAnimIsVoxel",    BOOL,  false)                \
    X(turret_anim_x,  'R', "TurretAnimX",                INT,   0)                    \
    X(turret_anim_y,  'R', "TurretAnimY",                INT,   0)                    \
    X(turret_anim_zadjust, 'R', "TurretAnimZAdjust",     INT,   0)                    \
                                                                                      \
    /* ---- 声音（rules 段里的声音名；具体波形在 SoundDB）---- */                      \
    X(die_sound,      'R', "DieSound",                   STR,   "")                   \
    X(create_sound,   'R', "CreateSound",                STR,   "")                   \
    X(move_sound,     'R', "MoveSound",                  STR,   "")                   \
    X(damage_sound,   'R', "DamageSound",                STR,   "")                   \
    X(crashing_sound, 'R', "CrashingSound",              STR,   "")                   \
    X(sinking_sound,  'R', "SinkingSound",               STR,   "")                   \
    X(impact_land_sound, 'R', "ImpactLandSound",         STR,   "")                   \
    X(working_sound,  'R', "WorkingSound",               STR,   "")                   \
    X(not_working_sound, 'R', "NotWorkingSound",         STR,   "")                   \
    X(enter_transport_sound, 'R', "EnterTransportSound", STR,   "")                   \
    X(leave_transport_sound, 'R', "LeaveTransportSound", STR,   "")                   \
    X(ambient_sound,  'R', "AmbientSound",               STR,   "")                   \
    X(aux_sound1,     'R', "AuxSound1",                  STR,   "")                   \
    X(aux_sound2,     'R', "AuxSound2",                  STR,   "")                   \
    X(capture_eva_event, 'R', "CaptureEvaEvent",         STR,   "")                   \
    X(voice_select,   'R', "VoiceSelect",                STR,   "")                   \
    X(voice_move,     'R', "VoiceMove",                  STR,   "")                   \
    X(voice_attack,   'R', "VoiceAttack",                STR,   "")                   \
    X(voice_feedback, 'R', "VoiceFeedback",              STR,   "")                   \
    X(voice_special_attack, 'R', "VoiceSpecialAttack",   STR,   "")                   \
    X(voice_crashing, 'R', "VoiceCrashing",              STR,   "")                   \
    X(voice_enter,    'R', "VoiceEnter",                 STR,   "")                   \
    X(voice_capture,  'R', "VoiceCapture",               STR,   "")                   \
    X(voice_deploy,   'R', "VoiceDeploy",                STR,   "")                   \
    X(voice_harvest,  'R', "VoiceHarvest",               STR,   "")                   \
                                                                                      \
    /* ---- art：外形 / 占地 / 图标 ---- */                                            \
    X(art_image,      'A', "Image",                      STR,   "")                   \
    X(art_cameo,      'A', "Cameo",                      STR,   "")                   \
    X(art_alt_cameo,  'A', "AltCameo",                   STR,   "")                   \
    X(art_foundation, 'A', "Foundation",                 STR,   "")                   \
    X(art_height,     'A', "Height",                     INT,   0)                    \
    X(art_occupy_height, 'A', "OccupyHeight",            INT,   0)                    \
    X(art_voxel,      'A', "Voxel",                      BOOL,  false)                \
    X(art_palette,    'A', "Palette",                    STR,   "")                   \
    X(art_alt_palette, 'A', "AltPalette",                BOOL,  false)                \
    X(art_remapable,  'A', "Remapable",                  BOOL,  false)                \
    X(art_new_theater, 'A', "NewTheater",                BOOL,  false)                \
    X(art_demand_load, 'A', "DemandLoad",                BOOL,  false)                \
    X(art_normalized, 'A', "Normalized",                 BOOL,  false)                \
    X(art_flat,       'A', "Flat",                       BOOL,  false)                \
    X(art_use_buffer, 'A', "UseBuffer",                  BOOL,  false)                \
    X(art_sequence,   'A', "Sequence",                   STR,   "")                   \
    X(art_crawls,     'A', "Crawls",                     BOOL,  false)                \
    X(art_fire_up,    'A', "FireUp",                     INT,   0)                    \
    X(art_can_be_hidden, 'A', "CanBeHidden",             BOOL,  false)                \
    X(art_can_hide_things, 'A', "CanHideThings",         BOOL,  false)                \
    X(art_bib_shape,  'A', "BibShape",                   STR,   "")                   \
    X(art_z_shape_point_move, 'A', "ZShapePointMove",    ILIST, )                     \
    X(art_to_overlay, 'A', "ToOverlay",                  STR,   "")                   \
    X(art_turret_offset, 'A', "TurretOffset",            INT,   0)                    \
    X(art_barrel_length, 'A', "PBarrelLength",           INT,   0)                    \
    X(art_docking_offset0, 'A', "DockingOffset0",        ILIST, )                     \
    X(art_primary_fire_flh, 'A', "PrimaryFireFLH",       ILIST, )                     \
    X(art_secondary_fire_flh, 'A', "SecondaryFireFLH",   ILIST, )                     \
    X(art_primary_fire_pixel_offset, 'A', "PrimaryFirePixelOffset", ILIST, )          \
    X(art_damage_levels, 'A', "DamageLevels",            INT,   0)                    \
    X(art_extra_damage_stage, 'A', "ExtraDamageStage",   BOOL,  false)                \
    X(art_buildup,    'A', "Buildup",                    STR,   "")                   \
    X(art_demand_load_buildup, 'A', "DemandLoadBuildup", BOOL,  false)                \
    X(art_free_buildup, 'A', "FreeBuildup",              BOOL,  false)                \
    X(art_add_occupy1, 'A', "AddOccupy1",                ILIST, )                     \
    X(art_add_occupy2, 'A', "AddOccupy2",                ILIST, )                     \
    X(art_add_occupy3, 'A', "AddOccupy3",                ILIST, )                     \
    X(art_add_occupy4, 'A', "AddOccupy4",                ILIST, )                     \
    X(art_add_occupy5, 'A', "AddOccupy5",                ILIST, )                     \
    X(art_add_occupy6, 'A', "AddOccupy6",                ILIST, )                     \
    X(art_add_occupy7, 'A', "AddOccupy7",                ILIST, )                     \
    X(art_remove_occupy1, 'A', "RemoveOccupy1",          ILIST, )                     \
    X(art_remove_occupy2, 'A', "RemoveOccupy2",          ILIST, )                     \
    X(art_remove_occupy3, 'A', "RemoveOccupy3",          ILIST, )                     \
    X(art_remove_occupy4, 'A', "RemoveOccupy4",          ILIST, )                     \
    X(art_remove_occupy5, 'A', "RemoveOccupy5",          ILIST, )                     \
    X(art_remove_occupy6, 'A', "RemoveOccupy6",          ILIST, )                     \
    X(art_remove_occupy7, 'A', "RemoveOccupy7",          ILIST, )                     \
    X(art_muzzle_flash0, 'A', "MuzzleFlash0",            ILIST, )                     \
    X(art_muzzle_flash1, 'A', "MuzzleFlash1",            ILIST, )                     \
    X(art_muzzle_flash2, 'A', "MuzzleFlash2",            ILIST, )                     \
    X(art_muzzle_flash3, 'A', "MuzzleFlash3",            ILIST, )                     \
    X(art_muzzle_flash4, 'A', "MuzzleFlash4",            ILIST, )                     \
    X(art_muzzle_flash5, 'A', "MuzzleFlash5",            ILIST, )                     \
    X(art_muzzle_flash6, 'A', "MuzzleFlash6",            ILIST, )                     \
    X(art_muzzle_flash7, 'A', "MuzzleFlash7",            ILIST, )                     \
    X(art_muzzle_flash8, 'A', "MuzzleFlash8",            ILIST, )                     \
    X(art_muzzle_flash9, 'A', "MuzzleFlash9",            ILIST, )                     \
    X(art_damage_fire_offset0, 'A', "DamageFireOffset0", ILIST, )                     \
    X(art_damage_fire_offset1, 'A', "DamageFireOffset1", ILIST, )                     \
    X(art_damage_fire_offset2, 'A', "DamageFireOffset2", ILIST, )                     \
    /* 动画族：名字是 SHP 基名（STR），ZAdjust/YSort 是像素偏移（INT）。
       三类后缀完全同构，所以用四段展开写，不做二次宏 —— 显式比聪明好查。 */          \
    X(art_anim_active, 'A', "AnimActive",                ILIST, )                     \
    X(art_active_anim, 'A', "ActiveAnim",                STR,   "")                   \
    X(art_active_anim_damaged, 'A', "ActiveAnimDamaged", STR,   "")                   \
    X(art_active_anim_zadjust, 'A', "ActiveAnimZAdjust", INT,   0)                    \
    X(art_active_anim_ysort, 'A', "ActiveAnimYSort",     INT,   0)                    \
    X(art_active_anim_powered, 'A', "ActiveAnimPowered", STR,   "")                   \
    X(art_active_anim_two, 'A', "ActiveAnimTwo",         STR,   "")                   \
    X(art_active_anim_two_damaged, 'A', "ActiveAnimTwoDamaged", STR, "")              \
    X(art_active_anim_two_zadjust, 'A', "ActiveAnimTwoZAdjust", INT, 0)               \
    X(art_active_anim_two_ysort, 'A', "ActiveAnimTwoYSort", INT, 0)                   \
    X(art_active_anim_three, 'A', "ActiveAnimThree",     STR,   "")                   \
    X(art_active_anim_three_damaged, 'A', "ActiveAnimThreeDamaged", STR, "")          \
    X(art_active_anim_three_zadjust, 'A', "ActiveAnimThreeZAdjust", INT, 0)           \
    X(art_active_anim_three_ysort, 'A', "ActiveAnimThreeYSort", INT, 0)               \
    X(art_idle_anim, 'A', "IdleAnim",                    STR,   "")                   \
    X(art_idle_anim_damaged, 'A', "IdleAnimDamaged",     STR,   "")                   \
    X(art_idle_anim_zadjust, 'A', "IdleAnimZAdjust",     INT,   0)                    \
    X(art_idle_anim_ysort, 'A', "IdleAnimYSort",         INT,   0)                    \
    X(art_special_anim, 'A', "SpecialAnim",              STR,   "")                   \
    X(art_special_anim_damaged, 'A', "SpecialAnimDamaged", STR, "")                   \
    X(art_special_anim_zadjust, 'A', "SpecialAnimZAdjust", INT, 0)                    \
    X(art_special_anim_ysort, 'A', "SpecialAnimYSort",   INT,   0)                    \
    X(art_special_anim_two, 'A', "SpecialAnimTwo",       STR,   "")                   \
    X(art_special_anim_two_damaged, 'A', "SpecialAnimTwoDamaged", STR, "")            \
    X(art_special_anim_two_zadjust, 'A', "SpecialAnimTwoZAdjust", INT, 0)             \
    X(art_special_anim_two_ysort, 'A', "SpecialAnimTwoYSort", INT, 0)                 \
    X(art_special_anim_three, 'A', "SpecialAnimThree",   STR,   "")                   \
    X(art_special_anim_three_damaged, 'A', "SpecialAnimThreeDamaged", STR, "")        \
    X(art_special_anim_three_zadjust, 'A', "SpecialAnimThreeZAdjust", INT, 0)         \
    X(art_special_anim_three_ysort, 'A', "SpecialAnimThreeYSort", INT, 0)             \
    X(art_super_anim, 'A', "SuperAnim",                  STR,   "")                   \
    X(art_super_anim_damaged, 'A', "SuperAnimDamaged",   STR,   "")                   \
    X(art_super_anim_zadjust, 'A', "SuperAnimZAdjust",   INT,   0)                    \
    X(art_super_anim_ysort, 'A', "SuperAnimYSort",       INT,   0)                    \
    X(art_super_anim_two, 'A', "SuperAnimTwo",           STR,   "")                   \
    X(art_super_anim_two_damaged, 'A', "SuperAnimTwoDamaged", STR, "")                \
    X(art_super_anim_two_zadjust, 'A', "SuperAnimTwoZAdjust", INT, 0)                 \
    X(art_super_anim_two_ysort, 'A', "SuperAnimTwoYSort", INT,  0)                    \
    X(art_super_anim_three, 'A', "SuperAnimThree",       STR,   "")                   \
    X(art_super_anim_three_damaged, 'A', "SuperAnimThreeDamaged", STR, "")            \
    X(art_super_anim_three_zadjust, 'A', "SuperAnimThreeZAdjust", INT, 0)             \
    X(art_super_anim_three_ysort, 'A', "SuperAnimThreeYSort", INT, 0)                 \
    X(art_super_anim_four, 'A', "SuperAnimFour",         STR,   "")                   \
    X(art_super_anim_four_damaged, 'A', "SuperAnimFourDamaged", STR, "")              \
    X(art_super_anim_four_zadjust, 'A', "SuperAnimFourZAdjust", INT, 0)               \
    X(art_super_anim_four_ysort, 'A', "SuperAnimFourYSort", INT, 0)                   \
    X(art_production_anim, 'A', "ProductionAnim",        STR,   "")                   \
    X(art_production_anim_damaged, 'A', "ProductionAnimDamaged", STR, "")             \
    X(art_production_anim_zadjust, 'A', "ProductionAnimZAdjust", INT, 0)              \
    X(art_production_anim_ysort, 'A', "ProductionAnimYSort", INT, 0)
// ↑ 上面这一行故意不留续行反斜杠：这是 RA2_TECHNO_FIELDS 的结尾。
//   若要在后面继续加组，把这一行的 `)` 去掉并在下一行补 `\`。

/// 一个 TechnoType = rules 的 [单位] 段 + art 的 [Image] 段。
struct TechnoType {
    std::string id;       ///< rules 段名（大写），如 MTNK
    std::string image;    ///< art 段名（大写）；没写 Image= 时等于 id
    int category = -1;    ///< 0=建筑 1=防御 2=步兵 3=载具；-1 = 不在任何类型列表里

    /// **全量**键值（一个键都不丢）。对账就靠这两张表。
    ValueMap rules;
    ValueMap art;

    // ---- 类型化视图（X-macro 生成，见上面的清单）----
#define RA2_TT_DECL(f, src, key, ty, def) RA2_TT_DECL_##ty(f, def)
#define RA2_TT_DECL_INT(f, def) int f = def;
#define RA2_TT_DECL_DBL(f, def) double f = def;
#define RA2_TT_DECL_BOOL(f, def) bool f = def;
#define RA2_TT_DECL_STR(f, def) std::string f = def;
#define RA2_TT_DECL_ILIST(f, def) std::vector<int> f;
#define RA2_TT_DECL_SLIST(f, def) std::vector<std::string> f;
    RA2_TECHNO_FIELDS(RA2_TT_DECL)
#undef RA2_TT_DECL
#undef RA2_TT_DECL_INT
#undef RA2_TT_DECL_DBL
#undef RA2_TT_DECL_BOOL
#undef RA2_TT_DECL_STR
#undef RA2_TT_DECL_ILIST
#undef RA2_TT_DECL_SLIST

    /// 从两个 ValueMap 填类型化字段。**只读、不写** 那两张表。
    void Fill_Typed();

    /// 类型化字段覆盖了多少个键（供覆盖率统计）。
    static int Typed_Key_Count();

    /// X-macro 里声明的全部键，形如 "R:UIName" / "A:Cameo"。
    /// 拿它和真实数据的键集合求交，就是"覆盖率"——不是拍脑袋写的数字。
    static void Typed_Keys(std::vector<std::string>* out);
};

// ---------------------------------------------------------------------------
// 武器 / 弹头 / 抛射体
//
// 这三类**不做全量类型化**：它们的键一共 52 / 85 / 35 种，很多是特效开关
// （IsLaser / IsRadBeam / IsElectricBolt …），语义要等 P3 用到时再逐个逆向。
// 现在给的是"全量键值表 + 已被 RE 锚定的几个字段"（锚点见 UnitModel.cpp）。
// ---------------------------------------------------------------------------

struct WeaponType {
    std::string id;
    ValueMap   kv;

    int    damage = 0;         ///< Damage= → WeaponType+0xa4
    int    rof = 0;            ///< ROF=    → +0xb0（逻辑帧，15Hz）
    double range = 0.0;        ///< Range=  → +0xb4（格）
    int    speed = 0;          ///< Speed=  → +0xa8（leptons/帧；1 格 = 256）
    int    burst = 0;          ///< Burst=
    int    minimum_range = 0;  ///< MinimumRange=
    int    ammo = 0;           ///< Ammo=
    std::string projectile;    ///< Projectile= → BulletType* @+0xa0
    std::string warhead;       ///< Warhead=
    std::string report;        ///< Report=
    std::string anim;          ///< Anim=
    bool omni_fire = false;    ///< OmniFire=
};

struct WarheadType {
    std::string id;
    ValueMap   kv;

    /// Verses= 11 个百分比，下标 = ArmorTypes 顺序
    /// （none/flak/plate/light/medium/heavy/wood/steel/concrete/special_1/special_2）。
    int verses[11] = {100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100};
    bool  verses_present = false;
    int   cell_spread = 0;     ///< CellSpread=
    std::string anim_list;     ///< AnimList=
};

struct ProjectileType {
    std::string id;
    ValueMap   kv;

    std::string image;         ///< Image=；缺省 = 类型名（ObjectType 的继承语义）
    bool inviso = false;       ///< Inviso=    → BulletType+0x29e
    bool arcing = false;       ///< Arcing=    → +0x29b
    bool proximity = false;    ///< Proximity= → +0x29f
};

// ---------------------------------------------------------------------------
// sound(md).ini / theme(md).ini
// ---------------------------------------------------------------------------

/// sound.ini 的 `[SoundList]`：编号 -> 声音名；声音名 -> 段（段里有 Sounds=）。
struct SoundDB {
    /// [SoundList] 编号 -> 声音名（下标 = 编号，空串 = 未定义）。
    std::vector<std::string> list;
    /// 声音名（大写）-> [SoundList] 编号。
    std::unordered_map<std::string, int> list_index;
    /// 声音段（大写名）-> 全量键值。
    std::unordered_map<std::string, ValueMap> defs;

    int Count() const noexcept { return static_cast<int>(list.size()); }
    /// 按名字或编号查声音段；找不到返回 nullptr。
    const ValueMap* Find(const char* name) const;
};

/// theme.ini：`[Themes]` 是主题名列表，每个主题一个段。
struct ThemeType {
    std::string id;
    ValueMap kv;
    std::string name;      ///< Name=（CSF 标签）
    std::string sound;     ///< Sound=
    bool normal = false;   ///< Normal=（缺省 yes，但这里只表达"文件里写了什么"）
    bool repeat = false;   ///< Repeat=
    int  scenario = 0;     ///< Scenario=
    std::string side;      ///< Side=
};

struct ThemeDB {
    std::vector<ThemeType>  themes;
    std::vector<std::string> order;   ///< [Themes] 的编号列表顺序
    const ThemeType* Find(const char* id) const;
};

// ---------------------------------------------------------------------------
// TypeDB：把上面全部装起来
// ---------------------------------------------------------------------------

class TypeDB {
public:
    /// 复用 UnitModelDB 已经合并好的 rules/art（**不重新解析**，避免两份数据分叉），
    /// 再从 MIX 里补读 sound / theme / ai。只挂一个包也能用（缺的族按空处理）。
    bool Load(const UnitModelDB& unitdb, const MixFileClass* const* mixes, int count);

    bool Loaded() const noexcept { return loaded_; }

    /// 全部 TechnoType（含不在任何类型列表里的段？不 —— 只收列表里的 559 个）。
    const std::map<std::string, TechnoType>& Types() const;

    const TechnoType* Type(const char* id) const;
    const WeaponType* Weapon(const char* id) const;
    const WarheadType* Warhead(const char* id) const;
    const ProjectileType* Projectile(const char* id) const;

    const SoundDB& Sounds() const noexcept { return sounds_; }
    const ThemeDB& Themes() const noexcept { return themes_; }

    struct Stats {
        int types = 0;
        int units_in_lists = 0;       ///< 四个类型列表里的名字数（去重）
        int units_missing_section = 0;///< 列表里有名字、但 rules 里没有对应段
        int rules_keys = 0;        ///< 所有单位段去重后一共多少种键
        int art_keys = 0;
        int rules_key_hits = 0;    ///< 键出现次数总和（rules）
        int art_key_hits = 0;
        int typed_keys = 0;        ///< X-macro 声明的键种数
        int typed_rules_hit = 0;   ///< 声明的键里在数据中真实出现的有几个（rules）
        int typed_art_hit = 0;
        int weapons = 0;
        int weapons_by_signature = 0; ///< 全库里"有 Damage= + Warhead="的段数
        int warheads = 0;
        int warheads_in_list = 0;     ///< [Warheads] 列表长度
        int projectiles = 0;
        int sounds = 0;
        int themes = 0;
        bool sound_ini = false;    ///< 本安装里有没有 sound(md).ini
        bool theme_ini = false;
        bool ai_ini = false;       ///< 有没有 ai(md).ini
    };
    const Stats& Stats_() const noexcept { return stats_; }

    /// 列表里有名字、rules 里却没有段的那些（原版数据本身的不一致，不是丢数据）。
    const std::vector<std::string>& Units_Missing_Section() const noexcept {
        return units_missing_;
    }

    /// 逐键对账：段的去重键数与值都与 IniFile 原文一致。返回不一致的条数（0 = 过）。
    /// 需要同时把 rules_/art_ 传进来 —— 表本身就是从它们建的。
    int Verify(const IniFile& rules, const IniFile& art, FILE* log) const;

    /// 打表：每一行 `单位<TAB>来源<TAB>键<TAB>值`，按 (单位, 来源, 键) 升序。
    /// 排序是为了让"独立实现的对账"不必关心两边的插入顺序。
    void Dump_Raw(FILE* f) const;

    /// 人读的概览（键出现频次前 N）。
    void Dump_Summary(FILE* f, int top_keys) const;

private:
    void Collect_Types(const UnitModelDB& unitdb);
    void Build_Weapons(const IniFile& rules);
    void Build_Warheads(const IniFile& rules);
    void Build_Projectiles(const IniFile& rules);
    void Load_Sounds(const MixFileClass* const* mixes, int count);
    void Load_Themes(const MixFileClass* const* mixes, int count);
    bool Has_AI_INI(const MixFileClass* const* mixes, int count, FILE* log) const;

    std::map<std::string, TechnoType>      types_;
    std::map<std::string, WeaponType>      weapons_;
    std::map<std::string, WarheadType>     warheads_;
    std::map<std::string, ProjectileType>  projectiles_;
    std::vector<std::string>               units_missing_;
    SoundDB sounds_;
    ThemeDB themes_;
    Stats   stats_;
    bool    loaded_ = false;
};

}  // namespace ra2
