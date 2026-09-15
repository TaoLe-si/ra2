r"""
query.py -- 在分析结果上做检索，是逆向时最常用的入口。

典型用法：
  # 1) 找包含某个子串的字符串，以及引用它们的函数
  python tools/query.py --string "findpath"

  # 2) 看某个函数在哪个地址、被谁调用、调用了谁
  python tools/query.py --func 0x005659F0

  # 3) 列出指令数最多的函数（热点粗筛）
  python tools/query.py --hot 30

  # 4) 列出最大的虚函数表
  python tools/query.py --vtables 20
"""

from __future__ import annotations

import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DB = os.path.join(HERE, "..", "db")


def _load(name: str):
    p = os.path.join(DB, name)
    if not os.path.exists(p):
        print("缺少 %s，请先运行 tools/analyze.py" % p, file=sys.stderr)
        return {}
    with open(p, encoding="utf-8") as f:
        return json.load(f)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--string", help="按子串搜索字符串，并显示引用它的函数")
    ap.add_argument("--func", help="查看函数详情（十六进制 VA）")
    ap.add_argument("--hot", type=int, metavar="N", help="指令数最多的 N 个函数")
    ap.add_argument("--vtables", type=int, metavar="N", help="槽位数最多的 N 张虚表")
    ap.add_argument("--callers", help="查找调用了该地址的所有函数（十六进制 VA）")
    a = ap.parse_args()

    strings = {int(k, 16): v for k, v in _load("strings.json").items()}
    funcs = _load("functions.json")
    vtables = _load("vtables.json")

    if a.string:
        pat = a.string.lower()
        hits = [(va, s) for va, s in strings.items() if pat in s.lower()]
        print("匹配字符串 %d 条：" % len(hits))
        for va, s in hits[:40]:
            print("  0x%08X  %s" % (va, s[:110]))
            users = [f for f in funcs.values() if va in f["strings"]]
            for f in users[:6]:
                print("        <- 0x%08X  (%d 字节 / %d 指令, src=%s)"
                      % (f["va"], f["size"], f["insns"], f["src"]))
            if not users:
                print("        <- (无函数引用)")
        return

    if a.func:
        va = int(a.func, 16)
        f = funcs.get("0x%08X" % va)
        if not f:
            print("未找到函数 0x%08X" % va)
            return
        print("函数 0x%08X - 0x%08X  (%d 字节 / %d 指令 / src=%s)"
              % (f["va"], f["va"] + f["size"], f["size"], f["insns"], f["src"]))
        print("  调用 (%d):" % len(f["calls"]))
        for c in f["calls"][:40]:
            g = funcs.get("0x%08X" % c)
            tail = "  (%d 指令)" % g["insns"] if g else ""
            print("    0x%08X%s" % (c, tail))
        print("  引用字符串:")
        for s in f["strings"][:40]:
            print("    0x%08X  %s" % (s, strings.get(s, "?")[:100]))
        return

    if a.callers:
        va = int(a.callers, 16)
        out = [f for f in funcs.values() if va in f["calls"]]
        print("调用 0x%08X 的函数 %d 个：" % (va, len(out)))
        for f in sorted(out, key=lambda x: -x["insns"])[:40]:
            print("  0x%08X  (%d 指令, src=%s)" % (f["va"], f["insns"], f["src"]))
        return

    if a.hot:
        top = sorted((f for f in funcs.values() if f["src"] != "gap"),
                     key=lambda x: -x["insns"])[:a.hot]
        print("指令数最多的 %d 个函数（已排除线性兜底）：" % len(top))
        for f in top:
            strs = [strings.get(s, "") for s in f["strings"]]
            hint = max(strs, key=len) if strs else ""
            print("  0x%08X  %6d 指令  %5d 字节  调用%3d  | %s"
                  % (f["va"], f["insns"], f["size"], len(f["calls"]), hint[:70]))
        return

    if a.vtables:
        top = sorted(vtables, key=lambda x: -x["count"])[:a.vtables]
        print("槽位数最多的 %d 张虚表：" % len(top))
        for v in top:
            print("  0x%08X  %3d 槽  首槽函数 0x%08X  (%s)"
                  % (v["va"], v["count"], v["entries"][0], v["section"]))
        return

    ap.print_help()


if __name__ == "__main__":
    main()
