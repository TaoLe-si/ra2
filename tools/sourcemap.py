r"""
sourcemap.py -- 用二进制里残留的 assert 宏路径还原原始源码结构。

gamemd.exe 保留了大量 `assert` 失败时用的 __FILE__ 字符串，形如：
    D:\ra2mdpost\MainLoop.CPP
    D:\ra2mdpost\House.CPP
这些字符串由引用它们的函数锚定，于是可以把「原始 .cpp 文件」映射到「代码地址区间」，
进而大致还原 Westwood 当年 ra2mdpost 工程的切分方式——这正是源码还原时目录结构的依据。

产出：
  db/sources.json  文件名 -> 关联函数列表
  docs/source-map.md
"""

from __future__ import annotations

import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DB = os.path.join(HERE, "..", "db")
DOCS = os.path.join(HERE, "..", "docs")

PATH_RE = re.compile(r"[A-Za-z]:\\[^\s\"']*?\.(?:CPP|cpp|H|h|HPP|hpp|INL|inl)")


def load(name):
    with open(os.path.join(DB, name), encoding="utf-8") as f:
        return json.load(f)


def main() -> None:
    strings = {int(k, 16): v for k, v in load("strings.json").items()}
    funcs = list(load("functions.json").values())

    # 字符串 VA -> 出现在其中的源码路径
    path_of: dict[int, list[str]] = {}
    for va, s in strings.items():
        hits = PATH_RE.findall(s)
        if hits:
            path_of[va] = hits

    files: dict[str, dict] = {}
    for f in funcs:
        if f["src"] == "gap":
            continue
        for sva in f["strings"]:
            for p in path_of.get(sva, ()):
                base = os.path.basename(p)
                e = files.setdefault(base, {
                    "file": base,
                    "path": p,
                    "functions": [],
                    "min_va": f["va"],
                    "max_va": f["va"] + f["size"],
                    "total_insns": 0,
                })
                e["functions"].append({
                    "va": f["va"], "insns": f["insns"], "size": f["size"],
                })
                e["min_va"] = min(e["min_va"], f["va"])
                e["max_va"] = max(e["max_va"], f["va"] + f["size"])
                e["total_insns"] += f["insns"]

    for e in files.values():
        e["functions"].sort(key=lambda x: -x["insns"])

    os.makedirs(DOCS, exist_ok=True)
    with open(os.path.join(DB, "sources.json"), "w", encoding="utf-8") as f:
        json.dump(files, f, ensure_ascii=False, indent=1)

    lines = [
        "# 原始源码文件映射（由 assert 的 __FILE__ 字符串恢复）",
        "",
        "构建机路径前缀为 `D:\\ra2mdpost\\`（YR 发行后的分支）。",
        "下表把每个原始文件锚定到它在 gamemd.exe 中的代码区间——区间内其它未直接引用",
        "路径字符串的函数，通常也属于同一个编译单元，因为它们按 COMDAT 顺序连续排布。",
        "",
        "| 原始文件 | 函数数 | 指令数 | 代码区间 | 最大函数 |",
        "|---|---:|---:|---|---|",
    ]
    for name in sorted(files, key=lambda x: x.lower()):
        e = files[name]
        top = e["functions"][0] if e["functions"] else None
        lines.append("| `%s` | %d | %d | `0x%06X`-`0x%06X` | %s |" % (
            e["file"], len(e["functions"]), e["total_insns"],
            e["min_va"], e["max_va"],
            "`0x%08X` (%d 指令)" % (top["va"], top["insns"]) if top else "—"))
    with open(os.path.join(DOCS, "source-map.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")

    print("原始源码文件 %d 个，锚定函数 %d 个" % (
        len(files), sum(len(e["functions"]) for e in files.values())))


if __name__ == "__main__":
    main()
