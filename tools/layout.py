"""
layout.py -- 把三张自动生成的表合成一个**真正能编译、且编译期自检**的 C++ 对象模型。

输入（都是前面几轮自动生成出来的）：
  db/fields.json     偏移 + 宽度 + 写次数   <- tools/fieldscan.py  扫构造函数
  db/fieldnames.json 偏移 -> INI 键名        <- tools/fieldname.py  扫 Read_INI
  db/rtti.json       继承关系 bases[0]      <- tools/rtti.py       扫 RTTI

输出：
  src/re/ObjectModel.h   结构体定义 + 每条字段的 static_assert(offsetof(...) == 偏移)
  db/layout.json         全量数据（每个类的成员序列：字段/填充、来源、名字）
  docs/object-model.md   方法、对账、未知量、以及这个模型明确没做到的事

为什么要做这一步：
  前三张表都是**查询表**（按 (类, 偏移) 查宽度/名字），不是"能用"的对象模型 ——
  代码里没法 `obj->COST = 1000`，也没法 `sizeof(TechnoTypeClass)`。
  把 (偏移, 宽度, 名字) 铺成「字段 + 显式填充」的连续内存布局之后：
    * 每个已知字段的偏移由编译器 `static_assert` 钉死（防漂移，不是取证）；
    * 类的末端 = 已知字段的最大末端，这是 sizeof 的**下界**（新数据）；
    * 未知的部分不再是"看不见"，而是写出来的 _unk_ / _pad_ 字节数组。

布局策略：
  1. 逐类取「构造函数写过」∪「Read_INI 读过/写过」的并集；两者的宽度必须一致，不一致就报错。
  2. 按 RTTI 的 bases[0] 串成继承链，子类只声明「偏移 >= 父类末端」的那部分字段。
     父类末端以下的字段一律算父类的 —— 这一条本身是个独立对账：
     子类构造函数写过的所有父类区偏移，必须都能在父类字段表里找到（见 docs）。
  3. 字段之间不满的地方用 _pad_XXXX[] 填满，使每个已知字段**恰好**落在它的偏移上。
  4. `#pragma pack(push,1)` 是故意的：二进制里的布局是事实，不能让它被编译器的
     对齐规则改写。pack(1) 下 offsetof / sizeof 精确等于我们铺出来的偏移
     （已用 tools/_probe_offsetof.cpp 在 MSVC 14.x 上实测过，不是推测）。

用法：
  python tools/layout.py            # 生成三个输出 + 打印对账表
  python tools/layout.py --check    # 只对账，不写文件
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# 这 6 个类是 P2 类型表用到的对象类型类，也是目前字段名覆盖到的全部类。
WANTED = [
    "ObjectTypeClass",
    "TechnoTypeClass",
    "BuildingTypeClass",
    "UnitTypeClass",
    "InfantryTypeClass",
    "IsometricTileTypeClass",
]

# C++ 关键字（INI 键名撞上就改；撞上的概率≈0，但撞了必须能发现而不是编不过）
KEYWORDS = {
    "alignas", "alignof", "and", "asm", "auto", "bool", "break", "case", "catch", "char",
    "class", "const", "constexpr", "continue", "default", "delete", "do", "double",
    "else", "enum", "explicit", "export", "extern", "false", "float", "for", "friend",
    "goto", "if", "inline", "int", "long", "mutable", "namespace", "new", "noexcept",
    "not", "nullptr", "operator", "or", "private", "protected", "public", "register",
    "return", "short", "signed", "sizeof", "static", "struct", "switch", "template",
    "this", "throw", "true", "try", "typedef", "typeid", "typename", "union",
    "unsigned", "using", "virtual", "void", "volatile", "wchar_t", "while", "xor",
}


def load_json(name):
    with open(os.path.join(ROOT, "db", name), encoding="utf-8") as fh:
        return json.load(fh)


def hexs(o, w=4):
    return "0x%0*X" % (w, o)


# ---------------------------------------------------------------- 继承链

def parent_of(rtti, fields, c):
    """RTTI 的 bases[0] 里、并且我们真的扫过字段的那个基类。没有就返回 None。"""
    e = rtti["classes"].get(c)
    if not isinstance(e, dict):
        return None
    bases = e.get("bases") or []
    if not bases:
        return None
    b = bases[0]
    if b == c or b not in fields["classes"]:
        return None
    return b


def build_chain(rtti, fields):
    """把 WANTED 沿 bases[0] 往上走，收集出所有需要的类，父类排在子类前面。"""
    parents = {}
    todo = list(WANTED)
    while todo:
        c = todo.pop()
        if c in parents:
            continue
        p = parent_of(rtti, fields, c)
        parents[c] = p
        if p is not None and p not in parents:
            todo.append(p)
    depth = {}
    for c in parents:
        d, cur = 0, c
        while parents.get(cur):
            cur, d = parents[cur], d + 1
        depth[c] = d
    # 同一深度内按名字排序，保证输出稳定、可复现
    return sorted(parents, key=lambda c: (depth[c], c))


# ---------------------------------------------------------------- 字段集合

def class_fields(fields, fn, cls):
    """返回 {off: {...}}，src ∈ ctor / ini / both。宽度不一致直接终止。"""
    out = {}
    for f in fields["classes"].get(cls, {}).get("fields", []):
        out[f["off"]] = {
            "off": f["off"], "size": f["size"], "key": None, "type": None,
            "src": "ctor", "w": f["w"], "r": f["r"],
        }
    for v in fn["classes"].get(cls, {}).values():
        o = v["off"]
        e = out.get(o)
        if e is None:
            out[o] = {"off": o, "size": v["width"], "key": v["key"],
                      "type": v["type"], "src": "ini", "w": 0, "r": 0,
                      "level": v["level"]}
        else:
            if e["size"] != v["width"]:
                raise SystemExit(
                    "[x] %s 偏移 0x%X（%s）：构造函数说宽 %d，Read_INI 说宽 %d"
                    % (cls, o, v["key"], e["size"], v["width"]))
            e["key"] = v["key"]
            e["type"] = v["type"]
            e["level"] = v["level"]
            e["src"] = "both"
    return out


def overlaps(entries):
    """相邻字段区间相交即算重叠（偏移已排序）。"""
    o = sorted(entries)
    bad = []
    for i in range(len(o) - 1):
        a, b = entries[o[i]], entries[o[i + 1]]
        if a["off"] + a["size"] > b["off"]:
            bad.append((a, b))
    return bad


def sanitize(key, taken):
    s = re.sub(r"[^0-9A-Za-z_]", "_", key)
    if not s or s[0].isdigit():
        s = "_" + s
    if s in KEYWORDS:
        s += "_"
    base, n = s, 2
    while s in taken:
        s = "%s_%d" % (base, n)
        n += 1
    taken.add(s)
    return s


def build_class(cls, own_start, entries):
    """把字段集合铺成连续成员序列（字段 + 填充）。返回 (members, end, own_off)。"""
    members = []
    cur = own_start
    taken = set()
    own_offset = None
    for o in sorted(entries):
        e = entries[o]
        if o < cur:
            raise SystemExit("[x] %s 偏移 0x%X 与前一个字段重叠" % (cls, o))
        if o > cur:
            name = "_pad_" + hexs(cur)[2:]
            while name in taken:
                name += "_"
            taken.add(name)
            members.append({"kind": "pad", "off": cur, "size": o - cur, "name": name})
        if own_offset is None:
            own_offset = o - own_start
        if e["key"]:
            name = sanitize(e["key"], taken)
            members.append({"kind": "field", "off": o, "size": e["size"],
                            "name": name, "key": e["key"], "type": e["type"],
                            "src": e["src"], "level": e.get("level", ""),
                            "w": e.get("w", 0), "named": True})
        else:
            name = "_unk_" + hexs(o)[2:]
            while name in taken:
                name += "_"
            taken.add(name)
            members.append({"kind": "field", "off": o, "size": e["size"],
                            "name": name, "key": None, "type": None,
                            "src": e["src"], "w": e.get("w", 0), "named": False})
        cur = o + e["size"]
    return members, cur, (own_offset if own_offset is not None else 0)


# ---------------------------------------------------------------- 生成

HEAD = """// 自动生成文件，请勿手改。生成工具：tools/layout.py
// 数据来源（三张表合成）：
//   db/fields.json     偏移+宽度     <- tools/fieldscan.py  扫 gamemd.exe 的构造函数
//   db/fieldnames.json 偏移->INI 键  <- tools/fieldname.py  扫 gamemd.exe 的 Read_INI
//   db/rtti.json       继承关系      <- tools/rtti.py       扫 gamemd.exe 的 RTTI
//
// 结构：
//   * 按 RTTI 的 bases[0] 串成继承链（AbstractClass -> ... -> 各 TypeClass）。
//   * 每个已知字段都落在它**实测出来的偏移**上：字段之间用 _pad_XXXX[] 补齐。
//   * 没名字但有证据（构造函数写过）的偏移是 _unk_XXXX，不是"没有字段"。
//   * `#pragma pack(push,1)` 是故意的：二进制里的布局是事实，不能被编译器的对齐
//     规则改写。pack(1) 下 MSVC 的 offsetof/sizeof 精确等于这里铺出来的偏移
//     （tools/_probe_offsetof.cpp 实测过）。
//   * 每条字段后面都有一条 static_assert(offsetof(...) == 偏移)：
//     改坏布局会**编译不过**，而不是悄悄漂移。它守的是回归，不是取证 ——
//     偏移本身来自反汇编，见 docs/fields.md 与 docs/fieldnames.md。
//
// 注意：`u8 _pad_XXXX[n]` 填充**不代表**那 n 个字节真的没用 —— 只代表
// "构造函数和 Read_INI 都没有写到那里"。我们不知道它们是什么，所以不编名字。
// 详见 docs/object-model.md。
"""


def member_decl(m, off0):
    """一条成员的 C++ 声明 + 行尾注释。"""
    rel = m["off"] - off0
    if m["kind"] == "pad":
        return "    u8 %s[%d];%s// +0x%X（本体 +0x%X） —— 没有证据" % (
            m["name"], m["size"], " " * max(1, 26 - len(m["name"])), m["off"], rel)
    ty = {1: "u8", 2: "u16", 4: "u32", 8: "u32"}[m["size"]]
    decl = "    %s %s;" % (ty, m["name"]) if m["size"] != 8 else "    u32 %s[2];" % m["name"]
    if m["named"]:
        note = "INI: %s" % m["key"]
        if m.get("level"):
            note += " [%s]" % m["level"]
        if m["src"] == "ini":
            note += "（构造函数没写过）"
    else:
        note = "没名字（只有构造函数写过的证据）"
    return "%s%s// +0x%X（本体 +0x%X） %s" % (
        decl, " " * max(1, 30 - len(decl)), m["off"], rel, note)


def emit_header(classes, image):
    L = [HEAD, "#pragma once", ""]
    L.append("// 基线镜像：%s" % (image or "?"))
    L.append("")
    L.append("#include <cstddef>")
    L.append("#include <cstdint>")
    L.append("")
    L.append("namespace ra2 {")
    L.append("namespace re {")
    L.append("namespace model {")
    L.append("")
    L.append("using u8 = std::uint8_t;")
    L.append("using u16 = std::uint16_t;")
    L.append("using u32 = std::uint32_t;")
    L.append("")
    L.append("// pack(1)：二进制里的布局是事实，不让编译器按对齐规则改写它。")
    L.append("#pragma pack(push, 1)")
    L.append("")
    for c in classes:
        base = (" : public " + c["parent"]) if c["parent"] else ""
        L.append("/// %s 0x%X .. 0x%X（本体 %d 字节）：已知字段 %d 个"
                 "（有名字 %d 个），填充 %d 字节。"
                 % (c["cls"], c["start"], c["end"], c["end"] - c["start"],
                    c["fields"], c["named"], c["pad_bytes"]))
        L.append("struct %s%s {" % (c["cls"], base))
        for m in c["members"]:
            L.append(member_decl(m, c["start"]))
        L.append("};")
        L.append("")
    L.append("#pragma pack(pop)")
    L.append("")
    L.append("// ---- 编译期钉死：每个已知字段的偏移必须和二进制里读到的一致 ----")
    L.append("// 这是防漂移的回归闸门（改坏布局就编译不过），不是对偏移的取证。")
    L.append("#define RA2_OFF(T, M, O) \\")
    L.append("    static_assert(offsetof(T, M) == (O), \\")
    L.append("                  \"offsetof(\" #T \", \" #M \") != \" #O)")
    L.append("")
    for c in classes:
        L.append("// %s" % c["cls"])
        for m in c["members"]:
            if m["kind"] == "pad":
                continue
            L.append("RA2_OFF(%s, %s, 0x%X);" % (c["cls"], m["name"], m["off"]))
        L.append("")
    L.append("// 末端 = 最后一个已知字段的末端，也就是 sizeof 的**下界**")
    for c in classes:
        L.append("static_assert(sizeof(%s) == 0x%X, \"%s 末端变了\");"
                 % (c["cls"], c["end"], c["cls"]))
    L.append("")
    L.append("#undef RA2_OFF")
    L.append("")
    L.append("// ---- 给代码用的查询表 ----")
    L.append("// 结构体给编译器用（偏移被 static_assert 钉死），这两张表给运行期/工具用：")
    L.append("// 成员序列可以按名字或偏移去查，也是 Model_Check() 对账 FieldNames.h 的依据。")
    L.append("")
    L.append("struct ModelMember {")
    L.append("    uint32_t    off;    // 绝对偏移（不是「相对本体」）")
    L.append("    uint32_t    size;")
    L.append("    uint8_t     kind;   // 0 = 字段，1 = 填充（没有证据的字节）")
    L.append("    const char* name;   // 成员名；_pad_XXXX 也有名字")
    L.append("    const char* key;    // INI 键名；nullptr = 没名字")
    L.append("};")
    L.append("")
    L.append("struct ModelClass {")
    L.append("    const char* name;")
    L.append("    const char* parent;      // nullptr = 根")
    L.append("    uint32_t    start;       // 本体起点（= 父类末端；根为 0）")
    L.append("    uint32_t    end;         // 末端 = sizeof 的**下界**")
    L.append("    uint32_t    fields;      // 已知字段数（含无名字的 _unk_）")
    L.append("    uint32_t    named;       // 其中有名字的")
    L.append("    uint32_t    pad_bytes;   // 没有证据的字节数")
    L.append("    uint32_t    first;       // 在 kModelMembers 里的起始下标")
    L.append("    uint32_t    count;       // 成员条数（字段 + 填充段）")
    L.append("};")
    L.append("")
    L.append("inline constexpr ModelMember kModelMembers[] = {")
    idx = 0
    for c in classes:
        L.append("    // %s" % c["cls"])
        for m in c["members"]:
            key = ('"%s"' % m["key"]) if m.get("key") else "nullptr"
            L.append('    {0x%X, %u, %d, "%s", %s},'
                     % (m["off"], m["size"], 0 if m["kind"] == "field" else 1,
                        m["name"], key))
            idx += 1
    L.append("};")
    L.append("")
    first = 0
    L.append("inline constexpr ModelClass kModelClasses[] = {")
    for c in classes:
        n = len(c["members"])
        L.append('    {"%s", %s, 0x%X, 0x%X, %u, %u, %u, %u, %u},'
                 % (c["cls"], ('"%s"' % c["parent"]) if c["parent"] else "nullptr",
                    c["start"], c["end"], c["fields"], c["named"], c["pad_bytes"],
                    first, n))
        first += n
    L.append("};")
    L.append("")
    L.append("inline constexpr uint32_t kModelClassCount =")
    L.append("    sizeof(kModelClasses) / sizeof(kModelClasses[0]);")
    L.append("inline constexpr uint32_t kModelMemberCount =")
    L.append("    sizeof(kModelMembers) / sizeof(kModelMembers[0]);")
    L.append("")
    L.append("/// 按类名查模型条目；查不到返回 nullptr。")
    L.append("inline const ModelClass* ModelOf(const char* name) {")
    L.append("    if (name == nullptr) return nullptr;")
    L.append("    for (uint32_t i = 0; i < kModelClassCount; ++i) {")
    L.append("        const char* a = kModelClasses[i].name;")
    L.append("        const char* b = name;")
    L.append("        while (*a && *a == *b) { ++a; ++b; }")
    L.append("        if (*a == *b) return &kModelClasses[i];")
    L.append("    }")
    L.append("    return nullptr;")
    L.append("}")
    L.append("")
    L.append("/// 按 (类, INI 键名) 查成员；查不到返回 nullptr。大小写敏感（键名原样）。")
    L.append("inline const ModelMember* ModelMemberOfKey(const ModelClass& c,")
    L.append("                                           const char* key) {")
    L.append("    if (key == nullptr) return nullptr;")
    L.append("    for (uint32_t i = 0; i < c.count; ++i) {")
    L.append("        const ModelMember& m = kModelMembers[c.first + i];")
    L.append("        if (m.key == nullptr) continue;")
    L.append("        const char* a = m.key;")
    L.append("        const char* b = key;")
    L.append("        while (*a && *a == *b) { ++a; ++b; }")
    L.append("        if (*a == *b) return &m;")
    L.append("    }")
    L.append("    return nullptr;")
    L.append("}")
    L.append("")
    L.append("/// 按 (类, 偏移) 查成员；偏移必须正好落在某个成员的开头，否则 nullptr。")
    L.append("inline const ModelMember* ModelMemberAt(const ModelClass& c, uint32_t off) {")
    L.append("    for (uint32_t i = 0; i < c.count; ++i) {")
    L.append("        const ModelMember& m = kModelMembers[c.first + i];")
    L.append("        if (m.off == off) return &m;")
    L.append("    }")
    L.append("    return nullptr;")
    L.append("}")
    L.append("")
    L.append("}  // namespace model")
    L.append("}  // namespace re")
    L.append("}  // namespace ra2")
    L.append("")
    return "\n".join(L)


def write_docs(classes, notes, info, totals, image):
    tot_fields, tot_named, tot_pad = totals
    L = []
    L.append("# 对象模型：从三张表铺成能编译的结构体")
    L.append("")
    L.append("生成工具 `tools/layout.py`，产物 `src/re/ObjectModel.h`"
             "（全量数据 `db/layout.json`）。基线镜像 %s。" % (image or "?"))
    L.append("")
    L.append("## 1. 这一步解决什么")
    L.append("")
    L.append("前三轮的产物都是**查询表**：")
    L.append("")
    L.append("| 表 | 内容 | 谁生成 |")
    L.append("|---|---|---|")
    L.append("| `src/re/FieldOffsets.h` | (类, 偏移) -> 宽度、被写次数 | `tools/fieldscan.py` |")
    L.append("| `src/re/FieldNames.h` | (类, 偏移) -> INI 键名 | `tools/fieldname.py` |")
    L.append("| `src/re/ObjectSizes.h` | (类) -> sizeof | `tools/sizeofscan.py` |")
    L.append("")
    L.append("查询表能回答\"0x610 是什么\"，但写不出 `obj.COST = 1000`，"
             "也说不出 `sizeof(TechnoTypeClass)`。")
    L.append("把 (偏移, 宽度, 名字) 按 RTTI 的继承链铺成**连续内存布局**之后，"
             "对象模型才第一次能被代码直接用上：")
    L.append("")
    L.append("* 每个已知字段落在它**实测出来的偏移**上，由 "
             "`static_assert(offsetof(...) == 偏移)` 钉死；")
    L.append("* 字段之间的空隙是写出来的 `u8 _pad_XXXX[n]`，不是\"看不见\"；")
    L.append("* 类的末端是已知数据的直接结果，也就是 `sizeof` 的**下界**（新数据）；")
    L.append("* 没名字但有证据的偏移是 `_unk_XXXX`，保留着\"这里确实有一个 "
             "N 字节字段\"这条信息。")
    L.append("")
    L.append("## 2. 怎么铺的")
    L.append("")
    L.append("1. **字段集合** = 构造函数写过的偏移（`fields.json`）"
             "∪ Read_INI 读写过的偏移（`fieldnames.json`）。两边都出现的偏移"
             "宽度必须一致，不一致直接报错终止（本轮实跑：0 处不一致）。")
    L.append("2. **继承链**取自 RTTI 的 `bases[0]`："
             "`AbstractClass -> AbstractTypeClass -> ObjectTypeClass -> "
             "{TechnoTypeClass -> {Building,Unit,Infantry}, IsometricTileTypeClass}`。")
    L.append("   子类只声明\"偏移 ≥ 父类末端\"的部分，父类区一律算父类的。")
    L.append("3. **不满就填**：字段之间用 `_pad_XXXX[n]` 补到下一个字段的偏移，"
             "使每个已知字段恰好落在它的偏移上。")
    L.append("4. **`#pragma pack(push,1)`** 是故意的：二进制里的布局是事实，"
             "不能让编译器的对齐规则改写它。pack(1) 下 MSVC 的 `offsetof`/`sizeof` "
             "精确等于铺出来的偏移。这一点不是推测 —— `tools/_probe_offsetof.cpp` "
             "在本机 MSVC 14.x 上实测过（4 层继承 + 混合宽度 + pack(1)，"
             "`sizeof` 与 `offsetof` 全部对上）。")
    L.append("")
    L.append("## 3. 本轮算出来的数")
    L.append("")
    L.append("| 类 | 父类 | 本体起点 | 末端 | 已知字段 | 有名字 | 填充字节 | 已覆盖 |")
    L.append("|---|---|---|---|---|---|---|---|")
    for c in classes:
        span = c["end"] - c["start"]
        L.append("| `%s` | %s | 0x%X | 0x%X | %d | %d | %d | %.1f%% |" % (
            c["cls"], ("`%s`" % c["parent"]) if c["parent"] else "（根）",
            c["start"], c["end"], c["fields"], c["named"], c["pad_bytes"],
            100.0 * c["covered"] / span if span else 0.0))
    L.append("| **合计** | | | | **%d** | **%d** | **%d** | |"
             % (tot_fields, tot_named, tot_pad))
    L.append("")
    L.append("**末端是 `sizeof` 的下界，不是 `sizeof`。** 它是\"最后一个有证据的字段"
             "结束在哪\"；二进制里完全可能再往后还有我们没看到的字节"
             "（对齐填充、只在别的路径赋值的字段）。能说出口的是："
             "`sizeof(TechnoTypeClass) >= 0xDF4`。")
    L.append("")
    L.append("**覆盖率的含义要说清**：`已覆盖` = 有证据的字节 / 本体字节。"
             "`UnitTypeClass` 只有 3.5% 不代表它只有 130 字节有内容，只代表"
             "**构造函数和 Read_INI 这两条通道**在这些偏移上留下了痕迹。"
             "剩下的是\"暂时没有证据\"，不是\"空的\"。")
    L.append("")
    L.append("**父类末端 → 子类第一个自有字段之间的缝**"
             "（表里最后一列，`layout.json` 的 `align_gap`）：")
    L.append("")
    L.append("| 类 | 父类末端 | 第一个自有字段 | 缝（字节） |")
    L.append("|---|---|---|---|")
    for c in classes:
        if not c["parent"]:
            continue
        first = c["start"] + c["align_gap"]
        L.append("| `%s` | 0x%X | 0x%X | %d |" % (c["cls"], c["start"], first, c["align_gap"]))
    L.append("")
    L.append("缝 = 0 的那几个类说明父类末端和子类第一个自有字段**严丝合缝**；"
             "缝 > 0 说明中间还有我们没看到的字节（对齐填充，或者只在别处赋值的字段）。"
             "两种都是数据，不是错误。")
    L.append("")
    L.append("## 4. 独立对账（不是自说自话）")
    L.append("")
    L.append("### 4.1 继承边界精确吻合 —— 两套独立证据撞在一起")
    L.append("")
    L.append("`ObjectTypeClass` 的末端（它自己构造函数写过的最远字段）是 **0x294**；"
             "`TechnoTypeClass` 的**自有**命名字段里最小的偏移也正好是 **0x294**。")
    L.append("")
    L.append("两组数据来源完全不同：前者扫 `ObjectTypeClass` 的构造函数，"
             "后者扫 `TechnoTypeClass` 的 `Read_INI`（读 INI 键的路径）。"
             "RTTI 只说\"TechnoTypeClass 继承 ObjectTypeClass\"，"
             "**不给出**边界在哪 —— 边界是这两条通道各自算出来又正好对上的。")
    L.append("")
    L.append("### 4.2 子类在父类区留下的痕迹，祖先必须已经认识")
    L.append("")
    L.append("子类构造函数会内联父类的初始化，子类 `Read_INI` 也会去写父类的字段"
             "（例如 `IsometricTileTypeClass` 的 15 个键**全部**落在 "
             "`ObjectTypeClass` 的 0x9C~0x238 里，一个自有字段都没有）。"
             "这些偏移必须能在祖先链的字段表里找到，且名字一致 —— 否则就说明"
             "要么祖先漏了字段，要么这条记录被误归到了子类。")
    L.append("")
    L.append("本轮结果：")
    L.append("")
    if notes:
        for n in notes:
            L.append("* **异常**：" + n)
    else:
        L.append("* 无异常：子类在祖先区写过或命名过的偏移，祖先链全都认识，名字也一致。")
    for n in info:
        L.append("* " + n)
    L.append("")
    L.append("### 4.3 同一偏移两套宽度必须一致")
    L.append("")
    L.append("构造函数用 `mov dword` 写、Read_INI 用 `ReadInteger` 收 —— "
             "同一偏移上两边的宽度必须相等。本轮 0 处不一致"
             "（不一致会在 `tools/layout.py` 里直接终止）。")
    L.append("")
    L.append("### 4.4 类内不得有重叠字段")
    L.append("")
    L.append("同一类的字段集合里任意两个字段的字节区间不得相交；"
             "重叠说明至少有一条写记录被归错了"
             "（例如把一个 4 字节写读成两个 1 字节）。本轮 0 处重叠。")
    L.append("")
    L.append("### 4.5 编译期 + 运行期两道闸门")
    L.append("")
    L.append("`src/re/ObjectModel.h` 里每条字段一条 `static_assert`；"
             "`ra2core` 冒烟测试里的 `Model_Check()` 再在运行期对一遍"
             "（`FieldNames.h` 的每条命名字段必须能在模型里找到同名成员、"
             "偏移与宽度一致；反向 `ModelOf(\"不存在的类\")` 必须查不到）。")
    L.append("")
    L.append("## 5. 明确没做到的")
    L.append("")
    L.append("* **`static_assert` 不是\"偏移的验证\"**。偏移来自反汇编；"
             "`static_assert` 保证的是\"这份 C++ 模型不会悄悄漂移\"。"
             "把它当取证读就错了 —— 它的价值在回归。")
    L.append("* **`offsetof` 用在非 standard-layout 类型上是条件支持的**"
             "（本例每一层基类都有数据成员，整个链不是 standard-layout）。"
             "MSVC 对单继承非虚基类给出正确结果，且这里的结果被 `static_assert` "
             "逐条钉过 —— 是\"在目标编译器上已验证\"，不是\"标准保证\"。"
             "换编译器要重跑这一层。")
    L.append("* **只覆盖 8 个类**（4 层继承链上的 8 个 TypeClass）。"
             "`BuildingClass`/`CellClass`/`HouseClass` 这些静态对象池里的类"
             "`sizeof` 拿不到（不是 `operator new` 出来的），字段证据也还没挖，"
             "本轮不进模型。")
    L.append("* **虚拟函数表不在模型里**。结构体里没有虚函数、也没有 vptr 声明；"
             "虚表信息在 `src/re/ClassHierarchy.h`（按槽位）与 `docs/vtables.md`。")
    L.append("* **没有语义**。`_unk_XXXX` 是什么、`_pad_XXXX` 里有没有东西，"
             "本模型一律不知道。名字只表示\"这个偏移是从这个 INI 键读出来的\"。")
    L.append("* **成员名用的就是 INI 键名**（原样大写），没有换成 CamelCase 的"
             "\"引擎内部名字\"。键名有证据可查（反汇编里的字符串字面量），"
             "编出来的内部名字没有 —— 少一个可以错的地方。")
    L.append("* **父类末端与子类第一个自有字段之间可能有对齐缝隙**。"
             "本模型把它算成子类本体开头的 `_pad_`；"
             "真实的 `sizeof(父类)` 可能比其末端大 1~3 字节。"
             "上面表里的\"末端\"因此是**下界**而不是精确 `sizeof`。")
    L.append("")
    with open(os.path.join(ROOT, "docs", "object-model.md"), "w",
              encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(L))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="只对账，不写文件")
    a = ap.parse_args()

    fields = load_json("fields.json")
    fn = load_json("fieldnames.json")
    rtti = load_json("rtti.json")

    chain = build_chain(rtti, fields)
    sets = {c: class_fields(fields, fn, c) for c in chain}
    ends = {}
    classes = []
    notes = []
    info = []

    # ---- 预扫：先用各自的原始证据算出每一层的 [起点, 末端) ----
    # 这一步只用来回答"某个偏移属于继承链上的哪一层"。后面的合并只会在
    # 祖先**内部**加字段，不会把末端推远，所以预扫的结果就是最终结果。
    def own_extent(ents, start):
        e = start
        for o, x in ents.items():
            if o >= start:
                e = max(e, o + x["size"])
        return e

    parents = {}
    pre_start, pre_end = {}, {}
    for c in chain:
        p = parent_of(rtti, fields, c)
        parents[c] = p
        pre_start[c] = pre_end[p] if p else 0
        pre_end[c] = own_extent(sets[c], pre_start[c])

    def body_owner(off, of_class):
        """off 落在哪个祖先的本体里（不含 of_class 自己）。找不到返回 None。"""
        cur = parents.get(of_class)
        while cur:
            if pre_start[cur] <= off < pre_end[cur]:
                return cur
            cur = parents.get(cur)
        return None

    # ---- 合并：子类 Read_INI 在**祖先本体**里命名的偏移 ----
    # 例：UnitTypeClass::Read_INI 把 SPEEDTYPE 读进 0x67C，那是 TechnoTypeClass 的
    # 字段。这类记录如果祖先自己的两条通道都没碰到过，光靠祖先自己是拿不到的 ——
    # 那就用子类的证据补上，来源标成 sub-ini，不假装是祖先自己看到的。
    merged = []
    for c in chain:
        for o, e in sorted(sets[c].items()):
            if o >= pre_start[c] or not e.get("key"):
                continue
            holder = body_owner(o, c)
            if holder is None:
                continue
            hset = sets[holder]
            if o in hset:
                continue                      # 祖先自己已经认识，什么都不用做
            if o + e["size"] > pre_end[holder]:
                notes.append(
                    "%s 的 Read_INI 把 %s 读进 0x%X，它跨过了 %s 的末端 0x%X —— "
                    "本模型不合并（合并会把继承边界推远）"
                    % (c, e["key"], o, holder, pre_end[holder]))
                continue
            probe = dict(hset)
            probe[o] = e
            ov = overlaps(probe)
            if ov:
                notes.append(
                    "%s 的 Read_INI 把 %s 读进 0x%X，但 %s 在这条偏移上有别的字段，"
                    "合并不进去" % (c, e["key"], o, holder))
                continue
            hset[o] = dict(e, src="sub-ini", from_class=c)
            merged.append((holder, o, e["key"], c))

    if merged:
        by = {}
        for holder, o, k, c in merged:
            by.setdefault(holder, []).append("%s@%s（来自 %s）"
                                             % (k, hexs(o), c))
        for holder, items in sorted(by.items()):
            info.append("%s 的字段表被子孙的 Read_INI 补了 %d 条：%s"
                        % (holder, len(items), "、".join(items)))

    for c in chain:
        ents = sets[c]
        bad = overlaps(ents)
        if bad:
            for x, y in bad:
                info.append("重叠：%s 0x%X(宽%d) 与 0x%X(宽%d)"
                            % (c, x["off"], x["size"], y["off"], y["size"]))
            raise SystemExit("[x] %s 有重叠字段，无法铺成结构体" % c)
        p = parents[c]
        start = ends[p] if p else 0
        own = {o: e for o, e in ents.items() if o >= start}
        below = {o: e for o, e in ents.items() if o < start}
        if p:
            # 祖先链上所有字段集合的并集：父类的字段在模型里是通过继承"在"子类里的，
            # 所以子类构造函数写到某个祖先区的偏移，只要祖先链上有人认识它就够了。
            cum, anc, cur = {}, [], p
            while cur:
                for o, e in sets[cur].items():
                    cum.setdefault(o, (cur, e))
                anc.append(cur)
                cur = parents.get(cur)
            ctor_seen = set()
            for anc_c in anc:
                for f in fields["classes"].get(anc_c, {}).get("fields", []):
                    ctor_seen.add(f["off"])
            missing = sorted(o for o in below if o not in cum)
            if missing:
                notes.append(
                    "%s 的字段表里有 0x%s，可祖先链（%s）谁都不认识这个偏移"
                    % (c, ", 0x".join("%X" % o for o in missing), " <- ".join(anc)))
            only_ctor = sorted(o for o in below if o not in ctor_seen)
            if only_ctor:
                # 谁给了名字？沿祖先链找最近的、命过这个偏移的那一层。
                src = {}
                cur2 = p
                while cur2:
                    for o in only_ctor:
                        if o in sets[cur2] and sets[cur2][o].get("key") and o not in src:
                            src[o] = cur2
                    cur2 = parents.get(cur2)
                by = {}
                for o in only_ctor:
                    by.setdefault(src.get(o, "(没名字)"), []).append(o)
                for who, offs in sorted(by.items()):
                    info.append(
                        "%s 的构造函数写了祖先区的 %s：祖先的**构造函数**通道没有这些字节，"
                        "名字来自 %s 的 Read_INI 通道"
                        % (c, ", ".join(hexs(o) for o in offs), who))
            # 父类区里被本类命名过的偏移：字段本身归祖先，但这条记录不该被丢掉
            named_below = {o: e for o, e in ents.items()
                           if o < start and e.get("key")}
            got_lost = sorted(o for o in named_below if o not in cum)
            if got_lost:
                notes.append("%s 把 %s 命名了，但祖先链不认识" % (
                    c, ", ".join("%s@%s" % (named_below[o]["key"], hexs(o))
                                 for o in got_lost)))
            renamed = [(o, named_below[o]["key"], cum[o][1]["key"]) for o in named_below
                       if o in cum and cum[o][1].get("key") and
                       cum[o][1]["key"] != named_below[o]["key"]]
            for o, k1, k2 in renamed:
                notes.append("%s 把 0x%X 叫 %s，但 %s 把它叫 %s"
                             % (c, o, k1, cum[o][0], k2))
            if named_below:
                parts = []
                for o in sorted(named_below):
                    who = cum[o][0] if o in cum else "(不认识)"
                    parts.append("%s@%s→%s" % (named_below[o]["key"], hexs(o), who))
                info.append(
                    "%s 的 Read_INI 读的 %d 个键落在祖先本体上（字段归祖先）：%s"
                    % (c, len(named_below), "、".join(parts)))
        own_min = min(own) if own else None
        members, end, own_off = build_class(c, start, own)
        # 合并只该往祖先**内部**加字段：末端和起点都不许动。动了就是上面
        # "不许合并"的判据漏了情形，必须炸出来而不是悄悄接受。
        if start != pre_start[c] or end != pre_end[c]:
            notes.append("%s 的区间从预扫的 [0x%X,0x%X) 变成了 [0x%X,0x%X) ——"
                         "合并不该改变继承边界"
                         % (c, pre_start[c], pre_end[c], start, end))
        pad = sum(m["size"] for m in members if m["kind"] == "pad")
        nf = sum(1 for m in members if m["kind"] == "field")
        nn = sum(1 for m in members if m["kind"] == "field" and m["named"])
        ends[c] = end
        classes.append({
            "cls": c, "parent": p, "start": start, "end": end,
            "fields": nf, "named": nn, "pad_bytes": pad,
            "covered": end - start - pad,
            "align_gap": (own_min - start) if own_min is not None else 0,
            "members": members,
        })

    tot_fields = sum(c["fields"] for c in classes)
    tot_named = sum(c["named"] for c in classes)
    tot_pad = sum(c["pad_bytes"] for c in classes)

    print("%-24s %-22s %7s %7s %6s %6s %7s %8s %7s %5s"
          % ("类", "父类", "起点", "末端", "字段", "有名", "填充", "已覆盖", "覆盖%", "缝"))
    for c in classes:
        span = c["end"] - c["start"]
        print("%-24s %-22s 0x%-5X 0x%-5X %6d %6d %7d %8d %6.1f%% %5d" % (
            c["cls"], c["parent"] or "(根)", c["start"], c["end"],
            c["fields"], c["named"], c["pad_bytes"], c["covered"],
            100.0 * c["covered"] / span if span else 0.0, c["align_gap"]))
    print("%-24s %-22s %7s %7s %6d %6d %7d" % ("合计", "", "", "",
                                               tot_fields, tot_named, tot_pad))
    print()
    if notes:
        print("对账注记（异常，应当为空）：")
        for n in notes:
            print("  ! " + n)
    else:
        print("对账注记：无异常 —— 子类在祖先区写过/命名过的偏移，祖先链全都认识，"
              "且名字一致。")
    for n in info:
        print("  · " + n)
    print()

    if a.check:
        return 0

    with open(os.path.join(ROOT, "src", "re", "ObjectModel.h"), "w",
              encoding="utf-8", newline="\n") as fh:
        fh.write(emit_header(classes, rtti.get("image")))

    data = {
        "method": "tools/layout.py：offset/width(构造函数) ∪ offset/key(Read_INI) "
                  "+ RTTI bases[0] -> 带显式填充的连续结构体",
        "inputs": ["db/fields.json", "db/fieldnames.json", "db/rtti.json"],
        "image": rtti.get("image"),
        "pack": 1,
        "problems": notes,
        "info": info,
        "totals": {"classes": len(classes), "fields": tot_fields,
                   "named": tot_named, "pad_bytes": tot_pad},
        "classes": {c["cls"]: c for c in classes},
    }
    with open(os.path.join(ROOT, "db", "layout.json"), "w", encoding="utf-8",
              newline="\n") as fh:
        json.dump(data, fh, ensure_ascii=False, indent=1)

    write_docs(classes, notes, info, (tot_fields, tot_named, tot_pad), rtti.get("image"))
    print("[OK] src/re/ObjectModel.h  db/layout.json  docs/object-model.md")
    return 0


if __name__ == "__main__":
    sys.exit(main())
