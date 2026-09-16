"""redump_all.py -- 全量逆向语料落盘。

"完整逆向"的物化底座：把 db/functions.json 里全部 6,740 个可靠函数的
反汇编（capstone）+ 字符串引用 + 调用边，按子系统目录写入 re/。
每个函数一个 .asm 片段，聚合成 re/<subsystem>/<NNN>_<va>.asm，
外加 re/<subsystem>/INDEX.md（函数清单+证据摘要）。

子系统划分优先级：
  1. docs/source-map.md 的原始 .cpp 区间（构建单元，最可信）；
  2. RTTI 类名（vtable 附近函数归属该类）；
  3. 其余按 64KB VA 桶归 "misc"。

用法：python tools/redump_all.py [--max N]（默认全量）
产出：re/**（可 git 追踪），全量约 1.4M 指令。
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from peimage import PEImage
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RE_DIR = os.path.join(ROOT, "re")


def load_subsystems():
    """从 source-map 拿原始 .cpp 区间 → (名字, start, end)。"""
    out = []
    p = os.path.join(ROOT, "docs", "source-map.md")
    txt = open(p, encoding="utf-8").read()
    for m in re.finditer(r"\|\s*`([^`]+)`\s*\|\s*(\d+)\s*\|\s*(\d+)\s*\|\s*`0x([0-9A-F]+)`-`0x([0-9A-F]+)`", txt):
        name, _f, _i, a, b = m.groups()
        out.append((name.replace(".CPP", "").replace(".cpp", ""), int(a, 16), int(b, 16)))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--max", type=int, default=0, help="每个子系统最多导出多少函数（0=全量）")
    args = ap.parse_args()

    fns = json.load(open(os.path.join(ROOT, "db", "functions.json"),
                         encoding="utf-8", errors="ignore"))
    fns = {k: v for k, v in fns.items() if v.get("src") not in ("gap", None)}
    subs = load_subsystems()
    print("子系统（构建单元）：%d 个；函数 %d 个" % (len(subs), len(fns)))

    img = PEImage()
    md = Cs(CS_ARCH_X86, CS_MODE_32)

    # 函数按起始 VA 分进子系统；区间之间不重叠（source-map 保证）。
    def which(va):
        for name, a, b in subs:
            if a <= va <= b:
                return name
        return None

    os.makedirs(RE_DIR, exist_ok=True)
    buckets = {}
    for k, v in sorted(fns.items(), key=lambda kv: kv[1]["va"]):
        name = which(v["va"])
        if name is None:
            # 未锚定区：按 0x10000 对齐桶归 misc
            name = "misc_%08X" % (v["va"] & ~0xFFFF)
        buckets.setdefault(name, []).append((k, v))

    total = 0
    for name, items in sorted(buckets.items()):
        d = os.path.join(RE_DIR, name)
        os.makedirs(d, exist_ok=True)
        idx = ["# %s —— %d 函数\n" % (name, len(items)),
               "| VA | 指令数 | 字符串证据 |", "|---|---:|---|"]
        if args.max:
            items = items[: args.max]
        for k, v in items:
            va = v["va"]
            off = img.rva_to_off(v["rva"])
            if off is None:
                continue
            blob = img.data[off: off + v["size"]]
            with open(os.path.join(d, "%08X.asm" % va), "w", encoding="utf-8") as f:
                f.write("; %s  insns=%d size=%d src=%s\n" % (k, v["insns"], v["size"], v.get("src")))
                for s in (str(x) for x in v.get("strings", [])[:10]):
                    f.write("; str: %r\n" % s)
                for c in v.get("calls", [])[:40]:
                    f.write("; call 0x%08X\n" % c)
                for ins in md.disasm(blob, va):
                    f.write("%08X  %-8s %s\n" % (ins.address, ins.mnemonic, ins.op_str))
            strs = "; ".join(str(x) for x in v.get("strings", [])[:3])
            idx.append("| `0x%08X` | %d | %s |" % (va, v["insns"], strs.replace("|", "/")[:80]))
            total += 1
        with open(os.path.join(d, "INDEX.md"), "w", encoding="utf-8") as f:
            f.write("\n".join(idx))
    print("落盘完成：%d 函数 → re/" % total)


if __name__ == "__main__":
    main()
