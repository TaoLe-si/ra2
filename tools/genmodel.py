"""
genmodel.py -- 把 RTTI 结果变成可编译的 C++ 与可读文档。

输入：db/rtti.json（由 tools/rtti.py 从 gamemd.exe 的 MSVC RTTI 抽取）
输出：
  src/re/ClassHierarchy.h   类表（类名 / 基类 / 虚表 VA / 槽位数），带运行时反查
  src/re/virtuals.inc        扁平化的「虚表槽位 -> 函数 VA」数组（供 ClassHierarchy.h 包含）
  docs/class-hierarchy.md    人类可读的继承树 + 全部类清单
  db/virtuals.json           机器可读的每类槽位表

为什么这么做：
  虚表地址在运行时就是对象的 vptr。有了这张表，还原出的 C++ 代码可以在运行时
  通过 vptr 反查「这个对象到底是什么类」，这对多核改造时做对象类型分流、
  以及验证我们对类层次的理解是否与二进制一致，都是硬需求。
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


def cpp_ident(name: str) -> str:
    """把任意类名压成合法 C++ 标识符。"""
    s = re.sub(r"[^0-9A-Za-z]", "_", name)
    s = re.sub(r"_+", "_", s).strip("_")
    if s and s[0].isdigit():
        s = "_" + s
    return s or "Unnamed"


def cpp_str(s: str) -> str:
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def build(rtti: dict):
    classes = rtti["classes"]

    # 只保留主虚表（offset_in_class == 0），避免多重继承的副表混入
    info = {}   # name -> dict(name, base, vt_va, count, entries)
    for name, c in classes.items():
        prim = [v for v in c["vtables"] if v["offset_in_class"] == 0] or c["vtables"]
        v = max(prim, key=lambda x: x["count"])
        info[name] = {
            "name": name,
            "base": c["base"],
            "vt_va": v["va"],
            "count": v["count"],
            "entries": v["entries"],
            "vtables": len(c["vtables"]),
        }

    names = sorted(info, key=lambda x: (x.lower(), x))
    index = {n: i for i, n in enumerate(names)}
    return info, names, index


def gen_header(info, names, index, out_path: str, inc_name: str = "virtuals.inc"):
    # 扁平槽位数组
    offsets = []
    flat = []
    for n in names:
        e = info[n]
        offsets.append(len(flat))
        flat.extend(e["entries"])

    lines = []
    lines.append("// 自动生成文件，请勿手改。生成工具：tools/genmodel.py")
    lines.append("// 数据来源：gamemd.exe 的 MSVC RTTI（db/rtti.json），非人工猜测。")
    lines.append("#pragma once")
    lines.append("#include <cstdint>")
    lines.append("#include <cstring>")
    lines.append("")
    lines.append("namespace ra2 {")
    lines.append("namespace re {")
    lines.append("")
    lines.append("// 每个类的 RTTI 信息。base_index 为 -1 表示无基类。")
    lines.append("struct ClassInfo {")
    lines.append("    const char* name;")
    lines.append("    int         base_index;")
    lines.append("    uint32_t    vtable_va;   // 主虚表 VA（即对象的 vptr 值）")
    lines.append("    uint16_t    slots;       // 主虚表槽位数")
    lines.append("    uint16_t    vtables;     // 该类拥有的虚表总数（多重继承 >1）")
    lines.append("};")
    lines.append("")
    lines.append("inline constexpr int kClassCount = %d;" % len(names))
    lines.append("")
    lines.append("inline constexpr ClassInfo kClassTable[kClassCount] = {")
    for n in names:
        e = info[n]
        b = index.get(e["base"], -1) if e["base"] else -1
        lines.append("    {%s, %d, 0x%08X, %d, %d}," % (
            cpp_str(n), b, e["vt_va"], e["count"], e["vtables"]))
    lines.append("};")
    lines.append("")
    lines.append("// 每个类一个常量索引，写起来比 ClassIndex(\"...\") 顺手，且带编译期校验。")
    # 类名里带模板/命名空间符号的（如 "A::?$B" 和 "A_B" 都可能压成同一个标识符），
    # 这里做去重，否则头文件根本编不过。
    used: dict[str, int] = {}
    for i, n in enumerate(names):
        ident = cpp_ident(n)
        if ident in used:
            used[ident] += 1
            ident = "%s_dup%d" % (ident, used[ident])
        else:
            used[ident] = 0
        lines.append("inline constexpr int kIndex%s = %d;" % (ident, i))
    lines.append("")
    lines.append("// 扁平化的槽位表：第 i 个类的槽位区间是 kFlatSlots[kSlotOffset[i] .. +slots)")
    lines.append("inline constexpr uint32_t kSlotOffset[kClassCount] = {")
    for i in range(0, len(offsets), 8):
        lines.append("    " + " ".join("%d," % x for x in offsets[i:i + 8]))
    lines.append("};")
    lines.append("")
    lines.append("inline constexpr uint32_t kFlatSlotCount = %d;" % len(flat))
    lines.append("namespace detail {")
    lines.append("inline constexpr uint32_t kFlatSlots[kFlatSlotCount] = {")
    for i in range(0, len(flat), 8):
        lines.append("    " + " ".join("0x%08X," % x for x in flat[i:i + 8]))
    lines.append("};")
    lines.append("}  // namespace detail")
    lines.append("")
    lines.append("// 编译期字符串比较，供 constexpr 查表使用")
    lines.append("inline constexpr bool ConstEq(const char* a, const char* b) {")
    lines.append("    while (*a && *b) {")
    lines.append("        if (*a != *b) return false;")
    lines.append("        ++a; ++b;")
    lines.append("    }")
    lines.append("    return *a == *b;")
    lines.append("}")
    lines.append("")
    lines.append("// 按名字查类（编译期可用）；找不到返回 -1")
    lines.append("inline constexpr int ClassIndex(const char* name) {")
    lines.append("    for (int i = 0; i < kClassCount; ++i)")
    lines.append("        if (ConstEq(kClassTable[i].name, name)) return i;")
    lines.append("    return -1;")
    lines.append("}")
    lines.append("")
    lines.append("// 按名字查类（运行期，走 strcmp，比 ConstEq 快）；找不到返回 -1")
    lines.append("inline int FindClass(const char* name) {")
    lines.append("    for (int i = 0; i < kClassCount; ++i)")
    lines.append("        if (std::strcmp(kClassTable[i].name, name) == 0) return i;")
    lines.append("    return -1;")
    lines.append("}")
    lines.append("")
    lines.append("// 按运行时 vptr 查类；找不到返回 -1。")
    lines.append("// 用法：auto* o = ...; int id = FindClassByVTable(*(uint32_t*)o);")
    lines.append("inline int FindClassByVTable(uint32_t vptr) {")
    lines.append("    for (int i = 0; i < kClassCount; ++i)")
    lines.append("        if (kClassTable[i].vtable_va == vptr) return i;")
    lines.append("    return -1;")
    lines.append("}")
    lines.append("")
    lines.append("// 取第 cls 个类第 slot 号虚函数的入口 VA；越界返回 0")
    lines.append("inline constexpr uint32_t VirtualEntry(int cls, int slot) {")
    lines.append("    if (cls < 0 || cls >= kClassCount) return 0;")
    lines.append("    if (slot < 0 || slot >= kClassTable[cls].slots) return 0;")
    lines.append("    return detail::kFlatSlots[kSlotOffset[cls] + slot];")
    lines.append("}")
    lines.append("")
    lines.append("// 判断 derived 是否是 base 的派生类（含自身）")
    lines.append("inline constexpr bool IsDerivedFrom(int derived, int base) {")
    lines.append("    int guard = 0;")
    lines.append("    while (derived >= 0 && guard++ < 64) {")
    lines.append("        if (derived == base) return true;")
    lines.append("        derived = kClassTable[derived].base_index;")
    lines.append("    }")
    lines.append("    return false;")
    lines.append("}")
    lines.append("")
    lines.append("}  // namespace re")
    lines.append("}  // namespace ra2")
    lines.append("")
    open(out_path, "w", encoding="utf-8", newline="\n").write("\n".join(lines))
    return len(flat)


def gen_doc(info, names, index, out_path: str):
    kids = {}
    for n in names:
        kids.setdefault(info[n]["base"], []).append(n)
    for k in kids:
        kids[k].sort(key=lambda x: x.lower())

    roots = [n for n in names if not info[n]["base"]]
    roots.sort(key=lambda x: x.lower())

    lines = [
        "# gamemd.exe 类层次（RTTI 自动抽取）",
        "",
        "由 `tools/genmodel.py` 依据 `tools/rtti.py` 抽取的 MSVC RTTI 生成。",
        "类名、基类、虚表地址、槽位数**全部来自二进制本身**，不是猜测。",
        "",
        "- 具名类 / 模板实例总数：%d" % len(names),
        "",
    ]

    def emit_tree(n, depth, out, seen):
        if depth > 6 or n in seen:
            return
        seen.add(n)
        e = info[n]
        out.append("%s- `%s` — %d 槽，虚表 `0x%08X`" % (
            "  " * depth, n, e["count"], e["vt_va"]))
        for k in kids.get(n, []):
            emit_tree(k, depth + 1, out, seen)

    lines += ["## 游戏对象模型（`AbstractClass` 子树）", ""]
    tree = []
    emit_tree("AbstractClass", 0, tree, set())
    lines += tree

    lines += ["", "## 其余继承树（按根名字排序）", ""]
    seen = set()
    for r in roots:
        if r == "AbstractClass":
            continue
        t = []
        emit_tree(r, 0, t, seen)
        if t:
            lines += t

    lines += ["", "## 全部类清单", ""]
    lines += ["| 类 | 基类 | 槽位数 | 虚表 VA |", "|---|---|---:|---|"]
    for n in names:
        e = info[n]
        lines.append("| `%s` | %s | %d | `0x%08X` |" % (
            n, "`%s`" % e["base"] if e["base"] else "—", e["count"], e["vt_va"]))

    open(out_path, "w", encoding="utf-8", newline="\n").write("\n".join(lines) + "\n")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--rtti", default=os.path.join(ROOT, "db", "rtti.json"))
    ap.add_argument("--outdir", default=os.path.join(ROOT, "src", "re"))
    a = ap.parse_args()

    rtti = json.load(open(a.rtti, encoding="utf-8"))
    info, names, index = build(rtti)

    os.makedirs(a.outdir, exist_ok=True)
    hdr = os.path.join(a.outdir, "ClassHierarchy.h")
    nflat = gen_header(info, names, index, hdr)

    gen_doc(info, names, index, os.path.join(ROOT, "docs", "class-hierarchy.md"))

    virt = {n: info[n]["entries"] for n in names}
    with open(os.path.join(ROOT, "db", "virtuals.json"), "w", encoding="utf-8") as f:
        json.dump({"classes": len(names), "slots_total": nflat, "virtuals": virt}, f)

    print("classes=%d flat_slots=%d -> %s ; docs/class-hierarchy.md ; db/virtuals.json" % (
        len(names), nflat, os.path.relpath(hdr, ROOT)))


if __name__ == "__main__":
    main()
