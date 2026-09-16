// UnitModel.cpp -- 见 UnitModel.h。

#include "data/UnitModel.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ra2 {
namespace {

// 会当成"单位"去解析的类型列表段。顺序即优先级（先出现先收录）。
const char* const kTypeLists[] = {
    "VehicleTypes",
    "AircraftTypes",
    "InfantryTypes",
    "BuildingTypes",
};

/// INI 里写的都是小写/混写，MIX 里的名字是 CRC(大写)，所以统一转大写。
std::string Upper(std::string s) {
    for (char& c : s) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    return s;
}

std::string Trim(const std::string& s) {
    const char* kSp = " \t\r\n\f\v";
    const size_t b = s.find_first_not_of(kSp);
    if (b == std::string::npos) {
        return std::string();
    }
    const size_t e = s.find_last_not_of(kSp);
    return s.substr(b, e - b + 1);
}

/// 把 "RULES.INI" 这类名字从 MIX 里捞出来合并进 ini。找不到返回 false。
bool Merge_From_Mix(const MixFileClass& mix, const char* name, IniFile* ini,
                    bool overwrite) {
    const std::vector<uint8_t> d = mix.Read_Deep(name);
    if (d.empty()) {
        return false;
    }
    return ini->Merge(d.data(), d.size(), overwrite);
}

}  // namespace

// ---------------------------------------------------------------------------

bool UnitModelDB::Load(const MixFileClass* const* mixes, int count) {
    rules_.Clear();
    art_.Clear();
    ids_.clear();
    if (mixes == nullptr || count <= 0) {
        return false;
    }

    // 顺序 = 挂载顺序，且 base 打底、md 覆盖（覆盖在位，见 Ini::Merge）。
    // 后一个 pass 用 overwrite=true，这样 md 的值会顶掉 base 的，
    // 而编号列表（[BuildingTypes] 之类）也不会因为两处都有 1..N 而翻倍。
    int got = 0;
    for (int pass = 0; pass < 2; ++pass) {
        const bool overwrite = (pass == 1);
        const char* rname = (pass == 0) ? "RULES.INI" : "RULESMD.INI";
        const char* aname = (pass == 0) ? "ART.INI" : "ARTMD.INI";
        for (int i = 0; i < count; ++i) {
            if (Merge_From_Mix(*mixes[i], rname, &rules_, overwrite)) {
                ++got;
            }
            if (Merge_From_Mix(*mixes[i], aname, &art_, overwrite)) {
                ++got;
            }
        }
    }
    if (got == 0) {
        std::printf("[x] 这些 MIX 里一份 RULES/ART 都没有\n");
        return false;
    }

    std::vector<uint32_t> ids;
    for (int i = 0; i < count; ++i) {
        mixes[i]->Collect_Leaf_IDs(&ids, 4);
    }
    ids_.reserve(ids.size() * 2);
    for (uint32_t v : ids) {
        ids_.insert(v);
    }

    stats_.rules_sections = rules_.Section_Count();
    stats_.art_sections = art_.Section_Count();

    Collect_Units();
    Collect_Overlays();
    loaded_ = true;
    // 全量解析一遍：统计要准（有多少体素单位、几个缺车体），
    // Dump 也要能直接把所有单位列出来。
    for (const std::string& u : units_) {
        Resolve(u.c_str());
    }

    // 页签要在**全部解完之后**才定：建筑的"结构 / 防御"之分要看
    // Resolve 读出来的 Primary= / Wall=（见 Collect_Units 的注释）。
    for (const std::string& u : units_) {
        auto it = cache_.find(u);
        if (it == cache_.end()) {
            continue;
        }
        UnitModel& m = it->second;
        int tab = m.category;
        if (tab == 0 && (m.has_weapon || m.wall)) {
            tab = 1;
        }
        m.category = tab;
        if (tab >= 0 && tab < 4) {
            tabs_[tab].push_back(u);
        }
    }
    return true;
}

const std::vector<std::string>& UnitModelDB::Tab_Units(int tab) const {
    static const std::vector<std::string> kEmpty;
    if (tab < 0 || tab > 3) {
        return kEmpty;
    }
    return tabs_[tab];
}

int UnitModelDB::Buildable(int tab, const char* owner, int tech_level,
                           std::vector<std::string>* out) const {
    if (out == nullptr) {
        return 0;
    }
    out->clear();
    if (tab < 0 || tab > 3) {
        return 0;
    }
    const std::string own = Upper(Trim(owner != nullptr ? owner : ""));
    const std::string side = Upper(Country_Side(own.c_str()));
    for (const std::string& id : tabs_[tab]) {
        const auto it = cache_.find(id);
        if (it == cache_.end()) {
            continue;
        }
        const UnitModel& m = it->second;
        // Owner= 里没写的（少数通用件）谁都建得了；写了就得命中。
        if (!own.empty() && !m.owners.empty()) {
            bool hit = false;
            for (const std::string& o : m.owners) {
                if (Upper(Trim(o)) == own) {
                    hit = true;
                    break;
                }
            }
            if (!hit) {
                continue;
            }
        }
        // TechLevel=-1：原版不可建造（规则里显式写 -1，不是"缺省未写"）。
        // 以前把 -1 当"无限制"，侧栏会塞进 BUS/民用车等上百项。
        if (m.tech_level < 0) {
            continue;
        }
        if (tech_level >= 0 && m.tech_level > tech_level) {
            continue;
        }
        // 阵营专属 Prerequisite + 敌方建造厂（空 Prerequisite 的 NACNST/GACNST）。
        // Owner= 几乎人人有份；侧栏要对齐 SIDEC，必须按 Side / 前置过滤。
        if (!side.empty() && !m.prereq.empty()) {
            bool bad = false;
            for (const std::string& raw : m.prereq) {
                const std::string p = Upper(Trim(raw));
                if (p.empty()) {
                    continue;
                }
                const bool allied = (p == "GACNST" || p == "GAWEAP" || p == "GAPILE" ||
                                     p == "GAREFN" || p == "GATECH" || p == "GAPOWR");
                const bool soviet = (p == "NACNST" || p == "NAWEAP" || p == "NAHAND" ||
                                     p == "NAREFN" || p == "NATECH" || p == "NAPOWR");
                const bool yuri = (p == "YACNST" || p == "YAWEAP" || p == "YABRCK" ||
                                   p == "YAREFN" || p == "YATECH" || p == "YAPOWR");
                if (allied && side != "GDI") {
                    bad = true;
                    break;
                }
                if (soviet && side != "NOD") {
                    bad = true;
                    break;
                }
                if (yuri && side != "THIRDSIDE" && side != "YURI") {
                    bad = true;
                    break;
                }
            }
            if (bad) {
                continue;
            }
        }
        // 空 Prerequisite 的敌方建造厂仍会出现在列表里（NACNST TechLevel=-1）。
        if (!side.empty()) {
            const std::string uid = Upper(id);
            if (side == "GDI" &&
                (uid == "NACNST" || uid == "YACNST")) {
                continue;
            }
            if (side == "NOD" &&
                (uid == "GACNST" || uid == "YACNST")) {
                continue;
            }
            if ((side == "THIRDSIDE" || side == "YURI") &&
                (uid == "GACNST" || uid == "NACNST")) {
                continue;
            }
            // 无阵营前置、但类型名是对方前缀的（TESLA 有 NACNST 已滤；
            // 大量 NA* 只有 POWER/BARRACKS）—— 按前缀再挡一层。
            // 例外：NASAM 是盟军防空。
            if (side == "GDI" && uid.size() >= 2 && uid[0] == 'N' && uid[1] == 'A' &&
                uid != "NASAM") {
                continue;
            }
            if (side == "NOD" && uid.size() >= 2 && uid[0] == 'G' && uid[1] == 'A') {
                continue;
            }
        }
        out->push_back(id);
    }
    return static_cast<int>(out->size());
}

std::string UnitModelDB::Country_Side(const char* country) const {
    const std::string c = Trim(country != nullptr ? country : "");
    if (c.empty()) {
        return {};
    }
    return Trim(rules_.Get_String(c.c_str(), "Side", ""));
}

const char* UnitModelDB::Base_Unit_For_Country(const char* country) const {
    // BaseUnit= 列表与 [Sides] 对齐：下标 0=GDI、1=Nod、2=ThirdSide（0x0066D530 读入）。
    if (base_units_.empty()) {
        return nullptr;
    }
    const std::string side = Upper(Country_Side(country));
    size_t idx = 0;
    if (side == "NOD") {
        idx = 1;
    } else if (side == "THIRDSIDE" || side == "YURI") {
        idx = 2;
    }
    if (idx >= base_units_.size()) {
        idx = 0;
    }
    return base_units_[idx].c_str();
}

void UnitModelDB::Collect_Units() {
    units_.clear();
    tabs_[0].clear();
    tabs_[1].clear();
    tabs_[2].clear();
    tabs_[3].clear();
    category_.clear();
    std::unordered_set<std::string> seen;
    // 原始四张列表 -> 侧栏页签的落位规则：
    //   InfantryTypes -> 步兵页(2)；VehicleTypes / AircraftTypes -> 载具页(3)；
    //   BuildingTypes 要**再分一次**：有武器或有 Wall=yes 的算"防御"(1)，
    //   其余算"建筑"(0)。
    // 【为什么靠 Primary= 判防御】原版没有单独的"防御列表"，而实测
    // 哨戒炮/高炮/光棱塔/爱国者全都写了 Primary=，围墙写了 Wall=yes，
    // 而电厂/兵营/矿厂一个武器都没有 —— 这条分界线在原版数据上是干净的。
    // 具体归类要等 Resolve 读出 Primary= 才能定，这里先把候选分好。
    for (const char* list : kTypeLists) {
        std::vector<std::string> names;
        rules_.Read_Numbered_List(list, &names);
        const std::string ls = list;
        int tab = 3;
        if (ls == "InfantryTypes") {
            tab = 2;
        } else if (ls == "BuildingTypes") {
            tab = 0;   // 待定，Resolve 之后再改到 1
        }
        for (const std::string& raw : names) {
            const std::string n = Upper(Trim(raw));
            if (n.empty()) {
                continue;
            }
            if (seen.find(n) == seen.end()) {
                seen.insert(n);
                units_.push_back(n);
            }
            if (category_.find(n) == category_.end()) {
                category_[n] = tab;
            }
        }
    }
    stats_.units = static_cast<int>(units_.size());

    // 开局资金 / 科技等级
    start_credits_ = rules_.Get_Int("General", "StartCredits", 10000);
    start_tech_ = rules_.Get_Int("General", "TechLevel", 10);
    // BuildSpeed=：造 1000 信贷物品要多少分钟（rules 注释；gamemd 0x00670D1B 读入 Rules+0x1748）。
    build_speed_ = rules_.Get_Double("General", "BuildSpeed", 0.7);
    if (build_speed_ <= 0.0) {
        build_speed_ = 0.7;
    }
    auto pct = [&](const char* key, double def) {
        std::string rp = Trim(rules_.Get_String("General", key, ""));
        if (rp.empty()) {
            return def;
        }
        if (!rp.empty() && rp.back() == '%') {
            rp.pop_back();
        }
        const double v = std::atof(rp.c_str());
        if (v > 1.0) {
            return v / 100.0;
        }
        return v > 0.0 ? v : def;
    };
    // RefundPercent / Repair*：0x00670D90 一带读入 RulesClass。
    refund_percent_ = pct("RefundPercent", 0.5);
    repair_percent_ = pct("RepairPercent", 0.15);
    repair_rate_ = rules_.Get_Double("General", "RepairRate", 0.016);
    repair_step_ = rules_.Get_Int("General", "RepairStep", 8);
    irepair_step_ = rules_.Get_Int("General", "IRepairStep", 20);
    // ConditionYellow/Red：0x0066B33D / 0x0066B364 读入 Rules+0x1708 / +0x1700。
    condition_yellow_ = pct("ConditionYellow", 0.5);
    condition_red_ = pct("ConditionRed", 0.25);
    min_low_power_prod_ =
        rules_.Get_Double("General", "MinLowPowerProductionSpeed", 0.5);
    max_low_power_prod_ =
        rules_.Get_Double("General", "MaxLowPowerProductionSpeed", 0.8);
    low_power_penalty_ =
        rules_.Get_Double("General", "LowPowerPenaltyModifier", 1.0);
    // 矿 bail 价值：rules [Riparius] Value=（[Tiberiums] 0=Riparius 是普通矿）。
    ore_bail_value_ = rules_.Get_Int("Riparius", "Value", 25);
    if (ore_bail_value_ <= 0) {
        ore_bail_value_ = 25;
    }
    // HarvesterLoadRate= int（0x00670CF4 -> Rules+0x1520，ctor 缺省 2）
    // HarvesterDumpRate= double 分钟（0x00670CD4 -> +0x1528，缺省 0.016）
    harvester_load_rate_ = rules_.Get_Int("General", "HarvesterLoadRate", 2);
    if (harvester_load_rate_ <= 0) {
        harvester_load_rate_ = 2;
    }
    harvester_dump_rate_ =
        rules_.Get_Double("General", "HarvesterDumpRate", 0.016);
    if (harvester_dump_rate_ <= 0.0) {
        harvester_dump_rate_ = 0.016;
    }
    // 抽象前置组：gamemd 0x0066D530 读 PrerequisitePower/Barracks/… 列表。
    prereq_groups_.clear();
    auto load_group = [&](const char* key) {
        std::vector<std::string> list;
        rules_.Get_String_List("General", key, &list);
        std::string tok = key;
        // PrerequisitePower -> POWER
        if (tok.size() > 12 && _strnicmp(tok.c_str(), "Prerequisite", 12) == 0) {
            tok = tok.substr(12);
        }
        for (char& c : tok) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        prereq_groups_[tok] = std::move(list);
    };
    load_group("PrerequisitePower");
    load_group("PrerequisiteBarracks");
    load_group("PrerequisiteFactory");
    load_group("PrerequisiteRadar");
    load_group("PrerequisiteTech");
    load_group("PrerequisiteProc");

    // LandType.Buildable：0x0067420E Get_Bool("Buildable", default=0) → +0x1C。
    // 段名序同 0x839d68；INI 以 ';' 开头的行在 0x00525CD4 被跳过。
    static const char* kLandNames[12] = {
        "Clear", "Road", "Water", "Rock", "Wall", "Tiberium",
        "Beach", "Rough", "Ice", "Railroad", "Tunnel", "Weeds",
    };
    for (int i = 0; i < 12; ++i) {
        land_buildable_[i] =
            rules_.Get_Bool(kLandNames[i], "Buildable", false);
    }
    // ShortGame @ 全局 0xa8b262；rules 显式 ShortGame=yes。
    short_game_ = rules_.Get_Bool("General", "ShortGame", true);
    base_units_.clear();
    rules_.Get_String_List("General", "BaseUnit", &base_units_);
    // FogOfWar @0x00671EA0 Get_Bool；联机局常由会话覆盖，缺省读 rules。
    fog_of_war_ = rules_.Get_Bool("General", "FogOfWar", false);
    // SpecialFlags.MCVDeploy @0x006B8BB5 / 0x006B8CE6：会话旗（非 Deploy 直接赋值）。
    mcv_deploy_ = rules_.Get_Bool("SpecialFlags", "MCVDeploy", false);
    // AIAutoDeployFrameDelay：Rules+0xE2C TypeList，按下标=IQ（0x00670249）。
    ai_auto_deploy_delay_.clear();
    rules_.Get_Int_List("General", "AIAutoDeployFrameDelay", &ai_auto_deploy_delay_);
    // MaxIQLevels @ Rules+0x1434（段 [IQ]；House::Read_INI IQ clamp @0x00500D9A）。
    max_iq_levels_ = rules_.Get_Int("IQ", "MaxIQLevels", 5);
    if (max_iq_levels_ < 1) {
        max_iq_levels_ = 1;
    }
    // [IQ] 阈值表（0x00674240，段名 ptr 0x824DD8="IQ"）：
    // MaxIQLevels/+0x1434，SuperWeapons/+0x1438，Production/+0x143c，
    // GuardArea/+0x1440，RepairSell/+0x1444，AutoCrush/+0x1448。
    // House::AI @0x4F85A3：IQ(+0x24C) >= Production → 置 +0x1F3/+0x1EE/+0x1EF。
    iq_super_weapons_ = rules_.Get_Int("IQ", "SuperWeapons", 4);
    iq_production_ = rules_.Get_Int("IQ", "Production", 5);
    iq_guard_area_ = rules_.Get_Int("IQ", "GuardArea", 2);
    iq_repair_sell_ = rules_.Get_Int("IQ", "RepairSell", 1);
    iq_auto_crush_ = rules_.Get_Int("IQ", "AutoCrush", 2);
    iq_scatter_ = rules_.Get_Int("IQ", "Scatter", 2);
    iq_content_scan_ = rules_.Get_Int("IQ", "ContentScan", 3);
    iq_aircraft_ = rules_.Get_Int("IQ", "Aircraft", 3);
    iq_harvester_ = rules_.Get_Int("IQ", "Harvester", 2);
    iq_sell_back_ = rules_.Get_Int("IQ", "SellBack", 2);
}

void UnitModelDB::Collect_Overlays() {
    overlay_images_.clear();
    overlay_new_theater_.clear();
    const IniSection* s = rules_.Find_Section("OverlayTypes");
    if (s == nullptr) {
        return;
    }
    int maxn = -1;
    std::vector<std::pair<int, std::string>> items;
    items.reserve(s->entries.size());
    for (const IniEntry& e : s->entries) {
        if (e.key.empty()) {
            continue;
        }
        bool digits = true;
        for (char c : e.key) {
            if (c < '0' || c > '9') {
                digits = false;
                break;
            }
        }
        if (!digits) {
            continue;
        }
        const int n = std::atoi(e.key.c_str());
        if (n < 0) {
            continue;
        }
        items.emplace_back(n, Upper(Trim(e.value)));
        if (n > maxn) {
            maxn = n;
        }
    }
    if (maxn < 0) {
        return;
    }
    overlay_images_.assign(static_cast<size_t>(maxn) + 1, std::string());
    overlay_new_theater_.assign(static_cast<size_t>(maxn) + 1, 0);
    overlay_is_ore_.assign(static_cast<size_t>(maxn) + 1, 0);
    for (const auto& it : items) {
        const std::string& name = it.second;
        if (name.empty()) {
            continue;
        }
        std::string image = Upper(Trim(rules_.Get_String(name.c_str(), "Image", "")));
        if (image.empty()) {
            image = Upper(Trim(art_.Get_String(name.c_str(), "Image", "")));
        }
        if (image.empty()) {
            image = name;
        }
        overlay_images_[static_cast<size_t>(it.first)] = image;
        const std::string nt = art_.Get_String(name.c_str(), "NewTheater", "");
        if (!nt.empty() && (nt[0] == 'y' || nt[0] == 'Y' || nt[0] == '1')) {
            overlay_new_theater_[static_cast<size_t>(it.first)] = 1;
        }
        // 矿/宝石：仅认 rules 段 Tiberium=yes（不按名字猜）。
        const bool ore = rules_.Get_Bool(name.c_str(), "Tiberium", false);
        overlay_is_ore_[static_cast<size_t>(it.first)] = ore ? 1 : 0;
    }
}

bool UnitModelDB::Overlay_Is_Ore(int index) const {
    if (index < 0 || index >= static_cast<int>(overlay_is_ore_.size())) {
        return false;
    }
    return overlay_is_ore_[static_cast<size_t>(index)] != 0;
}

const std::vector<std::string>& UnitModelDB::Prereq_Group(const char* token) const {
    static const std::vector<std::string> kEmpty;
    if (token == nullptr || token[0] == '\0') {
        return kEmpty;
    }
    std::string key = Upper(Trim(token));
    const auto it = prereq_groups_.find(key);
    if (it == prereq_groups_.end()) {
        return kEmpty;
    }
    return it->second;
}

UnitModelPart UnitModelDB::Make_Part(const std::string& image, const char* suffix,
                                     const char* ext) {
    UnitModelPart p;
    p.name = image + suffix + "." + ext;
    p.id = MixFileClass::CRC_Of(p.name.c_str());
    return p;
}

const UnitModel* UnitModelDB::Resolve(const char* unit) {
    if (!loaded_ || !unit || !*unit) {
        return nullptr;
    }
    const std::string key = Upper(Trim(unit));
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        return &it->second;
    }

    UnitModel m;
    m.unit = key;

    // Image= 缺省就是单位名本身（实测 ZEP 就没写 Image=）。
    std::string image = Trim(rules_.Get_String(key.c_str(), "Image", ""));
    if (image.empty()) {
        image = key;
    }
    m.image = Upper(image);

    m.voxel = art_.Get_Bool(m.image.c_str(), "Voxel", false);
    m.turret = rules_.Get_Bool(key.c_str(), "Turret", false);

    m.body = Make_Part(m.image, "", "VXL");
    m.body_hva = Make_Part(m.image, "", "HVA");
    m.body.present = Have(m.body.id);
    m.body_hva.present = Have(m.body_hva.id);

    if (m.turret) {
        m.turret_vxl = Make_Part(m.image, "TUR", "VXL");
        m.turret_hva = Make_Part(m.image, "TUR", "HVA");
        m.turret_vxl.present = Have(m.turret_vxl.id);
        m.turret_hva.present = Have(m.turret_hva.id);

        m.barrel_vxl = Make_Part(m.image, "BARL", "VXL");
        m.barrel_hva = Make_Part(m.image, "BARL", "HVA");
        m.barrel_vxl.present = Have(m.barrel_vxl.id);
        m.barrel_hva.present = Have(m.barrel_hva.id);
    }

    std::vector<int> flh;
    if (art_.Get_Int_List(m.image.c_str(), "PrimaryFireFLH", &flh) >= 3) {
        m.flh[0] = flh[0];
        m.flh[1] = flh[1];
        m.flh[2] = flh[2];
    }
    m.turret_offset = art_.Get_Int(m.image.c_str(), "TurretOffset", 0);

    // ---- 建造字段（侧栏/生产要用）----
    const char* k = key.c_str();
    m.cost = rules_.Get_Int(k, "Cost", 0);
    m.tech_level = rules_.Get_Int(k, "TechLevel", -1);
    m.power = rules_.Get_Int(k, "Power", 0);
    m.strength = rules_.Get_Int(k, "Strength", 0);
    m.sight = rules_.Get_Int(k, "Sight", 0);
    m.speed = rules_.Get_Int(k, "Speed", 0);
    m.build_time = rules_.Get_Int(k, "BuildTime", 1);
    m.cameo = Trim(art_.Get_String(m.image.c_str(), "Cameo", ""));
    if (m.cameo.empty()) {
        m.cameo = Trim(art_.Get_String(k, "Cameo", ""));
    }
    m.cameo = Upper(m.cameo);
    // 占地：建筑用 art Foundation=NxM（0x0046122D）；规则里的 Width/Height 几乎不写。
    m.width = 1;
    m.height = 1;
    {
        std::string foundation =
            Trim(art_.Get_String(m.image.c_str(), "Foundation", ""));
        if (foundation.empty()) {
            foundation = Trim(art_.Get_String(k, "Foundation", ""));
        }
        if (!foundation.empty()) {
            int fw = 0, fh = 0;
            const char* p = foundation.c_str();
            while (*p == ' ' || *p == '\t') {
                ++p;
            }
            while (*p >= '0' && *p <= '9') {
                fw = fw * 10 + (*p++ - '0');
            }
            if (*p == 'x' || *p == 'X') {
                ++p;
                while (*p >= '0' && *p <= '9') {
                    fh = fh * 10 + (*p++ - '0');
                }
            }
            if (fw >= 1) {
                m.width = fw;
            }
            if (fh >= 1) {
                m.height = fh;
            }
        } else {
            m.width = rules_.Get_Int(k, "Width", 1);
            m.height = rules_.Get_Int(k, "Height", 1);
        }
        if (m.width < 1) {
            m.width = 1;
        }
        if (m.height < 1) {
            m.height = 1;
        }
    }
    {
        const std::string nt =
            art_.Get_String(m.image.c_str(), "NewTheater", "");
        m.new_theater =
            !nt.empty() && (nt[0] == 'y' || nt[0] == 'Y' || nt[0] == '1');
    }
    {
        // Remapable 缺省 yes（原版艺术资源大量省略此键）。
        const std::string rm =
            art_.Get_String(m.image.c_str(), "Remapable", "");
        if (!rm.empty() && (rm[0] == 'n' || rm[0] == 'N' || rm[0] == '0')) {
            m.remapable = false;
        } else {
            m.remapable = true;
        }
    }
    m.has_weapon = !Trim(rules_.Get_String(k, "Primary", "")).empty();
    m.wall = rules_.Get_Bool(k, "Wall", false);
    m.storage = rules_.Get_Int(k, "Storage", 0);
    m.harvester = rules_.Get_Bool(k, "Harvester", false);
    {
        // PlaceAnywhere → BuildingType+0x1703（0x00460F0F Get_Bool，缺省保留字段原值=0）。
        const std::string pa =
            art_.Get_String(m.image.c_str(), "PlaceAnywhere", "");
        const std::string pb =
            pa.empty() ? art_.Get_String(k, "PlaceAnywhere", "") : pa;
        m.place_anywhere =
            !pb.empty() && (pb[0] == 'y' || pb[0] == 'Y' || pb[0] == '1');
    }
    // Adjacent @0x0045FFB7 Get_Int → +0xEB4；未写时保留 ctor 缺省（实测常见 1，CY=2）。
    m.adjacent = rules_.Get_Int(k, "Adjacent", 1);
    if (m.adjacent < 0) {
        m.adjacent = 0;
    }
    // BaseNormal @0x004601F1 Get_Bool → +0x154F；缺省 yes（墙等显式 BaseNormal=no）。
    m.base_normal = rules_.Get_Bool(k, "BaseNormal", true);
    rules_.Get_String_List(k, "Owner", &m.owners);
    rules_.Get_String_List(k, "Prerequisite", &m.prereq);
    m.primary = Trim(rules_.Get_String(k, "Primary", ""));
    m.armor = Trim(rules_.Get_String(k, "Armor", "none"));
    m.deploys_into = Trim(rules_.Get_String(k, "DeploysInto", ""));
    // Factory=：兵营/车厂/船坞/机场/建造场写抽象类型名；无则空（电厂等非工厂）。
    m.factory = Trim(rules_.Get_String(k, "Factory", ""));
    // ArmorTypes 硬编码顺序（Ares / ModEnc Verses 11 档）：
    // none flak plate light medium heavy wood steel concrete special_1 special_2
    {
        static const char* kArmor[] = {
            "none", "flak", "plate", "light", "medium", "heavy",
            "wood", "steel", "concrete", "special_1", "special_2"};
        m.armor_index = 0;
        for (int i = 0; i < 11; ++i) {
            if (_stricmp(m.armor.c_str(), kArmor[i]) == 0) {
                m.armor_index = i;
                break;
            }
        }
    }
    if (!m.primary.empty()) {
        const char* w = m.primary.c_str();
        m.damage = rules_.Get_Int(w, "Damage", 0);
        m.rof = rules_.Get_Int(w, "ROF", 0);
        m.range = static_cast<float>(rules_.Get_Double(w, "Range", 0.0));
        // WeaponType::Read @0x007722F5：Speed → +0xa8（缺省走 Get_Int 默认实参）。
        m.weapon_speed = rules_.Get_Int(w, "Speed", 0);
        m.projectile = Trim(rules_.Get_String(w, "Projectile", ""));
        if (!m.projectile.empty()) {
            const char* p = m.projectile.c_str();
            // BulletType::Read @0x0046BEE0 布尔字段；Image 继承 ObjectType（缺省=类型名）。
            m.proj_image = Trim(rules_.Get_String(p, "Image", p));
            if (m.proj_image.empty()) {
                m.proj_image = m.projectile;
            }
            m.proj_arcing = rules_.Get_Bool(p, "Arcing", false);
            m.proj_inviso = rules_.Get_Bool(p, "Inviso", false);
            m.proj_proximity = rules_.Get_Bool(p, "Proximity", false);
        }
        const std::string wh = Trim(rules_.Get_String(w, "Warhead", ""));
        if (!wh.empty()) {
            std::vector<std::string> vs;
            if (rules_.Get_String_List(wh.c_str(), "Verses", &vs) > 0) {
                for (int i = 0; i < 11 && i < static_cast<int>(vs.size()); ++i) {
                    // "75%" / "75" / "0.75"
                    std::string s = Trim(vs[static_cast<size_t>(i)]);
                    if (!s.empty() && s.back() == '%') {
                        s.pop_back();
                    }
                    const double v = std::atof(s.c_str());
                    if (v <= 1.0 && s.find('.') != std::string::npos) {
                        m.verses[i] = static_cast<int>(v * 100.0 + 0.5);
                    } else {
                        m.verses[i] = static_cast<int>(v + 0.5);
                    }
                }
            }
        }
    }
    {
        const auto cit = category_.find(key);
        m.category = (cit != category_.end()) ? cit->second : -1;
    }
    // Harvester= 以 rules 为准（0x00674240 读入）；不按 Storage 猜。

    // ---- 统计只在首次解析时累加 ----
    if (m.voxel) {
        ++stats_.voxel;
        if (!m.body.present) {
            ++stats_.body_missing;
        }
        if (m.turret_vxl.present) {
            ++stats_.with_turret;
        }
        if (m.barrel_vxl.present) {
            ++stats_.with_barrel;
        }
    }

    auto ins = cache_.emplace(key, m);
    return &ins.first->second;
}

void UnitModelDB::Dump(bool only_voxel) const {
    std::printf("%-12s %-12s %-6s %-6s %s\n", "单位", "Image", "炮塔", "炮管", "车体 ID");
    for (const std::string& u : units_) {
        auto it = cache_.find(u);
        if (it == cache_.end()) {
            continue;
        }
        const UnitModel& m = it->second;
        if (only_voxel && !m.voxel) {
            continue;
        }
        std::printf("%-12s %-12s %-6s %-6s %s0x%08X %s\n",
                    m.unit.c_str(), m.image.c_str(),
                    m.turret_vxl.present ? "有" : (m.turret ? "缺" : "-"),
                    m.barrel_vxl.present ? "有" : "-",
                    m.body.present ? "" : "!",
                    m.body.id, m.body.present ? "" : "车体缺失");
    }
    const Stats& s = stats_;
    std::printf("\n段: rules=%d art=%d | 单位 %d | 体素 %d | 炮塔 %d | 炮管 %d | 车体缺失 %d\n",
                s.rules_sections, s.art_sections, s.units, s.voxel,
                s.with_turret, s.with_barrel, s.body_missing);
}

}  // namespace ra2
