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
    int speed = 0;           ///< Speed=：0..10，越大越快
    int build_time = 1;      ///< BuildTime= 倍率（缺省 1）
    /// 占地格数（建筑的 Width=/Height=）。锚点要用它把大建筑挪到足迹中心，
    /// 不然 2x2、3x3 的楼全偏一格，整个城市看起来就是乱的。
    int width = 1, height = 1;
    bool has_weapon = false; ///< Primary= 非空 —— 用来把建筑分成"结构/防御"
    bool wall = false;       ///< Wall=yes（围墙也算防御类）
    std::vector<std::string> owners;  ///< Owner=，决定哪个阵营能建
    std::vector<std::string> prereq;  ///< Prerequisite=，建它之前要先有什么

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

    /// 解析一个单位。结果缓存在表里（同名只算一次）。找不到段返回 nullptr。
    const UnitModel* Resolve(const char* unit);

    /// 从 [VehicleTypes]/[AircraftTypes]/[InfantryTypes]/[BuildingTypes]
    /// 四个编号列表收集到的全部单位名。
    const std::vector<std::string>& Units() const noexcept { return units_; }
    int Unit_Count() const noexcept { return static_cast<int>(units_.size()); }

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

    /// 某个页签的**原始**名单（未过滤）。tab: 0=建筑 1=防御 2=步兵 3=载具。
    const std::vector<std::string>& Tab_Units(int tab) const;

    /// 按 Owner + TechLevel 过滤出某页签"现在能建"的东西，写进 out。
    /// owner 是阵营名（Russians / Americans…），大小写不敏感；空 = 不过滤。
    /// tech_level < 0 = 不过滤。返回写入的个数。
    int Buildable(int tab, const char* owner, int tech_level,
                  std::vector<std::string>* out) const;

private:
    void Collect_Units();
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
    int start_credits_ = 10000;
    int start_tech_ = 10;
    Stats stats_;
    bool loaded_ = false;
};

}  // namespace ra2
