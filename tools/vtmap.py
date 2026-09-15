r"""
vtmap.py -- 把虚函数表和类名对应起来。

难点：本二进制没有 RTTICompleteObjectLocator（见 docs/binary-baseline.md §6），
无法直接查表得到"这张虚表属于哪个类"。于是用两条证据链来推断：

  证据一（引用点）：谁引用了这张虚表？
      构造函数里一定有 `mov [this], offset vftable`，
      析构/delete 流程里也会再引用一次。
      把这些引用点归属到函数，再看这些函数引用了哪些字符串——
      如果字符串里出现类名/INI 段名，就是一条线索。

  证据二（前缀包含）：虚表之间的"槽位前缀"关系 = 继承关系。
      派生类的虚表前 k 项与基类完全一致（k = 基类的槽位数），后面才是新增的。
      所以：若 A ⊃ B 且 A 更长，A 的类很可能派生自 B 的类。
      这条不依赖任何字符串，纯结构推断，可信度较高。

产出：
  db/vtmap.json   机器可读的虚表证据与继承关系
  docs/vtables.md 人类可读的表格，供人工确认后回填到 C++ 代码
"""

from __future__ import annotations

import bisect
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DB = os.path.join(HERE, "..", "db")
DOCS = os.path.join(HERE, "..", "docs")
sys.path.insert(0, HERE)

from peimage import PEImage  # noqa: E402


def load(name):
    with open(os.path.join(DB, name), encoding="utf-8") as f:
        return json.load(f)


def main() -> None:
    img = PEImage()
    ib = img.image_base
    vtables = load("vtables.json")
    funcs = list(load("functions.json").values())
    strings = {int(k, 16): v for k, v in load("strings.json").items()}

    lo, hi = img.text_range()
    off0 = img.rva_to_off(lo)
    blob = img.data[off0:off0 + (hi - lo)]

    # ---- 证据一：谁引用了这张虚表 ----
    vt_by_va = {v["va"]: v for v in vtables}
    pats = {struct.pack("<I", va): va for va in vt_by_va}

    refs: dict[int, list[int]] = {}
    for k in range(len(blob) - 3):
        va = pats.get(blob[k:k + 4])
        if va is not None:
            refs.setdefault(va, []).append(lo + k)

    # 函数区间排序，用于把引用点归属到函数
    bounds = sorted((f for f in funcs if f["src"] != "gap"), key=lambda f: f["rva"])
    keys = [f["rva"] for f in bounds]

    def owner(rva: int):
        j = bisect.bisect_right(keys, rva) - 1
        if j < 0:
            return None
        f = bounds[j]
        return f if f["rva"] <= rva < f["end"] else None

    # ---- 证据二：槽位相似度 -> 继承候选 ----
    # 注意：不能用"前缀完全一致"做判据。派生类一旦重写（override）了基类的虚函数，
    # 对应槽位的值就变了，前缀必然在某处断开。所以改成在基类槽位范围内统计
    # 相同比例——未被重写的槽位仍然指向基类的实现，这个比例会很高。
    def prefix_len(a: list[int], b: list[int]) -> int:
        n = 0
        for x, y in zip(a, b):
            if x != y:
                break
            n += 1
        return n

    def overlap_ratio(a: list[int], b: list[int]) -> float:
        """a 与更短的 b 在 b 的槽位范围内的一致比例"""
        n = min(len(a), len(b))
        if n == 0:
            return 0.0
        return sum(1 for i in range(n) if a[i] == b[i]) / n

    results = []
    for vt in vtables:
        rva_list = refs.get(vt["va"], [])
        owners = {}
        for r in rva_list:
            f = owner(r)
            if f is None:
                continue
            owners.setdefault(f["va"], []).append(r)
        # 引用该虚表的函数，以及它们引用的字符串
        user_funcs = []
        for fva, locs in owners.items():
            f = next(x for x in funcs if x["va"] == fva)
            strs = [strings.get(s, "") for s in f["strings"]]
            strs = [s for s in strs if s and len(s) < 200]
            user_funcs.append({
                "va": fva,
                "size": f["size"],
                "insns": f["insns"],
                "refs": sorted(locs),
                "strings": sorted(set(strs))[:12],
            })
        user_funcs.sort(key=lambda x: -x["insns"])
        results.append({
            "va": vt["va"],
            "rva": vt["rva"],
            "count": vt["count"],
            "entries": vt["entries"],
            "section": vt["section"],
            "users": user_funcs,
        })

    # 继承候选：A 的槽位更多，且在 B 的槽位范围内一致比例够高
    results.sort(key=lambda x: -x["count"])
    for a in results:
        cands = []
        for b in results:
            if b is a or b["count"] >= a["count"]:
                continue
            r = overlap_ratio(a["entries"], b["entries"])
            if r >= 0.6:
                cands.append({
                    "va": b["va"],
                    "base_slots": b["count"],
                    "ratio": round(r, 3),
                    "exact_prefix": prefix_len(a["entries"], b["entries"]),
                })
        # 优先取槽位多、一致比例高的
        cands.sort(key=lambda x: (-x["base_slots"], -x["ratio"]))
        a["base_candidates"] = cands[:3]

    os.makedirs(DOCS, exist_ok=True)
    with open(os.path.join(DB, "vtmap.json"), "w", encoding="utf-8") as f:
        json.dump(results, f, ensure_ascii=False, indent=1)

    # ---- 报告 ----
    lines = [
        "# 虚函数表映射证据（供人工确认）",
        "",
        "由 `tools/vtmap.py` 生成。本二进制**没有** RTTICompleteObjectLocator，",
        "所以下表是推断而非查表结果 —— 每一行都需要人工过一遍才能回填到 C++ 代码。",
        "",
        "- 候选虚表总数：%d" % len(results),
        "- 有代码引用的虚表：%d" % sum(1 for r in results if r["users"]),
        "",
        "| # | 虚表 VA | 槽位 | 引用它的函数（指令数） | 函数引用的字符串线索 | 继承候选（槽位一致比例） |",
        "|---:|---|---:|---|---|---|",
    ]
    for i, r in enumerate(results[:80], 1):
        users = "，".join("`0x%08X`(%d)" % (u["va"], u["insns"]) for u in r["users"][:3]) or "—"
        hints: list[str] = []
        for u in r["users"][:3]:
            for s in u["strings"]:
                if s.startswith(".?A"):
                    continue
                hints.append(s)
        hint_txt = " / ".join('`%s`' % h[:44] for h in hints[:3]) or "—"
        bases = "，".join("`0x%08X`(%d槽,%.0f%%)" % (c["va"], c["base_slots"], c["ratio"] * 100)
                         for c in r["base_candidates"][:2]) or "—"
        lines.append("| %d | `0x%08X` | %d | %s | %s | %s |" % (
            i, r["va"], r["count"], users, hint_txt, bases))
    with open(os.path.join(DOCS, "vtables.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")

    print("虚表 %d 张，有引用的 %d 张" % (len(results), sum(1 for r in results if r["users"])))


if __name__ == "__main__":
    main()
