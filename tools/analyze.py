r"""
analyze.py -- gamemd.exe 静态分析总控。

一次性产出：
  db/strings.json    所有字符串及其 VA
  db/functions.json  函数表（起止 VA、规模、出边、引用的字符串）
  db/vtables.json    虚函数表（位置、槽位数、槽位函数）
  db/names.json      由内嵌 "Class::Method" 字符串推断出的函数命名
  db/summary.md      人类可读的基线摘要

用法：
  python tools/analyze.py                 # 分析默认镜像 D:\westwood\RA2YR\gamemd.exe
  python tools/analyze.py --image <path> --outdir <dir>
"""

from __future__ import annotations

import argparse
import json
import os
import re
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from peimage import PEImage, DEFAULT_IMAGE  # noqa: E402
from disasm import build_index, FunctionFinder, F_CALL  # noqa: E402

STR_MIN = 5
STR_RE = re.compile(rb"[ -~]{%d,}" % STR_MIN)
# 形如 "FooClass::Bar" / "Foo::Bar" 的调试串，用于给函数命名
NAME_RE = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*Class|[A-Za-z_][A-Za-z0-9_]*)::([A-Za-z_][A-Za-z0-9_]*)")


def t(msg: str, t0: float) -> float:
    now = time.time()
    print("[%6.1fs] %s" % (now - t0, msg), flush=True)
    return now


# ---------------- 字符串 ----------------
def extract_strings(img: PEImage) -> dict[int, str]:
    out: dict[int, str] = {}
    for sec in img.sections:
        if sec["name"] == ".rsrc":
            continue
        off = sec["raw_ptr"]
        size = min(sec["raw_size"], sec["vsize"] or sec["raw_size"])
        blob = img.data[off:off + size]
        for m in STR_RE.finditer(blob):
            s = m.group().decode("ascii")
            rva = sec["rva"] + m.start()
            out[img.image_base + rva] = s
    return out


# ---------------- 虚表 ----------------
def collect_data_refs(img: PEImage) -> dict[int, int]:
    """扫描 .text，统计每个 4 字节立即数把哪些数据地址当成了指针常量。

    虚表起点一定被引用过（构造函数里的 mov [this], offset vftable），
    所以引用次数 > 0 是判断"这里是某张表的开头"的强证据。
    """
    lo, hi = img.text_range()
    off0 = img.rva_to_off(lo)
    blob = img.data[off0:off0 + (hi - lo)]
    ib = img.image_base
    refs: dict[int, int] = {}
    for k in range(len(blob) - 3):
        (v,) = struct.unpack_from("<I", blob, k)
        rva = v - ib
        if rva <= 0:
            continue
        sec = img.section_of(rva)
        if sec in (".rdata", ".data"):
            refs[rva] = refs.get(rva, 0) + 1
    return refs


def find_vtables(img: PEImage, data_refs: dict[int, int], min_slots: int = 3,
                 max_slots: int = 128, sections=(".rdata",)) -> list[dict]:
    """找连续的 .text 指针数组 = 虚函数表。

    定界规则（比"是否已知函数起点"可靠得多）：
      * 起点：被代码引用过；
      * 延伸：值落在 .text 范围内即可，不要求已被识别为函数起点
        —— 虚函数里有一部分是 thunk 或非标准开场，用函数表会提前截断；
      * 终点：遇到非 .text 指针，或遇到另一个"被引用过"的地址
        （MSVC 把虚表连续排布，后一张表的开头会被引用于它自己的构造函数）。
    """
    lo, hi = img.text_range()
    out = []
    for sec in img.sections:
        if sec["name"] not in sections:
            continue
        off = sec["raw_ptr"]
        size = min(sec["raw_size"], sec["vsize"] or sec["raw_size"])
        blob = img.data[off:off + size]
        n = size // 4
        vals = struct.unpack_from("<%dI" % n, blob, 0)
        i = 0
        while i < n:
            if lo <= vals[i] - img.image_base < hi:
                j = i
                while j < n and lo <= vals[j] - img.image_base < hi:
                    if j > i and data_refs.get(sec["rva"] + j * 4, 0) > 0:
                        break  # 下一张表的开头
                    j += 1
                start_rva = sec["rva"] + i * 4
                if min_slots <= j - i <= max_slots and data_refs.get(start_rva, 0) > 0:
                    out.append({
                        "rva": start_rva,
                        "va": img.image_base + start_rva,
                        "count": j - i,
                        "entries": list(vals[i:j]),
                        "section": sec["name"],
                        "refs": data_refs.get(start_rva, 0),
                    })
                i = j if j > i else i + 1
            else:
                i += 1
    return out


# ---------------- 命名 ----------------
def build_names(img: PEImage, funcs: dict[int, dict], strings: dict[int, str]) -> dict[int, dict]:
    """用函数体内引用的 "Class::Method" 字符串给函数起名"""
    names: dict[int, dict] = {}
    for f in funcs.values():
        cands: dict[str, int] = {}
        for sva in f["strings"]:
            s = strings.get(sva)
            if not s:
                continue
            for m in NAME_RE.finditer(s):
                cls, meth = m.group(1), m.group(2)
                key = "%s::%s" % (cls, meth)
                cands[key] = cands.get(key, 0) + 1
        if not cands:
            continue
        key = max(cands, key=cands.get)
        names[f["va"]] = {
            "va": f["va"],
            "guess": key,
            "evidence": cands,
            "size": f["size"],
        }
    return names


# ---------------- 主流程 ----------------
def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", default=DEFAULT_IMAGE)
    ap.add_argument("--outdir", default=os.path.join(os.path.dirname(__file__), "..", "db"))
    ap.add_argument("--no-gaps", action="store_true", help="跳过线性兜底（更快，函数数更少）")
    a = ap.parse_args()
    outdir = os.path.abspath(a.outdir)
    os.makedirs(outdir, exist_ok=True)

    t0 = time.time()
    img = PEImage(a.image)
    t("PE 载入完成 (ImageBase=0x%08X)" % img.image_base, t0)

    strings = extract_strings(img)
    t("字符串 %d 条" % len(strings), t0)

    idx = build_index(img)
    t("反汇编 %d 条指令" % idx.decoded, t0)

    ff = FunctionFinder(img, idx)
    n_entry = ff.seed()
    n_pro = ff.seed_prologues()
    t("函数播种 entry=%d prologue=%d -> %d 个" % (n_entry, n_pro, len(ff.funcs)), t0)
    if not a.no_gaps:
        ff.seed_gaps()
        t("线性兜底后 %d 个函数" % len(ff.funcs), t0)

    ff.link_refs(strings.keys())
    t("引用关联完成", t0)

    funcs = ff.funcs
    data_refs = collect_data_refs(img)
    t("数据引用点 %d 个" % len(data_refs), t0)
    vtables = find_vtables(img, data_refs)
    t("虚函数表 %d 张" % len(vtables), t0)

    names = build_names(img, funcs, strings)
    t("可命名函数 %d 个" % len(names), t0)

    # ---- 落盘 ----
    with open(os.path.join(outdir, "strings.json"), "w", encoding="utf-8") as f:
        json.dump({"0x%08X" % k: v for k, v in strings.items()}, f, ensure_ascii=False)
    with open(os.path.join(outdir, "functions.json"), "w", encoding="utf-8") as f:
        json.dump(ff.to_json(), f)
    with open(os.path.join(outdir, "vtables.json"), "w", encoding="utf-8") as f:
        json.dump(vtables, f)
    with open(os.path.join(outdir, "names.json"), "w", encoding="utf-8") as f:
        json.dump({"0x%08X" % k: v for k, v in names.items()}, f, ensure_ascii=False)
    t("已写出 db/", t0)

    # ---- 摘要 ----
    big = sorted(funcs.values(), key=lambda x: -x["size"])[:40]
    lines = [
        "# gamemd.exe 静态分析基线",
        "",
        "- 镜像：`%s`" % img.path,
        "- 文件大小：%d 字节" % len(img.data),
        "- ImageBase：`0x%08X`（重定位表已剥离，静态地址 == 运行时地址）" % img.image_base,
        "- 入口点 VA：`0x%08X`" % (img.image_base + img.entry_rva),
        "- 链接时间戳：`0x%08X`" % img.pe.FILE_HEADER.TimeDateStamp,
        "- 反汇编指令数：%d" % idx.decoded,
        "- 识别函数：%d（entry/call/prologue 播种 %d）" % (
            len(funcs), sum(1 for f in funcs.values() if f["src"] != "gap")),
        "- 调用点：%d" % len([k for k in idx.targets if idx.flags_at(k) & F_CALL]),
        "- 虚函数表候选：%d" % len(vtables),
        "- 字符串：%d" % len(strings),
        "- 可命名函数（依据内嵌 Class::Method 字符串）：%d" % len(names),
        "",
        "镜像路径由 `tools/peimage.py` 的 `resolve_default_image()` 自动定位",
        "（环境变量 `RA2_GAMEMD` 优先）。**跨机器复现认入口点与链接时间戳，",
        "不认路径** —— 本仓库最早那份基线取自 `D:\\westwood\\RA2YR`，现在本机是",
        "`D:\\RA2\\Reunion 2023`，入口点与时间戳一致，是同一个构建；",
        "在那边重跑本脚本，除路径一行外输出逐字节相同。",
        "",
        "## 体积最大的 40 个函数",
        "",
        "| VA | 字节 | 指令数 | 来源 | 命名线索 |",
        "|---|---:|---:|---|---|",
    ]
    for f in big:
        nm = names.get(f["va"], {}).get("guess", "")
        lines.append("| `0x%08X` | %d | %d | %s | %s |" % (
            f["va"], f["size"], f["insns"], f["src"], "`%s`" % nm if nm else "—"))
    with open(os.path.join(outdir, "summary.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    t("已写出 db/summary.md", t0)


if __name__ == "__main__":
    main()
