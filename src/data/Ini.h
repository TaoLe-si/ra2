// Ini.h -- RA2 / Westwood INI 解析（CCINIClass 的还原）
//
// 方言是实测出来的，不是照抄文档。样本：rulesmd.ini(742958B/31061 行)、
// artmd.ini(336535B/19605 行)、aimd.ini(138538B/7400 行)、
// soundmd.ini(99292B/5724 行) —— 四个真实文件全部来自 ra2md.mix 内的 LOCALMD.MIX。
//
// 【实测确定的行为】
//   * 纯 ASCII（4 个文件里非 ASCII 字节数都是 0），CRLF 行尾，最长行 303 字符，
//     没有反斜杠续行。
//   * 注释是 `;`，可以出现在任何位置：
//       - 整行            `; rifle soldier weapons (multiple shots)`
//       - 段头后面         `[JumpjetControls] ;gs These are now merely defaults`
//       - 值后面           `RefundPercent=50%       ; percent of original cost`
//     另外整行以 `//` 开头的也算注释（`// PCG; Provides knobs to tweak...`）。
//   * 段头一律取到**第一个 `]`** 为止，后面的内容忽略。这条很重要：
//     `[GAFWLL];temp wall for yuri[YAWALL]` 的段名是 GAFWLL，不是 GAFWLL]..YAWALL。
//   * 键值行 = 第一个 `=` 左边当键、右边当值；`=` 两侧的空白和 TAB 都要吃掉
//     （实测大量 `Burst = 2`、`LetsDoTheTimeWarpOutAgain = ChronoScreenSound`）。
//     值取到第一个 `;` 为止。
//   * 值可以为空（实测 214 处，例如 `Report=`）。
//   * 段名、键名一律**大小写不敏感**（art.ini 里大小写混着写）。
//   * 段名会重复，且必须按大小写不敏感合并到同一段：
//       rulesmd.ini  [VIRUS](5154 行) 与 [Virus](27076 行) —— 仅大小写不同
//       artmd.ini    [PARABOMB](14745 行) 与 [PARABOMB](15651 行) —— 完全同名
//     所以 sections_ 里同名只留一个，后续同名的条目**追加**进去。
//   * 段内允许重复键（实测 1 处：[CRNeutronRifle] 的 Report 出现两次），
//     保留全部出现顺序，按名取值返回**第一个**。
//   * 编号型键（纯数字）大量存在且**不从 0 开始、允许跳号**：
//       [InfantryTypes] 1..65（没有 0）、[TerrainTypes] 从 2 开始、
//       [Animations] 1,3,... 而且文件里明确写着 "The following can occur in any order."
//     所以编号列表要按**数字升序**读，而不是"0,1,2 一直读到缺号"。
//     （实测所有编号段在文件里本来就是升序的，两种读法在原始数据上等价；
//      但没有 0 这一点决定了"从 0 数到断"必然是错的。）
//   * 原始文件自带一行畸形（[Animations] 里的 `842-GAWETH_ED`，少了等号），
//     解析器必须跳过，不能报错 —— 官方数据都不干净。
//
// 【不做的】
//   * `[$Section]` 继承语法：网上资料说 Westwood INI 支持，但实测这四个文件里
//     一个 `$` 段都没有，所以不实现。真遇上再加。
//   * `#include`：同上，没有。

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ra2 {

/// 一段里的一条键值。
struct IniEntry {
    std::string key;
    std::string value;
};

/// 一个 `[Section]`。
struct IniSection {
    std::string name;
    std::vector<IniEntry> entries;
    /// 小写键名 -> entries 下标。**只记第一次出现**，与按名取值"返回第一个"一致。
    std::unordered_map<std::string, int> index;
};

/// INI 文档。
class IniFile {
public:
    /// 从内存解析。输入含 '\0' 也没关系（按长度走）。会先清空已有内容。
    bool Load(const uint8_t* data, size_t size);
    bool Load(const char* text) { return Load(reinterpret_cast<const uint8_t*>(text),
                                               text ? std::char_traits<char>::length(text) : 0); }
    bool Load_File(const char* path);

    /// 不清空，把另一份 INI 合并进来。同名段合并（大小写不敏感）。
    ///
    /// 为什么需要：RA2 的 rules.ini / rulesmd.ini 是"打底 + 覆盖"的关系，
    /// 光靠 Load 会丢掉其中一份。
    ///
    /// overwrite=false：条目**追加**到段尾，按名取值仍返回第一次出现的。
    /// overwrite=true ：同名键**在原来的位置覆盖值**（键名保持第一次的形态）。
    ///
    /// 【为什么要有 overwrite 这种模式】Westwood 的 INIClass::Load 就是覆盖在位的。
    /// 反例很实在：[BuildingTypes] 是个编号列表，rules.ini 有 301 项、
    /// rulesmd.ini 有 403 项。用"追加 + 取第一个"的写法，两份的 1..301 会**并存**，
    /// 于是单位总数变成 565 —— 比真实的 559 多出 6 个（CAARMR / NAHPAD 之类，
    /// 只在 rules.ini 的老列表里）。覆盖在位才会得到 403 项这一份正确的并集。
    bool Merge(const uint8_t* data, size_t size, bool overwrite = false);
    bool Merge(const char* text, bool overwrite = false) {
        return Merge(reinterpret_cast<const uint8_t*>(text),
                     text ? std::char_traits<char>::length(text) : 0, overwrite);
    }

    void Clear();

    int Section_Count() const noexcept { return static_cast<int>(sections_.size()); }
    /// 按文件顺序取段；越界返回空。
    const IniSection* Section(int i) const;
    /// 按名取段（大小写不敏感）；找不到返回 nullptr。
    const IniSection* Find_Section(const char* name) const;
    bool Has_Section(const char* name) const { return Find_Section(name) != nullptr; }

    /// 段内条目数；段不存在返回 0。
    int Entry_Count(const char* section) const;
    /// 段内第 i 条（保持文件顺序）；越界返回空串。
    const char* Entry_Key(const char* section, int i) const;
    const char* Entry_Value(const char* section, int i) const;

    // ---- 取值。找不到就返回 def（Westwood 的 Get_XXX 语义）----

    std::string Get_String(const char* section, const char* key,
                           const char* def = "") const;

    /// 整数。**用 atoi 语义**：遇到不是数字的字符就停，
    /// 所以 "100%" -> 100、"15 ; x" 早就被截断成 "15"、"abc" -> 0。
    int Get_Int(const char* section, const char* key, int def = 0) const;

    /// 浮点。同样遇到非数字字符停。
    double Get_Double(const char* section, const char* key, double def = 0.0) const;

    /// 布尔。实测词表：yes/no（最多）、true/false。
    /// 另外接受 1/0；其余按 def 处理。
    bool Get_Bool(const char* section, const char* key, bool def = false) const;

    /// 逗号分隔的整数列表（RA2 里满地都是：Verses、Damage、坐标…）。
    /// 空白会被跳过，空元素跳过。
    int Get_Int_List(const char* section, const char* key, std::vector<int>* out) const;

    /// 逗号分隔的百分比列表。"100%,80%" -> {100,80}。实测有 200%、400% 这种
    /// 超过 100 的值，所以不做上限钳制。
    int Get_Percent_List(const char* section, const char* key, std::vector<int>* out) const;

    /// 逗号分隔的字符串列表（自动 trim 两端空白）。
    int Get_String_List(const char* section, const char* key,
                        std::vector<std::string>* out) const;

    /// "R,G,B" -> 0x00BBGGRR（Westwood 是 BGR 打包）。
    /// 分量允许 0..255；不足三段的返回 def。
    uint32_t Get_Color(const char* section, const char* key, uint32_t def = 0) const;

    /// 编号列表：把段内**所有纯数字键**按数字升序取回值。
    /// 这条是实测逼出来的 —— [InfantryTypes] 从 1 开始、[Animations] 还跳号，
    /// 按"0,1,2 直到缺号"读会一个都读不到。返回读到的个数。
    int Read_Numbered_List(const char* section, std::vector<std::string>* out) const;

    /// 解析过程中的畸形行统计，用来证明"解析器确实跳过了它们"而不是碰巧没遇到。
    int Malformed_Lines() const noexcept { return malformed_; }

private:
    /// Load / Merge 共用的解析主体。Load = Clear + Parse。
    bool Parse(const uint8_t* data, size_t size);

    /// Merge(overwrite=true) 期间生效：同名键覆盖在位而不追加。
    bool merge_overwrite_ = false;

    std::vector<IniSection> sections_;
    std::unordered_map<std::string, int> section_index_;   ///< 小写段名 -> 下标
    int malformed_ = 0;
};

}  // namespace ra2
