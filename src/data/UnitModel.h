// UnitModel.h -- P2 数据层：从单位名推出它的体素模型由哪几个文件组成。
//
// 为什么需要这一层：P1 之前想看一辆坦克得手写三个 0xID（车体 / 炮塔 / 炮管）。
// 游戏自己从来不这么干 —— 它读 rules(md).ini 拿 Image=，读 art(md).ini 拿
// Voxel=yes，然后按名字拼出 <名>.VXL / <名>TUR.VXL / <名>BARL.VXL 去 MIX 查。
// 这条链路打通后，"给单位名出图"才算是真的连上了。
//
// 【INI 里到底怎么摆】实测（tools/iniprobe.py，四个文件全扫一遍）：
//   Voxel=yes        -> art(md).ini 的 [Image] 段       rules 里一处都没有
//   Turret=yes       -> rules(md).ini 的 [单位] 段      art 里一处都没有
//   Image=           -> rules(md).ini 的 [单位] 段      缺省 = 单位名本身
//   PrimaryFireFLH=  -> art(md).ini 的 [Image] 段       muzzle 位置
//   TurretOffset=    -> art(md).ini 的 [Image] 段
// 也就是说**同一个单位的信息被拆在 rules 和 art 两个文件里**，
// 只认一个必然错（实测 MTNK 的 Image=GTNK，不查 rules 就找不着模型）。
//
// 【文件名怎么拼】MIX 里没有名字，只有 Westwood CRC，而 CRC 对大小写不敏感
// （算法内部先转大写），所以这里统一用大写拼：
//     <IMAGE>.VXL     车体（必需）
//     <IMAGE>.HVA     车体动画
//     <IMAGE>TUR.VXL  炮塔（Turret=yes 且文件存在时）
//     <IMAGE>BARL.VXL 炮管（文件存在时）
//
// 【炮塔不是一定有独立文件】实测 18 个 Turret=yes 的单位里 17 个有 TUR.VXL，
// 只有 SCHP 没有 —— 它的炮塔是车体 VXL 里的肢体（CYLINDER19/CYLINDER57/DUMMY01）。
// 所以判据是"文件在不在 MIX 里"，不是"Turret=yes 没有"。
//
// 【验收】86 个 Voxel=yes 单位的车体 VXL **全部**命中（db/unit-vxl.json）。
// 这条不是碰巧：写错 Image= 的解析会立刻大面积落空。

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "data/Ini.h"
#include "io/FileSystem.h"

namespace ra2 {

/// 模型的一个组成部分（车体 / 炮塔 / 炮管，以及各自配的 HVA）。
struct UnitModelPart {
    std::string name;       ///< 大写文件名，如 "GTNKTUR.VXL"
    uint32_t id = 0;        ///< Westwood CRC
    bool present = false;   ///< MIX 里找不找得到
};

/// 一个单位的体素模型组成。
struct UnitModel {
    std::string unit;    ///< INI 里的单位名，如 MTNK
    std::string image;   ///< 美术名，如 GTNK（没写 Image= 时等于 unit）
    bool voxel = false;  ///< art[image].Voxel=yes
    bool turret = false; ///< rules[unit].Turret=yes

    UnitModelPart body;
    UnitModelPart body_hva;
    UnitModelPart turret_vxl;
    UnitModelPart turret_hva;
    UnitModelPart barrel_vxl;
    UnitModelPart barrel_hva;

    int flh[3] = {0, 0, 0};   ///< art[image].PrimaryFireFLH = "x,y,z"
    int turret_offset = 0;    ///< art[image].TurretOffset

    // ---- 建造相关（全部来自 rules.ini 的 [单位] 段）----
    // 侧栏能不能建、多少钱、多久、多少电，都由这几项决定。
    int cost = 0;            ///< Cost=
    int tech_level = 0;      ///< TechLevel=（-1 表示没写 = 永远可建）
    int power = 0;           ///< Power=：正数发电、负数耗电
    int strength = 0;        ///< Strength=（血量上限）
    int sight = 0;           ///< Sight=（UnitType::Read @0x00712170）
    int speed = 0;           ///< Speed=：0..10，越大越快
    int build_time = 1;      ///< BuildTime= 倍率（缺省 1）
    std::string cameo;       ///< art[image].Cameo=，侧栏图标基名（无后缀）
    /// 占地格数。建筑看 art 的 Foundation=NxM（gamemd 0x0046122D 读此键）；
    /// rules 的 Width=/Height= 对 BuildingTypes 基本不存在。锚点要用足迹中心，
    /// 否则 2x2/6x4 楼全钉在左上格，Arena 城区会像被撕碎。
    int width = 1, height = 1;
    /// art[image].NewTheater=yes：SHP 第二字母按剧场替换（CA→CU @ URBAN）。
    bool new_theater = false;
    /// art[image].Remapable=（缺省 yes）。no 时仍用 unit*.pal，但不套阵营色。
    bool remapable = true;
    bool has_weapon = false; ///< Primary= 非空 —— 用来把建筑分成"结构/防御"
    bool wall = false;       ///< Wall=yes（围墙也算防御类）
    bool harvester = false;  ///< Harvester=yes 或 Storage>0 的采矿车
    int storage = 0;         ///< Storage= 矿车/矿厂容量（矿粒）
    /// art PlaceAnywhere= → BuildingType+0x1703；CanPlaceHere @0x00464AC0 为真则直接过。
    bool place_anywhere = false;
    /// rules Adjacent= → BuildingType+0xEB4；邻近扫描半径 = Adjacent+1（0x004A8F48）。
    int adjacent = 1;
    /// rules BaseNormal= → +0x154F；邻近判定只认 BaseNormal 建筑（0x004A8FEC）。
    bool base_normal = true;
    std::vector<std::string> owners;  ///< Owner=，决定哪个阵营能建
    std::vector<std::string> prereq;  ///< Prerequisite=，建它之前要先有什么

    // ---- 战斗（Primary 武器段，对齐 rulesmd / WeaponType::Read @0x00772080）----
    std::string primary;     ///< Primary= 武器名
    int damage = 0;          ///< [Weapon] Damage= → +0xa4
    int rof = 0;             ///< [Weapon] ROF= → +0xb0（逻辑帧间隔，15Hz）
    float range = 0.0f;      ///< [Weapon] Range= → +0xb4（格）
    int weapon_speed = 0;    ///< [Weapon] Speed= → +0xa8（leptons/帧；1 格=256）
    std::string projectile;  ///< [Weapon] Projectile= → BulletType* @+0xa0
    std::string proj_image;  ///< [Projectile] Image=；缺省=Projectile 名（ObjectType）
    bool proj_inviso = false;    ///< [Projectile] Inviso= → BulletType+0x29e
    bool proj_arcing = false;    ///< [Projectile] Arcing= → +0x29b
    bool proj_proximity = false; ///< [Projectile] Proximity= → +0x29f
    std::string armor;       ///< Armor=none/flak/…/concrete
    int armor_index = 0;     ///< Verses 下标 0..10
    /// [Warhead] Verses= 11 个百分比（相对 ArmorTypes 顺序）。
    int verses[11] = {100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100};
    std::string deploys_into; ///< DeploysInto=（AMCV→GACNST）
    /// rules Factory= → BuildingType+0xEB8（BuildingType/InfantryType/UnitType/AircraftType）。
    /// Begin_Production @0x4FA438 与 Building AI @0x450326 用此字段找合适工厂。
    std::string factory;

    /// 侧栏页签：0=建筑 1=防御 2=步兵 3=载具。四个列表之外的类型是 -1。
    int category = -1;

    /// 能拿出一个可渲染的体素模型。
    bool ok() const noexcept { return voxel && body.present; }
};

/// 单位名 -> 体素模型的解析表。
class UnitModelDB {
public:
    /// 从若干已打开的 MIX 里读 RULES.INI / RULESMD.INI / ART.INI / ARTMD.INI。
    ///
    /// 为什么要接受多个：rules 在 ra2.mix 里（RULES.INI），
    /// rulesmd 在 ra2md.mix 里（RULESMD.INI），**一人一份**，只挂一个包就只有一半。
    /// 实测 ra2.mix 单挂：rules 1191 段 / 59 个体素单位；
    ///      两个一起：rules 1482 段 / 86 个体素单位。
    ///
    /// 覆盖顺序 = 数组**从后往前**（后挂载的优先），且 md 永远压过 base。
    /// 四份都找不到返回 false。
    ///
    /// 同时把所有 MIX 里全部条目（含嵌套）的 ID 收进一张集合，
    /// 用来判断 "TUR.VXL 在不在" —— 逐个 Read_Deep 会重复解 Blowfish，太慢。
    bool Load(const MixFileClass* const* mixes, int count);
    bool Load(const MixFileClass& mix) {
        const MixFileClass* p = &mix;
        return Load(&p, 1);
    }

    bool Loaded() const noexcept { return loaded_; }

    /// 合并好的 rules(md) / art(md)。**给 P2 的 TypeDB 复用** ——
    /// 它要做全量打表，但绝不能再解析一遍 rules：两份数据一旦分叉，
    /// "同一个单位在两张表里不一样"这种 bug 会极其难查。
    const IniFile& Rules() const noexcept { return rules_; }
    const IniFile& Art() const noexcept { return art_; }

    /// 解析一个单位。结果缓存在表里（同名只算一次）。找不到段返回 nullptr。
    const UnitModel* Resolve(const char* unit);

    /// 从 [VehicleTypes]/[AircraftTypes]/[InfantryTypes]/[BuildingTypes]
    /// 四个编号列表收集到的全部单位名。
    const std::vector<std::string>& Units() const noexcept { return units_; }
    int Unit_Count() const noexcept { return static_cast<int>(units_.size()); }

    /// [OverlayTypes] 按 INI 编号下标取 Image=（空串 = 这个编号没定义）。
    /// OverlayPack 的字节就是这个下标，0xFF = 无。
    const std::vector<std::string>& Overlay_Images() const noexcept {
        return overlay_images_;
    }
    /// 与 Overlay_Images 平行：art 里 NewTheater=yes 的项。
    /// 加载 SHP 时要把文件名第 2 个字母换成剧场字母（NAWALL→NUWALL）。
    const std::vector<uint8_t>& Overlay_New_Theater() const noexcept {
        return overlay_new_theater_;
    }
    int Overlay_Count() const noexcept {
        return static_cast<int>(overlay_images_.size());
    }

    struct Stats {
        int rules_sections = 0;   ///< rules.ini + rulesmd.ini 合并后的段数
        int art_sections = 0;
        int units = 0;            ///< 类型列表里的单位数
        int voxel = 0;            ///< 其中 Voxel=yes 的
        int with_turret = 0;      ///< 有独立 TUR.VXL 的
        int with_barrel = 0;      ///< 有独立 BARL.VXL 的
        int body_missing = 0;     ///< Voxel=yes 但车体 VXL 找不到（应为 0）
    };
    const Stats& Stats_() const noexcept { return stats_; }

    /// 打印一张全量表（--unitdb 用）。only_voxel 只列体素单位。
    void Dump(bool only_voxel = true) const;

    // ---- 侧栏建造列表 ----

    /// 开局资金（[General] 的 StartCredits，缺省 10000 —— 原版就是这个数）。
    int Start_Credits() const noexcept { return start_credits_; }
    /// 开局科技等级（[General] 的 TechLevel=，缺省 10 表示全开）。
    int Start_Tech_Level() const noexcept { return start_tech_; }
    /// [General] BuildSpeed=：造 1000 信贷物品要多少分钟（YR 默认 0.7）。
    double Build_Speed() const noexcept { return build_speed_; }
    /// gamemd 0x00711EE0：frames = Cost * BuildSpeed * kBuildFramesPerCredit
    /// k = 0.9 @ 0x007F4E80（= 900/1000，15Hz×60 秒）。
    static constexpr double kBuildFramesPerCredit = 0.9;
    int Build_Frames(int cost) const noexcept {
        if (cost <= 0) {
            return 1;
        }
        const int f = static_cast<int>(static_cast<double>(cost) * build_speed_ *
                                       kBuildFramesPerCredit + 0.5);
        return f > 0 ? f : 1;
    }
    /// [General] RefundPercent=（变卖退款百分比）。
    double Refund_Percent() const noexcept { return refund_percent_; }
    /// [General] RepairPercent=（满修总费用占 Cost 的比例，0.15）。
    double Repair_Percent() const noexcept { return repair_percent_; }
    /// [General] RepairRate=（分钟/步）；步间隔帧 = RepairRate * 900（0x007E27F8）。
    double Repair_Rate() const noexcept { return repair_rate_; }
    int Repair_Interval_Frames() const noexcept {
        const int f = static_cast<int>(repair_rate_ * 900.0 + 0.5);
        return f > 0 ? f : 1;
    }
    /// [General] RepairStep= / IRepairStep=。
    int Repair_Step() const noexcept { return repair_step_; }
    int IRepair_Step() const noexcept { return irepair_step_; }
    /// [General] ConditionYellow= / ConditionRed=（血条 PIPS 帧切换阈值）。
    /// gamemd：Rules+0x1700 / +0x1708；DrawHealthBar @0x006F64A0 用
    /// 0x005F5D20 / 0x005F5CD0 选型：>Yellow→帧1，(Red,Yellow]→帧2，≤Red→帧4。
    double Condition_Yellow() const noexcept { return condition_yellow_; }
    double Condition_Red() const noexcept { return condition_red_; }
    /// 单步修理费用：gamemd 0x007120D0
    /// cost_step = max(1, (Cost / (Strength/RepairStep)) * RepairPercent)
    int Repair_Step_Cost(int cost, int strength, bool infantry) const noexcept {
        const int step = infantry ? irepair_step_ : repair_step_;
        if (cost <= 0 || strength <= 0 || step <= 0) {
            return 1;
        }
        const int chunks = strength / step;
        if (chunks <= 0) {
            return 1;
        }
        const int c = static_cast<int>(static_cast<double>(cost / chunks) *
                                       repair_percent_ + 0.5);
        return c > 1 ? c : 1;
    }
    /// 低电生产：rules Min/MaxLowPowerProductionSpeed + LowPowerPenaltyModifier。
    double Min_Low_Power_Prod() const noexcept { return min_low_power_prod_; }
    double Max_Low_Power_Prod() const noexcept { return max_low_power_prod_; }
    double Low_Power_Penalty_Mod() const noexcept { return low_power_penalty_; }
    /// [Riparius] Value=：每 bail 信贷（rules [Tiberiums] 矿）。
    int Ore_Bail_Value() const noexcept { return ore_bail_value_; }
    /// [General] HarvesterLoadRate=（int，缺省 2；gamemd 0x00670CF4 / 默认 ctor）。
    int Harvester_Load_Rate() const noexcept { return harvester_load_rate_; }
    /// 装矿计时：0x0073D529 存 LoadRate*3 到 unit+0x10c。
    int Harvester_Load_Interval_Frames() const noexcept {
        return harvester_load_rate_ * 3;
    }
    /// [General] HarvesterDumpRate=（分钟，缺省 0.016；Rules+0x1528）。
    /// 卸矿间隔帧 = DumpRate * 900（常量 0x007E27F8）。
    double Harvester_Dump_Rate() const noexcept { return harvester_dump_rate_; }
    int Harvester_Dump_Interval_Frames() const noexcept {
        const int f = static_cast<int>(harvester_dump_rate_ * 900.0 + 0.5);
        return f > 0 ? f : 1;
    }

    /// OverlayTypes 编号是否矿石/宝石（Tiberium=yes；类型名由 rules 标记）。
    bool Overlay_Is_Ore(int index) const;

    /// LandType.Buildable（rules 段 Buildable=；Get_Bool 缺省 0 @0x0067420E → +0x1C）。
    /// CanPlaceHere 建筑分支 @0x0047CA33 读 byte [land*0x24 + 0x89ea60]。
    bool Land_Buildable(int land) const noexcept {
        if (land < 0 || land >= 12) {
            return false;
        }
        return land_buildable_[land];
    }
    /// [General] ShortGame=（全局 0xa8b262；yes 时败北只看建筑+BaseUnit @0x004F8EC6）。
    bool Short_Game() const noexcept { return short_game_; }
    /// [General] FogOfWar=（0x00671EA0 一带读入；缺省 false）。
    bool Fog_Of_War() const noexcept { return fog_of_war_; }
    /// [SpecialFlags] MCVDeploy=（会话旗，非 Deploy 直接赋值）。
    bool MCV_Deploy() const noexcept { return mcv_deploy_; }
    /// [General] AIAutoDeployFrameDelay[IQ]（Rules+0xE2C TypeList）。
    int AI_Auto_Deploy_Delay(int iq) const noexcept {
        if (ai_auto_deploy_delay_.empty()) return 0;
        int i = iq;
        if (i < 0) i = 0;
        if (i >= (int)ai_auto_deploy_delay_.size())
            i = (int)ai_auto_deploy_delay_.size() - 1;
        return ai_auto_deploy_delay_[i];
    }
    /// [IQ] MaxIQLevels=（Rules+0x1434；IQ clamp）。
    int Max_IQ_Levels() const noexcept { return max_iq_levels_; }
    /// [IQ] Production= 等（0x00674240，段名 "IQ"）；启用能力所需最低 IQ。
    int IQ_Production() const noexcept { return iq_production_; }

    /// 侧栏/生产类型 → 需要的 Factory= 字符串（与 BuildingType+0xEB8 / 0x4FBD80 槽对应）。
    /// 建筑/防御→BuildingType；步兵→InfantryType；载具→UnitType；其余空。
    static const char* Needed_Factory_For_Category(int category) noexcept {
        switch (category) {
            case 0:
            case 1:
                return "BuildingType";
            case 2:
                return "InfantryType";
            case 3:
                return "UnitType";
            default:
                return "";
        }
    }
    int IQ_Super_Weapons() const noexcept { return iq_super_weapons_; }
    int IQ_Repair_Sell() const noexcept { return iq_repair_sell_; }
    int IQ_Auto_Crush() const noexcept { return iq_auto_crush_; }
    int IQ_Guard_Area() const noexcept { return iq_guard_area_; }
    int IQ_Scatter() const noexcept { return iq_scatter_; }
    int IQ_Content_Scan() const noexcept { return iq_content_scan_; }
    int IQ_Aircraft() const noexcept { return iq_aircraft_; }
    int IQ_Harvester() const noexcept { return iq_harvester_; }
    int IQ_Sell_Back() const noexcept { return iq_sell_back_; }
    /// [General] BaseUnit=（Rules+0xb24；ShortGame 下当作“家”的载具）。
    const std::vector<std::string>& Base_Units() const noexcept {
        return base_units_;
    }
    /// 按国家 Side= 从 BaseUnit 列表取对应 MCV（[Sides] 序：GDI/Nod/ThirdSide → 下标 0/1/2）。
    const char* Base_Unit_For_Country(const char* country) const;

    /// [General] PrerequisitePower/Barracks/… 抽象组 -> 具体建筑名列表。
    const std::vector<std::string>& Prereq_Group(const char* token) const;

    /// 某个页签的**原始**名单（未过滤）。tab: 0=建筑 1=防御 2=步兵 3=载具。
    const std::vector<std::string>& Tab_Units(int tab) const;

    /// 按 Owner + TechLevel + 阵营 Prerequisite 过滤出某页签"现在能建"的东西。
    /// owner 是阵营名（Russians / Americans…），大小写不敏感；空 = 不过滤 Owner。
    /// tech_level < 0 = 不过滤 TechLevel。
    /// Owner= 在 YR 里几乎写全体阵营，真正分盟军/苏军的是 Prerequisite 里的
    /// GACNST/NACNST/GAWEAP/NAWEAP…（rules [Sides] + 各国 Side=）。
    int Buildable(int tab, const char* owner, int tech_level,
                  std::vector<std::string>* out) const;

    /// 国家名 -> Side=（GDI / Nod / ThirdSide…）。空串 = 查无。
    std::string Country_Side(const char* country) const;

private:
    void Collect_Units();
    void Collect_Overlays();
    bool Have(uint32_t id) const { return ids_.find(id) != ids_.end(); }
    static UnitModelPart Make_Part(const std::string& image, const char* suffix,
                                   const char* ext);

    IniFile rules_;
    IniFile art_;
    std::unordered_set<uint32_t> ids_;
    std::unordered_map<std::string, UnitModel> cache_;
    std::vector<std::string> units_;
    std::vector<std::string> tabs_[4];   ///< 侧栏四页签的名单
    std::unordered_map<std::string, int> category_;  ///< 单位名 -> 页签
    std::vector<std::string> overlay_images_;        ///< OverlayTypes 编号 -> Image=
    std::vector<uint8_t> overlay_new_theater_;       ///< 同下标：NewTheater=yes
    std::vector<uint8_t> overlay_is_ore_;            ///< 同下标：是否矿/宝石
    int start_credits_ = 10000;
    int start_tech_ = 10;
    double build_speed_ = 0.7;
    double refund_percent_ = 0.5;
    double repair_percent_ = 0.15;
    double repair_rate_ = 0.016;
    int repair_step_ = 8;
    int irepair_step_ = 20;
    double condition_yellow_ = 0.5;  ///< Rules+0x1700
    double condition_red_ = 0.25;    ///< Rules+0x1708
    double min_low_power_prod_ = 0.5;
    double max_low_power_prod_ = 0.8;
    double low_power_penalty_ = 1.0;
    int ore_bail_value_ = 25;  ///< [Riparius] Value=
    int harvester_load_rate_ = 2;
    double harvester_dump_rate_ = 0.016;
    bool land_buildable_[12] = {};
    bool short_game_ = true;  ///< rules ShortGame= 缺省注释写 def=yes；INI 显式 ShortGame=yes
    bool fog_of_war_ = false; ///< rules FogOfWar=
    bool mcv_deploy_ = false; ///< SpecialFlags MCVDeploy=
    std::vector<int> ai_auto_deploy_delay_; ///< General AIAutoDeployFrameDelay=
    int max_iq_levels_ = 5;                 ///< [IQ] MaxIQLevels=
    int iq_production_ = 5;                 ///< [IQ] Production=
    int iq_super_weapons_ = 4;              ///< [IQ] SuperWeapons=
    int iq_repair_sell_ = 1;                ///< [IQ] RepairSell=
    int iq_auto_crush_ = 2;                 ///< [IQ] AutoCrush=
    int iq_guard_area_ = 2;                 ///< [IQ] GuardArea=
    int iq_scatter_ = 2;                    ///< [IQ] Scatter=
    int iq_content_scan_ = 3;               ///< [IQ] ContentScan=
    int iq_aircraft_ = 3;                   ///< [IQ] Aircraft=
    int iq_harvester_ = 2;                  ///< [IQ] Harvester=
    int iq_sell_back_ = 2;                  ///< [IQ] SellBack=
    std::vector<std::string> base_units_;
    std::unordered_map<std::string, std::vector<std::string>> prereq_groups_;
    Stats stats_;
    bool loaded_ = false;
};

}  // namespace ra2
