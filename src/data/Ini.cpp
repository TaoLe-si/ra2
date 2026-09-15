// Ini.cpp
//
// 解析策略：一次线性扫描，逐行处理，不预先切行数组 —— rulesmd.ini 有 31k 行，
// 切行会白白多出几万次小分配。行内定位用指针，只有落进容器时才拷成 std::string。
//
// 大小写不敏感用的是"拷一份小写"而不是自定义 hash/eq：段名和键名总量在
// 十万量级以内，编译期小写表的复杂度不值得。

#include "data/Ini.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <utility>

namespace ra2 {
namespace {

inline bool Is_Space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

inline std::string To_Lower(const char* b, const char* e) {
    std::string s(b, e);
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return s;
}

/// 从 [b,e) 两端去掉空白。返回收窄后的区间。
inline void Trim(const char*& b, const char*& e) {
    while (b < e && Is_Space(*b)) {
        ++b;
    }
    while (e > b && Is_Space(*(e - 1))) {
        --e;
    }
}

/// Westwood 的 atoi 语义：跳过前导空白，吃可选正负号，然后吃到第一个非数字。
inline int Atoi(const char* b, const char* e) {
    Trim(b, e);
    bool neg = false;
    if (b < e && (*b == '+' || *b == '-')) {
        neg = (*b == '-');
        ++b;
    }
    long v = 0;
    while (b < e && *b >= '0' && *b <= '9') {
        v = v * 10 + (*b - '0');
        if (v > 0x7FFFFFFFL) {
            v = 0x7FFFFFFFL;   // 饱和，别溢出
        }
        ++b;
    }
    return static_cast<int>(neg ? -v : v);
}

/// 切逗号列表。返回 [b,e) 内第 n 段（已 trim）；n 越界返回空。
bool Nth_Field(const char* b, const char* e, int n, const char*& fb, const char*& fe) {
    int cur = 0;
    const char* seg = b;
    for (const char* p = b; p <= e; ++p) {
        if (p == e || *p == ',') {
            if (cur == n) {
                fb = seg;
                fe = p;
                Trim(fb, fe);
                return true;
            }
            ++cur;
            seg = p + 1;
        }
    }
    return false;
}

}  // namespace

void IniFile::Clear() {
    sections_.clear();
    section_index_.clear();
    malformed_ = 0;
}

bool IniFile::Load(const uint8_t* data, size_t size) {
    Clear();
    if (data == nullptr || size == 0) {
        return false;
    }
    const char* p = reinterpret_cast<const char*>(data);
    const char* end = p + size;

    IniSection* cur = nullptr;
    // 先粗估一下段数/条目数，省掉 vector 的多次扩容（rulesmd 有 1478 段、29k 条目）。
    sections_.reserve(2048);

    while (p < end) {
        const char* line_b = p;
        const char* line_e = p;
        while (line_e < end && *line_e != '\n') {
            ++line_e;
        }
        p = (line_e < end) ? line_e + 1 : end;

        const char* b = line_b;
        const char* e = line_e;
        Trim(b, e);
        if (b >= e) {
            continue;                     // 空行
        }
        if (*b == ';') {
            continue;                     // 注释
        }
        if (b + 1 < e && b[0] == '/' && b[1] == '/') {
            continue;                     // // 注释
        }

        if (*b == '[') {
            // 段头：取到第一个 ']'。实测 [GAFWLL];temp wall for yuri[YAWALL]
            // 这种写法，段名就是第一个 ] 之前的内容。
            const char* close = b + 1;
            while (close < e && *close != ']') {
                ++close;
            }
            const char* nb = b + 1;
            const char* ne = close;       // 没有 ] 就一直取到行尾
            Trim(nb, ne);
            const std::string name(nb, ne);
            if (name.empty()) {
                ++malformed_;
                continue;
            }
            const std::string lower = To_Lower(nb, ne);
            auto it = section_index_.find(lower);
            if (it != section_index_.end()) {
                // 段名重复是真的：artmd 有 [PARABOMB] 两处同名，
                // rulesmd 有 [VIRUS]/[Virus] 仅大小写不同。
                // 大小写不敏感地合并到同一段，后续条目追加进去。
                cur = &sections_[static_cast<size_t>(it->second)];
                continue;
            }
            section_index_.emplace(lower, static_cast<int>(sections_.size()));
            sections_.push_back(IniSection{});
            cur = &sections_.back();
            cur->name = name;
            continue;
        }

        const char* eq = b;
        while (eq < e && *eq != '=') {
            ++eq;
        }
        if (eq >= e) {
            // 实测原始文件自带这种畸形行：[Animations] 里的 `842-GAWETH_ED`
            // 少了等号。跳过并计数，不报错，也不把它当成 value 续行。
            ++malformed_;
            continue;
        }

        const char* kb = b;
        const char* ke = eq;
        Trim(kb, ke);
        if (kb >= ke) {
            ++malformed_;
            continue;
        }

        const char* vb = eq + 1;
        const char* ve = e;
        // 值截断到第一个 ';'
        for (const char* s = vb; s < ve; ++s) {
            if (*s == ';') {
                ve = s;
                break;
            }
        }
        Trim(vb, ve);

        if (cur == nullptr) {
            // 段外的键值行。原引擎会丢掉；这里同样丢掉但计数。
            ++malformed_;
            continue;
        }

        const std::string lower_key = To_Lower(kb, ke);
        // 段内重复键保留全部顺序，索引只记第一次出现（按名取值返回第一个）。
        if (cur->index.find(lower_key) == cur->index.end()) {
            cur->index.emplace(lower_key, static_cast<int>(cur->entries.size()));
        }
        cur->entries.push_back(IniEntry{std::string(kb, ke), std::string(vb, ve)});
    }
    return !sections_.empty();
}

bool IniFile::Load_File(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return false;
    }
    f.seekg(0, std::ios::end);
    const std::streamoff n = f.tellg();
    f.seekg(0, std::ios::beg);
    if (n <= 0) {
        return false;
    }
    std::vector<uint8_t> buf(static_cast<size_t>(n));
    f.read(reinterpret_cast<char*>(buf.data()), n);
    return Load(buf.data(), buf.size());
}

const IniSection* IniFile::Section(int i) const {
    if (i < 0 || i >= static_cast<int>(sections_.size())) {
        return nullptr;
    }
    return &sections_[static_cast<size_t>(i)];
}

const IniSection* IniFile::Find_Section(const char* name) const {
    if (name == nullptr) {
        return nullptr;
    }
    const std::string lower = To_Lower(name, name + std::char_traits<char>::length(name));
    auto it = section_index_.find(lower);
    if (it == section_index_.end()) {
        return nullptr;
    }
    return &sections_[static_cast<size_t>(it->second)];
}

namespace {

const IniEntry* Find_Entry(const IniFile& ini, const char* section, const char* key) {
    const IniSection* s = ini.Find_Section(section);
    if (s == nullptr || key == nullptr) {
        return nullptr;
    }
    const std::string lower = To_Lower(key, key + std::char_traits<char>::length(key));
    auto it = s->index.find(lower);
    if (it == s->index.end()) {
        return nullptr;
    }
    return &s->entries[static_cast<size_t>(it->second)];
}

}  // namespace

int IniFile::Entry_Count(const char* section) const {
    const IniSection* s = Find_Section(section);
    return s ? static_cast<int>(s->entries.size()) : 0;
}

const char* IniFile::Entry_Key(const char* section, int i) const {
    const IniSection* s = Find_Section(section);
    if (s == nullptr || i < 0 || i >= static_cast<int>(s->entries.size())) {
        return "";
    }
    return s->entries[static_cast<size_t>(i)].key.c_str();
}

const char* IniFile::Entry_Value(const char* section, int i) const {
    const IniSection* s = Find_Section(section);
    if (s == nullptr || i < 0 || i >= static_cast<int>(s->entries.size())) {
        return "";
    }
    return s->entries[static_cast<size_t>(i)].value.c_str();
}

std::string IniFile::Get_String(const char* section, const char* key,
                                const char* def) const {
    const IniEntry* e = Find_Entry(*this, section, key);
    return e ? e->value : std::string(def ? def : "");
}

int IniFile::Get_Int(const char* section, const char* key, int def) const {
    const IniEntry* e = Find_Entry(*this, section, key);
    if (e == nullptr) {
        return def;
    }
    const char* b = e->value.c_str();
    return Atoi(b, b + e->value.size());
}

double IniFile::Get_Double(const char* section, const char* key, double def) const {
    const IniEntry* e = Find_Entry(*this, section, key);
    if (e == nullptr) {
        return def;
    }
    const char* b = e->value.c_str();
    const char* p = b;
    const char* end = b + e->value.size();
    while (p < end && Is_Space(*p)) {
        ++p;
    }
    // 借 strtod：它同样"遇到非数字字符就停"，和 atoi 语义一致。
    // 但要在拷贝出来的、以确定长度界定的缓冲区上调用，避免读到别的键值。
    char tmp[64];
    const size_t n = std::min(sizeof(tmp) - 1, static_cast<size_t>(end - p));
    std::memcpy(tmp, p, n);
    tmp[n] = '\0';
    char* stop = nullptr;
    const double v = std::strtod(tmp, &stop);
    return (stop == tmp) ? def : v;
}

bool IniFile::Get_Bool(const char* section, const char* key, bool def) const {
    const IniEntry* e = Find_Entry(*this, section, key);
    if (e == nullptr) {
        return def;
    }
    const std::string s = To_Lower(e->value.c_str(), e->value.c_str() + e->value.size());
    if (s.empty()) {
        return def;
    }
    // 实测词表：yes 6099 / no 4924 / true 1436 / false 820。
    if (s == "yes" || s == "true" || s == "1") {
        return true;
    }
    if (s == "no" || s == "false" || s == "0") {
        return false;
    }
    return Atoi(e->value.c_str(), e->value.c_str() + e->value.size()) != 0;
}

int IniFile::Get_Int_List(const char* section, const char* key,
                          std::vector<int>* out) const {
    if (out == nullptr) {
        return 0;
    }
    out->clear();
    const IniEntry* e = Find_Entry(*this, section, key);
    if (e == nullptr) {
        return 0;
    }
    const char* b = e->value.c_str();
    const char* end = b + e->value.size();
    for (int n = 0;; ++n) {
        const char* fb = nullptr;
        const char* fe = nullptr;
        if (!Nth_Field(b, end, n, fb, fe)) {
            break;
        }
        if (fb >= fe) {
            continue;   // 空元素跳过（"a,,b" 这种）
        }
        out->push_back(Atoi(fb, fe));
    }
    return static_cast<int>(out->size());
}

int IniFile::Get_Percent_List(const char* section, const char* key,
                              std::vector<int>* out) const {
    // 百分比就是整数 + 可能的 '%'；Atoi 遇到 '%' 会停，所以直接复用。
    // 实测有 200%、400% 这类超 100 的值，不钳制。
    return Get_Int_List(section, key, out);
}

int IniFile::Get_String_List(const char* section, const char* key,
                             std::vector<std::string>* out) const {
    if (out == nullptr) {
        return 0;
    }
    out->clear();
    const IniEntry* e = Find_Entry(*this, section, key);
    if (e == nullptr) {
        return 0;
    }
    const char* b = e->value.c_str();
    const char* end = b + e->value.size();
    for (int n = 0;; ++n) {
        const char* fb = nullptr;
        const char* fe = nullptr;
        if (!Nth_Field(b, end, n, fb, fe)) {
            break;
        }
        if (fb >= fe) {
            continue;
        }
        out->emplace_back(fb, fe);
    }
    return static_cast<int>(out->size());
}

uint32_t IniFile::Get_Color(const char* section, const char* key, uint32_t def) const {
    const IniEntry* e = Find_Entry(*this, section, key);
    if (e == nullptr) {
        return def;
    }
    const char* b = e->value.c_str();
    const char* end = b + e->value.size();
    int c[3] = {0, 0, 0};
    for (int n = 0; n < 3; ++n) {
        const char* fb = nullptr;
        const char* fe = nullptr;
        if (!Nth_Field(b, end, n, fb, fe) || fb >= fe) {
            return def;
        }
        const int v = Atoi(fb, fe);
        c[n] = v < 0 ? 0 : (v > 255 ? 255 : v);
    }
    // Westwood 的颜色打包是 0x00BBGGRR（最低字节是红）。
    return (static_cast<uint32_t>(c[2]) << 16) | (static_cast<uint32_t>(c[1]) << 8) |
           static_cast<uint32_t>(c[0]);
}

int IniFile::Read_Numbered_List(const char* section, std::vector<std::string>* out) const {
    if (out == nullptr) {
        return 0;
    }
    out->clear();
    const IniSection* s = Find_Section(section);
    if (s == nullptr) {
        return 0;
    }
    // 收集 (编号, 条目下标)，按编号升序。
    std::vector<std::pair<int, int>> num;
    for (size_t i = 0; i < s->entries.size(); ++i) {
        const std::string& k = s->entries[i].key;
        bool all_digit = !k.empty();
        for (char c : k) {
            if (c < '0' || c > '9') {
                all_digit = false;
                break;
            }
        }
        if (all_digit) {
            num.emplace_back(std::atoi(k.c_str()), static_cast<int>(i));
        }
    }
    std::sort(num.begin(), num.end());
    for (const auto& p : num) {
        out->push_back(s->entries[static_cast<size_t>(p.second)].value);
    }
    return static_cast<int>(out->size());
}

}  // namespace ra2
