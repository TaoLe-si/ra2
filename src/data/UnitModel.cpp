// UnitModel.cpp -- 见 UnitModel.h。

#include "data/UnitModel.h"

#include <cstdio>
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
        // TechLevel = -1 是"没写"，不是"1 级"——写漏了会把它当高级货滤掉。
        if (tech_level >= 0 && m.tech_level >= 0 && m.tech_level > tech_level) {
            continue;
        }
        out->push_back(id);
    }
    return static_cast<int>(out->size());
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
    m.speed = rules_.Get_Int(k, "Speed", 0);
    m.build_time = rules_.Get_Int(k, "BuildTime", 1);
    m.width = rules_.Get_Int(k, "Width", 1);
    m.height = rules_.Get_Int(k, "Height", 1);
    if (m.width < 1) m.width = 1;
    if (m.height < 1) m.height = 1;
    m.has_weapon = !Trim(rules_.Get_String(k, "Primary", "")).empty();
    m.wall = rules_.Get_Bool(k, "Wall", false);
    rules_.Get_String_List(k, "Owner", &m.owners);
    rules_.Get_String_List(k, "Prerequisite", &m.prereq);
    {
        const auto cit = category_.find(key);
        m.category = (cit != category_.end()) ? cit->second : -1;
    }

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
