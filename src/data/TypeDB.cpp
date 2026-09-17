// TypeDB.cpp -- 见 TypeDB.h。

#include "data/TypeDB.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <set>

#include "data/UnitModel.h"
#include "io/FileSystem.h"

namespace ra2 {
namespace {

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

/// 把 MIX 里的 name 合并进 ini。找不到返回 false。
bool Merge_Mix(const MixFileClass& mix, const char* name, IniFile* ini,
               bool overwrite) {
    const std::vector<uint8_t> d = mix.Read_Deep(name);
    if (d.empty()) {
        return false;
    }
    return ini->Merge(d.data(), d.size(), overwrite);
}

/// 键排序：先比大写形式，再比原串 —— 必须是**全序**，否则同一段里
/// 只差大小写的两个键（如 `Name` 与 `NAME`）在两边会排出不同顺序，
/// 跨实现的逐行 diff 就会凭空多出差异。
bool Key_Less(const std::string& a, const std::string& b) {
    const std::string ua = Upper(a);
    const std::string ub = Upper(b);
    if (ua != ub) {
        return ua < ub;
    }
    return a < b;
}

}  // namespace

// ---------------------------------------------------------------------------
// ValueMap
// ---------------------------------------------------------------------------

void ValueMap::Add(const std::string& key, const std::string& value) {
    const std::string k = Upper(key);
    if (index_.find(k) != index_.end()) {
        return;   // 第一个出现者胜，与 IniFile::Get_String 一致
    }
    index_[k] = items_.size();
    items_.emplace_back(key, value);
}

const std::string* ValueMap::Find(const char* key) const {
    if (key == nullptr) {
        return nullptr;
    }
    const auto it = index_.find(Upper(key));
    if (it == index_.end()) {
        return nullptr;
    }
    return &items_[it->second].second;
}

std::string ValueMap::Get(const char* key, const char* def) const {
    const std::string* v = Find(key);
    return v ? *v : std::string(def ? def : "");
}

int ValueMap::Get_Int(const char* key, int def) const {
    const std::string* v = Find(key);
    return v ? ini_value::As_Int(v->c_str()) : def;
}

double ValueMap::Get_Double(const char* key, double def) const {
    const std::string* v = Find(key);
    return v ? ini_value::As_Double(v->c_str(), def) : def;
}

bool ValueMap::Get_Bool(const char* key, bool def) const {
    const std::string* v = Find(key);
    return v ? ini_value::As_Bool(v->c_str(), def) : def;
}

int ValueMap::Get_Int_List(const char* key, std::vector<int>* out) const {
    if (out == nullptr) {
        return 0;
    }
    const std::string* v = Find(key);
    if (v == nullptr) {
        out->clear();
        return 0;
    }
    return ini_value::As_Int_List(v->c_str(), out);
}

int ValueMap::Get_String_List(const char* key,
                              std::vector<std::string>* out) const {
    if (out == nullptr) {
        return 0;
    }
    const std::string* v = Find(key);
    if (v == nullptr) {
        out->clear();
        return 0;
    }
    return ini_value::As_String_List(v->c_str(), out);
}

// ---------------------------------------------------------------------------
// TechnoType
// ---------------------------------------------------------------------------

void TechnoType::Fill_Typed() {
    // Pick：按来源串挑一张表，取不到就给一个空的常驻字符串。
    // 用 lambda 而不是 if/else 展开，是为了让 X-macro 只写一次来源标记。
    static const std::string kEmpty;
    auto Pick = [this](char src, const char* key) -> const std::string& {
        const ValueMap& m = (src == 'R') ? rules : art;
        const std::string* v = m.Find(key);
        return v ? *v : kEmpty;
    };

#define RA2_TT_FILL(f, src, key, ty, def) RA2_TT_FILL_##ty(f, src, key, def)
#define RA2_TT_FILL_INT(f, src, key, def) \
    f = ini_value::As_Int(Pick(src, key).c_str());
#define RA2_TT_FILL_DBL(f, src, key, def) \
    f = ini_value::As_Double(Pick(src, key).c_str(), def);
#define RA2_TT_FILL_BOOL(f, src, key, def) \
    f = ini_value::As_Bool(Pick(src, key).c_str(), def);
#define RA2_TT_FILL_STR(f, src, key, def) f = Pick(src, key);
#define RA2_TT_FILL_ILIST(f, src, key, def) \
    ini_value::As_Int_List(Pick(src, key).c_str(), &f);
#define RA2_TT_FILL_SLIST(f, src, key, def) \
    ini_value::As_String_List(Pick(src, key).c_str(), &f);
    RA2_TECHNO_FIELDS(RA2_TT_FILL)
#undef RA2_TT_FILL
#undef RA2_TT_FILL_INT
#undef RA2_TT_FILL_DBL
#undef RA2_TT_FILL_BOOL
#undef RA2_TT_FILL_STR
#undef RA2_TT_FILL_ILIST
#undef RA2_TT_FILL_SLIST

    // 键存在但值为空时，INT 走 As_Int("") = 0 —— 与 IniFile::Get_Int 一致
    // （见 Ini.h 的"空串语义必须区分"）。这里不用再补一刀。
}

void TechnoType::Typed_Keys(std::vector<std::string>* out) {
    if (out == nullptr) {
        return;
    }
    out->clear();
    std::set<std::string> seen;
    // 键名统一大写：真实数据的键集合是按大写去重统计的，两边必须同一个口径，
    // 否则覆盖率会算出 0%（第一次跑就是这么错的，见 build/_tt.log 的第一版）。
#define RA2_TT_KEY(f, src, key, ty, def)                       \
    do {                                                       \
        const std::string k = std::string(1, src) + ":" + Upper(key); \
        if (seen.insert(k).second) {                           \
            out->push_back(k);                                 \
        }                                                      \
    } while (0);
    RA2_TECHNO_FIELDS(RA2_TT_KEY)
#undef RA2_TT_KEY
}

int TechnoType::Typed_Key_Count() {
    std::vector<std::string> v;
    Typed_Keys(&v);
    return static_cast<int>(v.size());
}

// ---------------------------------------------------------------------------
// SoundDB / ThemeDB
// ---------------------------------------------------------------------------

const ValueMap* SoundDB::Find(const char* name) const {
    if (name == nullptr || *name == '\0') {
        return nullptr;
    }
    const auto it = defs.find(Upper(Trim(name)));
    return (it == defs.end()) ? nullptr : &it->second;
}

const ThemeType* ThemeDB::Find(const char* id) const {
    if (id == nullptr) {
        return nullptr;
    }
    const std::string k = Upper(Trim(id));
    for (const ThemeType& t : themes) {
        if (Upper(t.id) == k) {
            return &t;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// TypeDB
// ---------------------------------------------------------------------------

const std::map<std::string, TechnoType>& TypeDB::Types() const { return types_; }

const TechnoType* TypeDB::Type(const char* id) const {
    if (id == nullptr || *id == '\0') {
        return nullptr;
    }
    const auto it = types_.find(Upper(Trim(id)));
    return (it == types_.end()) ? nullptr : &it->second;
}

const WeaponType* TypeDB::Weapon(const char* id) const {
    if (id == nullptr || *id == '\0') {
        return nullptr;
    }
    const auto it = weapons_.find(Upper(Trim(id)));
    return (it == weapons_.end()) ? nullptr : &it->second;
}

const WarheadType* TypeDB::Warhead(const char* id) const {
    if (id == nullptr || *id == '\0') {
        return nullptr;
    }
    const auto it = warheads_.find(Upper(Trim(id)));
    return (it == warheads_.end()) ? nullptr : &it->second;
}

const ProjectileType* TypeDB::Projectile(const char* id) const {
    if (id == nullptr || *id == '\0') {
        return nullptr;
    }
    const auto it = projectiles_.find(Upper(Trim(id)));
    return (it == projectiles_.end()) ? nullptr : &it->second;
}

bool TypeDB::Load(const UnitModelDB& unitdb, const MixFileClass* const* mixes,
                  int count) {
    types_.clear();
    weapons_.clear();
    warheads_.clear();
    projectiles_.clear();
    sounds_ = SoundDB();
    themes_ = ThemeDB();
    stats_ = Stats();
    if (!unitdb.Loaded()) {
        std::printf("[x] UnitModelDB 还没加载，TypeDB 无从下手\n");
        return false;
    }
    const IniFile& rules = unitdb.Rules();
    const IniFile& art = unitdb.Art();

    Collect_Types(unitdb);
    Build_Weapons(rules);
    Build_Warheads(rules);
    Build_Projectiles(rules);
    Load_Sounds(mixes, count);
    Load_Themes(mixes, count);
    stats_.ai_ini = Has_AI_INI(mixes, count, nullptr);

    stats_.types = static_cast<int>(types_.size());
    stats_.weapons = static_cast<int>(weapons_.size());
    stats_.warheads = static_cast<int>(warheads_.size());
    stats_.projectiles = static_cast<int>(projectiles_.size());
    stats_.sounds = sounds_.Count();
    stats_.themes = static_cast<int>(themes_.themes.size());
    stats_.typed_keys = TechnoType::Typed_Key_Count();
    loaded_ = true;
    return true;
}

void TypeDB::Collect_Types(const UnitModelDB& unitdb) {
    const IniFile& rules = unitdb.Rules();
    const IniFile& art = unitdb.Art();

    // 页签 -> 类别号。Tab_Units 里的才是"游戏认得的类型"，
    // 其余（不在四个列表里的段）不收 —— 收了会把 [General] 之类也算成单位。
    std::unordered_map<std::string, int> cat;
    for (int tab = 0; tab < 4; ++tab) {
        for (const std::string& u : unitdb.Tab_Units(tab)) {
            cat[u] = tab;
        }
    }

    std::set<std::string> rk, ak;
    units_missing_.clear();
    for (const std::string& name : unitdb.Units()) {
        const std::string id = Upper(Trim(name));
        if (id.empty()) {
            continue;
        }
        const IniSection* rs = rules.Find_Section(id.c_str());
        if (rs == nullptr) {
            // 列表里有名字、rules 里没有段。**这是原版数据自带的不一致**，不是解析错：
            // 实测本安装 559 个名字里有 6 个如此 —— YDUM / APACHE / CASYDN01 /
            // CATIME / CALA02 / CALOND02（RA2 时代的老机型，YR 的 rulesmd 里
            // [AircraftTypes] 仍列着 APACHE，但全库找不到 [APACHE] 段）。
            // 所以这里跳过并**如实记名**，不假装解析成功。
            units_missing_.push_back(id);
            continue;
        }
        TechnoType t;
        t.id = id;
        std::string image = Trim(rules.Get_String(id.c_str(), "Image", ""));
        t.image = image.empty() ? id : Upper(image);
        for (const IniEntry& e : rs->entries) {
            t.rules.Add(e.key, e.value);
            rk.insert(Upper(e.key));
            ++stats_.rules_key_hits;
        }
        if (const IniSection* as = art.Find_Section(t.image.c_str())) {
            for (const IniEntry& e : as->entries) {
                t.art.Add(e.key, e.value);
                ak.insert(Upper(e.key));
                ++stats_.art_key_hits;
            }
        }
        const auto cit = cat.find(id);
        t.category = (cit == cat.end()) ? -1 : cit->second;
        t.Fill_Typed();
        types_.emplace(id, std::move(t));
    }
    stats_.rules_keys = static_cast<int>(rk.size());
    stats_.art_keys = static_cast<int>(ak.size());
    stats_.units_in_lists = static_cast<int>(unitdb.Units().size());
    stats_.units_missing_section = static_cast<int>(units_missing_.size());
}

void TypeDB::Build_Weapons(const IniFile& rules) {
    // 武器名从**单位段引用的键**收：Primary/Secondary/Elite*/Weapon1..5/
    // DeathWeapon/OccupyWeapon/EliteOccupyWeapon。
    //
    // 【为什么不用"有 Damage= 的就是武器"这种形状判据收全库】那会收进一堆
    // 游戏自己都不引用的段，而且判据本身是猜的。这里的口径是"单位实际引用到的"——
    // 正好是 P3 需要的集合。全库的武器形状段数只作为**统计**报出来做参照
    // （stats_.weapons_by_signature），不进表。
    std::set<std::string> names;
    auto add = [&names, &rules](const std::string& w) {
        const std::string s = Upper(Trim(w));
        if (!s.empty() && rules.Has_Section(s.c_str())) {
            names.insert(s);
        }
    };
    for (const auto& kv : types_) {
        const TechnoType& t = kv.second;
        add(t.primary);
        add(t.secondary);
        add(t.elite_primary);
        add(t.elite_secondary);
        add(t.weapon1);
        add(t.weapon2);
        add(t.weapon3);
        add(t.weapon4);
        add(t.weapon5);
        add(t.death_weapon);
        add(t.occupy_weapon);
        add(t.elite_occupy_weapon);
    }
    for (const std::string& n : names) {
        const IniSection* s = rules.Find_Section(n.c_str());
        if (s == nullptr) {
            continue;
        }
        WeaponType w;
        w.id = n;
        for (const IniEntry& e : s->entries) {
            w.kv.Add(e.key, e.value);
        }
        w.damage = w.kv.Get_Int("Damage", 0);
        w.rof = w.kv.Get_Int("ROF", 0);
        w.range = w.kv.Get_Double("Range", 0.0);
        w.speed = w.kv.Get_Int("Speed", 0);
        w.burst = w.kv.Get_Int("Burst", 0);
        w.minimum_range = w.kv.Get_Int("MinimumRange", 0);
        w.ammo = w.kv.Get_Int("Ammo", 0);
        w.projectile = Upper(Trim(w.kv.Get("Projectile")));
        w.warhead = Upper(Trim(w.kv.Get("Warhead")));
        w.report = Trim(w.kv.Get("Report"));
        w.anim = Upper(Trim(w.kv.Get("Anim")));
        w.omni_fire = w.kv.Get_Bool("OmniFire", false);
        weapons_.emplace(n, std::move(w));
    }
    // 统计参照：全库里同时有 Damage= 与 Warhead= 的段有多少个（含没被引用的）。
    int sig = 0;
    for (int i = 0; i < rules.Section_Count(); ++i) {
        const IniSection* s = rules.Section(i);
        if (s == nullptr) {
            continue;
        }
        const IniFile& r = rules;
        if (r.Get_Int(s->name.c_str(), "Damage", -1) >= 0 &&
            !Trim(r.Get_String(s->name.c_str(), "Warhead", "")).empty()) {
            ++sig;
        }
    }
    stats_.weapons_by_signature = sig;
}

void TypeDB::Build_Warheads(const IniFile& rules) {
    // 弹头有两个来源，取并集：
    //   1. **引擎自己的登记表 [Warheads]**（编号列表）—— 这是权威名单；
    //   2. 武器段 Warhead= 引用到的名字（万一有没登记进列表的）。
    std::set<std::string> names;
    {
        std::vector<std::string> list;
        rules.Read_Numbered_List("Warheads", &list);
        stats_.warheads_in_list = static_cast<int>(list.size());
        for (const std::string& n : list) {
            const std::string s = Upper(Trim(n));
            if (!s.empty() && rules.Has_Section(s.c_str())) {
                names.insert(s);
            }
        }
    }
    for (const auto& kv : weapons_) {
        const std::string s = Upper(Trim(kv.second.warhead));
        if (!s.empty() && rules.Has_Section(s.c_str())) {
            names.insert(s);
        }
    }
    for (const std::string& n : names) {
        const IniSection* s = rules.Find_Section(n.c_str());
        if (s == nullptr) {
            continue;
        }
        WarheadType h;
        h.id = n;
        for (const IniEntry& e : s->entries) {
            h.kv.Add(e.key, e.value);
        }
        std::vector<int> v;
        if (h.kv.Get_Int_List("Verses", &v) > 0) {
            h.verses_present = true;
            for (int i = 0; i < 11; ++i) {
                h.verses[i] = (i < static_cast<int>(v.size()))
                                  ? v[static_cast<size_t>(i)]
                                  : 0;
            }
        }
        h.cell_spread = h.kv.Get_Int("CellSpread", 0);
        h.anim_list = Trim(h.kv.Get("AnimList"));
        warheads_.emplace(n, std::move(h));
    }
}

void TypeDB::Build_Projectiles(const IniFile& rules) {
    std::set<std::string> names;
    for (const auto& kv : weapons_) {
        const std::string s = Upper(Trim(kv.second.projectile));
        if (!s.empty() && rules.Has_Section(s.c_str())) {
            names.insert(s);
        }
    }
    for (const std::string& n : names) {
        const IniSection* s = rules.Find_Section(n.c_str());
        if (s == nullptr) {
            continue;
        }
        ProjectileType p;
        p.id = n;
        for (const IniEntry& e : s->entries) {
            p.kv.Add(e.key, e.value);
        }
        // Image= 缺省 = 类型名（ObjectType 的继承语义，BulletType::Read @0x0046BEE0）。
        std::string img = Trim(p.kv.Get("Image"));
        p.image = img.empty() ? n : Upper(img);
        p.inviso = p.kv.Get_Bool("Inviso", false);
        p.arcing = p.kv.Get_Bool("Arcing", false);
        p.proximity = p.kv.Get_Bool("Proximity", false);
        projectiles_.emplace(n, std::move(p));
    }
}

void TypeDB::Load_Sounds(const MixFileClass* const* mixes, int count) {
    IniFile ini;
    int got = 0;
    for (int pass = 0; pass < 2; ++pass) {
        const bool overwrite = (pass == 1);
        const char* name = (pass == 0) ? "SOUND.INI" : "SOUNDMD.INI";
        for (int i = 0; i < count; ++i) {
            if (mixes[i] != nullptr && Merge_Mix(*mixes[i], name, &ini, overwrite)) {
                ++got;
            }
        }
    }
    if (got == 0) {
        return;   // 本安装没有 sound.ini —— 不是错误，自检里如实报
    }
    stats_.sound_ini = true;
    std::vector<std::string> names;
    ini.Read_Numbered_List("SoundList", &names);
    int maxn = -1;
    {
        const IniSection* s = ini.Find_Section("SoundList");
        if (s != nullptr) {
            for (const IniEntry& e : s->entries) {
                bool digits = !e.key.empty();
                for (char c : e.key) {
                    if (c < '0' || c > '9') {
                        digits = false;
                        break;
                    }
                }
                if (digits) {
                    const int n = std::atoi(e.key.c_str());
                    if (n > maxn) {
                        maxn = n;
                    }
                }
            }
        }
    }
    if (maxn < 0) {
        return;
    }
    // 按编号落位：编号 -> 名字。名 → 编号是反查表。
    sounds_.list.assign(static_cast<size_t>(maxn) + 1, std::string());
    const IniSection* sl = ini.Find_Section("SoundList");
    for (const IniEntry& e : sl->entries) {
        bool digits = !e.key.empty();
        for (char c : e.key) {
            if (c < '0' || c > '9') {
                digits = false;
                break;
            }
        }
        if (!digits) {
            continue;
        }
        const std::string name = Upper(Trim(e.value));
        if (name.empty()) {
            continue;
        }
        const int n = std::atoi(e.key.c_str());
        sounds_.list[static_cast<size_t>(n)] = name;
        sounds_.list_index[name] = n;
    }
    for (const std::string& n : names) {
        const std::string key = Upper(Trim(n));
        if (key.empty()) {
            continue;
        }
        const IniSection* s = ini.Find_Section(key.c_str());
        if (s == nullptr) {
            continue;
        }
        ValueMap& m = sounds_.defs[key];
        for (const IniEntry& e : s->entries) {
            m.Add(e.key, e.value);
        }
    }
}

void TypeDB::Load_Themes(const MixFileClass* const* mixes, int count) {
    IniFile ini;
    int got = 0;
    for (int pass = 0; pass < 2; ++pass) {
        const bool overwrite = (pass == 1);
        const char* name = (pass == 0) ? "THEME.INI" : "THEMEMD.INI";
        for (int i = 0; i < count; ++i) {
            if (mixes[i] != nullptr && Merge_Mix(*mixes[i], name, &ini, overwrite)) {
                ++got;
            }
        }
    }
    if (got == 0) {
        return;
    }
    stats_.theme_ini = true;
    ini.Read_Numbered_List("Themes", &themes_.order);
    for (const std::string& raw : themes_.order) {
        const std::string id = Upper(Trim(raw));
        if (id.empty()) {
            continue;
        }
        const IniSection* s = ini.Find_Section(id.c_str());
        if (s == nullptr) {
            continue;
        }
        ThemeType t;
        t.id = id;
        for (const IniEntry& e : s->entries) {
            t.kv.Add(e.key, e.value);
        }
        t.name = Trim(t.kv.Get("Name"));
        t.sound = Upper(Trim(t.kv.Get("Sound")));
        // 缺省值取自 THEME.INI 自己的注释头（文件即证据）：
        //   Normal  def=yes / Scenario def=0 / Repeat def=no / Side 无缺省。
        t.normal = t.kv.Get_Bool("Normal", true);
        t.repeat = t.kv.Get_Bool("Repeat", false);
        t.scenario = t.kv.Get_Int("Scenario", 0);
        t.side = Trim(t.kv.Get("Side"));
        themes_.themes.push_back(std::move(t));
    }
}

bool TypeDB::Has_AI_INI(const MixFileClass* const* mixes, int count,
                        FILE* log) const {
    // AI.INI / AIMD.INI 是原版就有的（旧基线的名字表里有这两个 CRC：
    // AI.INI=0x9E11E49A @ ra2.mix、AIMD.INI=0x116F3F76 @ ra2md.mix）。
    // 但**本机的 Reunion 2023 重打包里两个都没有** —— 这是素材差异，
    // 不是解析问题。所以这里只如实报告"在不在"，不假装读过。
    static const char* const kNames[] = {"AI.INI", "AIMD.INI"};
    for (const char* n : kNames) {
        for (int i = 0; i < count; ++i) {
            if (mixes[i] == nullptr) {
                continue;
            }
            const uint32_t id = MixFileClass::CRC_Of(n);
            if (mixes[i]->Find_By_ID(id) != nullptr) {
                if (log != nullptr) {
                    std::fprintf(log, "  找到 %s（挂在 %s）\n", n,
                                 mixes[i]->Path().c_str());
                }
                return true;
            }
        }
    }
    if (log != nullptr) {
        std::fprintf(log, "  AI.INI / AIMD.INI：本安装的归档里都没有\n");
    }
    return false;
}

// ---------------------------------------------------------------------------
// 对账
// ---------------------------------------------------------------------------

int TypeDB::Verify(const IniFile& rules, const IniFile& art, FILE* log) const {
    int bad = 0;
    auto check_one = [&bad, log](const IniFile& ini, const char* sec,
                                 const ValueMap& map, const char* tag,
                                 const char* unit) {
        const IniSection* s = ini.Find_Section(sec);
        if (s == nullptr) {
            if (!map.Empty()) {
                ++bad;
                if (log) {
                    std::fprintf(log, "[x] %s: 段 [%s] 不存在，表里却有 %zu 个键\n",
                                 unit, sec, map.Size());
                }
            }
            return;
        }
        // 1) 去重键数必须相等（不许丢键、不许凭空添键）
        std::set<std::string> keys;
        for (const IniEntry& e : s->entries) {
            keys.insert(Upper(e.key));
        }
        if (keys.size() != map.Size()) {
            ++bad;
            if (log) {
                std::fprintf(log, "[x] %s %s: 段 [%s] 去重键 %zu != 表 %zu\n",
                             unit, tag, sec, keys.size(), map.Size());
            }
        }
        // 2) 每个键的值逐字节相等
        for (const IniEntry& e : s->entries) {
            const std::string want = ini.Get_String(sec, e.key.c_str());
            const std::string* got = map.Find(e.key.c_str());
            if (got == nullptr || *got != want) {
                ++bad;
                if (log) {
                    std::fprintf(log, "[x] %s %s: %s=%s（表里是 %s）\n", unit, tag,
                                 e.key.c_str(), want.c_str(),
                                 got ? got->c_str() : "<缺>");
                }
            }
        }
        // 3) 反向：表里的键在段里都要有（防"键名被改了个大小写"这类）
        for (size_t i = 0; i < map.Size(); ++i) {
            if (keys.find(Upper(map.Key(i))) == keys.end()) {
                ++bad;
                if (log) {
                    std::fprintf(log, "[x] %s %s: 表里的 %s 在段 [%s] 里没有\n",
                                 unit, tag, map.Key(i).c_str(), sec);
                }
            }
        }
    };
    for (const auto& kv : types_) {
        const TechnoType& t = kv.second;
        check_one(rules, t.id.c_str(), t.rules, "rules", t.id.c_str());
        check_one(art, t.image.c_str(), t.art, "art", t.id.c_str());
    }
    return bad;
}

void TypeDB::Dump_Raw(FILE* f) const {
    if (f == nullptr) {
        return;
    }
    std::vector<std::string> keys;
    for (const auto& kv : types_) {
        const TechnoType& t = kv.second;
        for (int src = 0; src < 2; ++src) {
            const ValueMap& m = (src == 0) ? t.rules : t.art;
            const char* tag = (src == 0) ? "rules" : "art";
            keys.clear();
            for (size_t i = 0; i < m.Size(); ++i) {
                keys.push_back(m.Key(i));
            }
            std::sort(keys.begin(), keys.end(), Key_Less);
            for (const std::string& k : keys) {
                const std::string* v = m.Find(k.c_str());
                std::fprintf(f, "%s\t%s\t%s\t%s\n", t.id.c_str(), tag, k.c_str(),
                             v ? v->c_str() : "");
            }
        }
    }
}

void TypeDB::Dump_Summary(FILE* f, int top_keys) const {
    if (f == nullptr) {
        return;
    }
    std::map<std::string, int> rfreq, afreq;
    for (const auto& kv : types_) {
        for (size_t i = 0; i < kv.second.rules.Size(); ++i) {
            rfreq[Upper(kv.second.rules.Key(i))] += 1;
        }
        for (size_t i = 0; i < kv.second.art.Size(); ++i) {
            afreq[Upper(kv.second.art.Key(i))] += 1;
        }
    }
    std::vector<std::pair<int, std::string>> rv, av;
    for (const auto& p : rfreq) {
        rv.emplace_back(p.second, p.first);
    }
    for (const auto& p : afreq) {
        av.emplace_back(p.second, p.first);
    }
    std::sort(rv.rbegin(), rv.rend());
    std::sort(av.rbegin(), av.rend());

    // 类型化覆盖：X-macro 里声明的键，有多少能在真实数据里找到。
    std::vector<std::string> typed;
    TechnoType::Typed_Keys(&typed);
    std::set<std::string> typed_set(typed.begin(), typed.end());
    int r_typed_hit = 0, a_typed_hit = 0;
    long r_typed_occ = 0, a_typed_occ = 0;
    std::vector<std::pair<int, std::string>> r_untyped, a_untyped;
    for (const auto& p : rfreq) {
        if (typed_set.count("R:" + p.first)) {
            ++r_typed_hit;
            r_typed_occ += p.second;
        } else {
            r_untyped.emplace_back(p.second, p.first);
        }
    }
    for (const auto& p : afreq) {
        if (typed_set.count("A:" + p.first)) {
            ++a_typed_hit;
            a_typed_occ += p.second;
        } else {
            a_untyped.emplace_back(p.second, p.first);
        }
    }
    // 声明的键里，哪些在真实数据里一次都没出现 —— 这既可能是"原版没用到"，
    // 也可能是**键名打错了**（打错的键永远不会命中，而且不会报任何错）。
    // 所以必须逐个列出来看，这是防手误的最后一道闸。
    std::vector<std::string> declared_miss;
    for (const std::string& k : typed) {
        const bool is_rules = (k[0] == 'R');
        const std::string bare = k.substr(2);
        const auto& freq = is_rules ? rfreq : afreq;
        if (freq.find(bare) == freq.end()) {
            declared_miss.push_back(k);
        }
    }
    std::sort(r_untyped.rbegin(), r_untyped.rend());
    std::sort(a_untyped.rbegin(), a_untyped.rend());

    const Stats& s = stats_;
    std::fprintf(f, "类型表：TechnoType %d", s.types);
    if (s.units_missing_section > 0) {
        std::fprintf(f, "（四个类型列表共 %d 个名字，其中 %d 个在原版数据里没有对应段）",
                     s.units_in_lists, s.units_missing_section);
    }
    std::fprintf(f, "\n");
    std::fprintf(f, "  rules 单位段：键种 %d  出现 %d 次  已类型化 %d 种（%.1f%%）"
                    " / 按出现次数 %.1f%%\n",
                 s.rules_keys, s.rules_key_hits, r_typed_hit,
                 s.rules_keys ? 100.0 * r_typed_hit / s.rules_keys : 0.0,
                 s.rules_key_hits ? 100.0 * r_typed_occ / s.rules_key_hits : 0.0);
    std::fprintf(f, "  art  Image 段：键种 %d  出现 %d 次  已类型化 %d 种（%.1f%%）"
                    " / 按出现次数 %.1f%%\n",
                 s.art_keys, s.art_key_hits, a_typed_hit,
                 s.art_keys ? 100.0 * a_typed_hit / s.art_keys : 0.0,
                 s.art_key_hits ? 100.0 * a_typed_occ / s.art_key_hits : 0.0);
    std::fprintf(f, "  X-macro 声明 %d 个键，其中 %d 个在数据里一次都没出现%s\n",
                 s.typed_keys, static_cast<int>(declared_miss.size()),
                 declared_miss.empty() ? "" : "：");
    for (const std::string& k : declared_miss) {
        std::fprintf(f, "      %s\n", k.c_str());
    }
    std::fprintf(f, "武器 %d（全库有 Damage+Warhead 的段共 %d）\n", s.weapons,
                 s.weapons_by_signature);
    std::fprintf(f, "弹头 %d（[Warheads] 列表 %d）  抛射体 %d\n", s.warheads,
                 s.warheads_in_list, s.projectiles);
    std::fprintf(f, "sound.ini %s（%d 个编号）  theme.ini %s（%d 首）  ai.ini %s\n",
                 s.sound_ini ? "有" : "无", s.sounds, s.theme_ini ? "有" : "无",
                 s.themes, s.ai_ini ? "有" : "无");
    if (!units_missing_.empty()) {
        std::fprintf(f, "列表里有名字但无 rules 段（原版数据自身的不一致）：");
        for (size_t i = 0; i < units_missing_.size(); ++i) {
            std::fprintf(f, "%s%s", i ? " " : "", units_missing_[i].c_str());
        }
        std::fprintf(f, "\n");
    }
    if (!themes_.themes.empty()) {
        std::fprintf(f, "theme 曲目：");
        for (size_t i = 0; i < themes_.themes.size(); ++i) {
            std::fprintf(f, "%s%s", i ? " " : "", themes_.themes[i].id.c_str());
        }
        std::fprintf(f, "\n");
    }
    if (top_keys > 0) {
        std::fprintf(f, "\n键出现频次 Top %d（rules / art 分开）：\n", top_keys);
        const int n = std::min<int>(top_keys, static_cast<int>(rv.size()));
        for (int i = 0; i < n; ++i) {
            std::fprintf(f, "  %-28s %4d   |  %-28s %4d\n", rv[i].second.c_str(),
                         rv[i].first,
                         i < static_cast<int>(av.size()) ? av[i].second.c_str() : "",
                         i < static_cast<int>(av.size()) ? av[i].first : 0);
        }
        // 还没类型化的高频键 = 下一步该补哪几个，按频次排，不用猜。
        std::fprintf(f, "\n还没类型化的键 Top %d（按频次；补字段就看这张表）：\n", top_keys);
        const int m = std::min<int>(top_keys, static_cast<int>(r_untyped.size()));
        for (int i = 0; i < m; ++i) {
            std::fprintf(f, "  %-28s %4d   |  %-28s %4d\n",
                         r_untyped[i].second.c_str(), r_untyped[i].first,
                         i < static_cast<int>(a_untyped.size())
                             ? a_untyped[i].second.c_str()
                             : "",
                         i < static_cast<int>(a_untyped.size()) ? a_untyped[i].first
                                                                : 0);
        }
    }
}

}  // namespace ra2
