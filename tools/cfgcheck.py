"""cfgcheck.py -- Stage 2 产物的**独立**对账器。

为什么不复用 cfgscan.py 的代码：那个工具是**生产者**，它对自己的产物做的是
内部自洽检查。生产者自证有个天然盲区 —— 它检查的是"我以为我写了什么"，
而不是"文件里实际是什么"。这里换一条完全独立的路：

  * `db/blocks.json` 从磁盘读回来，按文档口径重新解包，逐块核；
  * `src/re/BlockTable.h` **按文本解析**（正则抓数组字面量），不 include、不编译，
    也不调用生成它的任何函数 —— 直接看文件里到底写了几个数。

然后让两条路互相钉住。任何一方漂移都会在这里报出来。

用法：
    python tools/cfgcheck.py            # 对账，打印结论
    python tools/cfgcheck.py --quiet    # 只在失败时输出
退出码 0 = 全过，1 = 有失败项。
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BLOCKS = os.path.join(ROOT, "db", "blocks.json")
HEADER = os.path.join(ROOT, "src", "re", "BlockTable.h")

EDGE_KINDS = ["fall", "jcc", "jmp", "switch", "tail"]
END_KINDS = ["ret", "jmp", "jcc", "switch", "ind", "int3", "halt", "fall", "tail",
             "bad", "end"]


def parse_header(path):
    """从 BlockTable.h 里按文本抓出各个数组与常量。"""
    src = open(path, encoding="utf-8").read()

    def const(name):
        m = re.search(r"inline constexpr \w+ %s = (\d+);" % re.escape(name), src)
        if not m:
            raise KeyError(name)
        return int(m.group(1))

    def arr(name):
        m = re.search(r"inline constexpr \w+ %s\[\] = \{(.*?)\};" % re.escape(name),
                      src, re.S)
        if not m:
            raise KeyError(name)
        body = m.group(1)
        return [int(x, 0) for x in re.findall(r"0x[0-9A-Fa-f]+|\d+", body)]

    return {
        "count": const("kCfgFuncCount"),
        "block_count": const("kBlockCount"),
        "edge_count": const("kEdgeCount"),
        "switch_edge_count": const("kSwitchEdgeCount"),
        "single_count": const("kSingleFuncCount"),
        "block_sum": const("kCfgBlockSum"),
        "edge_sum": const("kCfgEdgeSum"),
        "case_sum": const("kCfgCaseSum"),
        "va": arr("kCfgFuncVA"),
        "blocks": arr("kCfgBlocks"),
        "edges": arr("kCfgEdges"),
        "cases": arr("kCfgCases"),
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()
    fails = []
    oks = []

    def chk(cond, name, detail=""):
        (oks if cond else fails).append((name, detail))

    raw = json.load(open(BLOCKS, encoding="utf-8"))
    h = parse_header(HEADER)
    stats = raw["stats"]
    single_edges = raw["single_edges"]

    # ---- 0. 编码表必须跟对账器的认知一致 ----
    chk(raw["edge_kinds"] == EDGE_KINDS, "边类型表一致", str(raw["edge_kinds"]))
    chk(raw["end_kinds"] == END_KINDS, "终止形态表一致", str(raw["end_kinds"]))

    funcs = raw["funcs"]
    single = raw["single"]

    # 单块函数的边。口径必须跟 cfgscan 里 `ncase` 完全一致：
    # 只统计**终止形态是 switch 的块**上那些 switch/tail 型边。
    SW_EK = {EDGE_KINDS.index("switch"), EDGE_KINDS.index("tail")}
    single_edge_n = sum(len(v) for v in single_edges.values())
    single_case_edges = sum(len(v) for k, v in single_edges.items()
                            if single.get(k) == "switch"
                            for _t, ek in v if ek in SW_EK)
    # stats 自身的分解也必须闭合（跟 cfgscan 里的判据 10 独立再算一遍）
    chk(stats["blocks"] == stats["blocks_multi"] + len(single),
        "stats.blocks == blocks_multi + 单块函数数",
        "%d vs %d" % (stats["blocks"], stats["blocks_multi"] + len(single)))
    chk(stats["edges"] == stats["edges_multi"] + stats["edges_single"],
        "stats.edges == edges_multi + edges_single",
        "%d vs %d" % (stats["edges"], stats["edges_multi"] + stats["edges_single"]))
    chk(stats["edges_single"] == single_edge_n,
        "stats.edges_single == single_edges 实际条数",
        "%d vs %d" % (stats["edges_single"], single_edge_n))

    # ---- 1. 按文档口径解包 blocks.json，逐块核 ----
    n_blocks = 0
    n_edges = 0
    n_cases = 0
    bad_code = 0
    bad_end = 0
    overlap = 0
    orphan_funcs = 0
    ext_edge_not_tail = 0
    for fkey, rec in funcs.items():
        va = int(fkey, 0)
        blks = rec["b"]
        heads = {b[0] for b in blks}
        # 函数体范围：第一块首址 ~ 最后一块末址（JSON 里没有 size，只能这样取）
        func_end = blks[-1][2]
        n_blocks += len(blks)
        prev_end = None
        for b in blks:
            bva, nins, bend, endk, succ = b
            if nins < 1:
                bad_code += 1
            if endk >= len(END_KINDS):
                bad_end += 1
            if not (va <= bva < bend <= func_end):
                bad_code += 1
            if prev_end is not None and bva < prev_end:
                overlap += 1
            prev_end = bend
            n_edges += len(succ)
            if endk == END_KINDS.index("switch"):
                n_cases += len(succ)
            for t, ek in succ:
                if ek >= len(EDGE_KINDS):
                    bad_code += 1
                    continue
                if va <= t < func_end:
                    # 落在函数体内的目标**必须**是某个块的首址，否则是漏切了 leader
                    if t not in heads:
                        ext_edge_not_tail += 1
                elif ek != EDGE_KINDS.index("tail"):
                    # 落在函数外的目标必须是 tail（尾调用/跳表外项）
                    ext_edge_not_tail += 1
        # 2. 可达性：从第一块出发能不能到所有块
        idx_of = {b[0]: i for i, b in enumerate(blks)}
        seen = {0}
        stack = [0]
        while stack:
            i = stack.pop()
            for t, _ek in blks[i][4]:
                j = idx_of.get(t)
                if j is not None and j not in seen:
                    seen.add(j)
                    stack.append(j)
        if len(seen) != len(blks):
            orphan_funcs += 1

    chk(bad_code == 0, "块内指令数 ≥1、块区间合法、边型下标合法",
        "违规 %d" % bad_code)
    chk(bad_end == 0, "终止形态下标合法", "违规 %d" % bad_end)
    chk(overlap == 0, "同函数内块按地址递增不重叠", "重叠 %d 处" % overlap)
    chk(ext_edge_not_tail == 0,
        "边目标：在体内必须是块首、在体外必须是 tail",
        "违规 %d 条" % ext_edge_not_tail)
    chk(orphan_funcs == 0, "每个块都从入口块可达", "有孤儿块的函数 %d 个" % orphan_funcs)
    chk(n_blocks + len(single) == stats["blocks"],
        "解包出的块数（多块 + 单块）== stats.blocks",
        "%d + %d vs %d" % (n_blocks, len(single), stats["blocks"]))
    chk(n_edges + single_edge_n == stats["edges"],
        "解包出的边数（多块 + 单块）== stats.edges",
        "%d + %d vs %d" % (n_edges, single_edge_n, stats["edges"]))
    chk(n_cases + single_case_edges == stats["switch_edges"],
        "解包出的跳表边数（多块 + 单块）== stats.switch_edges",
        "%d + %d vs %d" % (n_cases, single_case_edges, stats["switch_edges"]))
    chk(len(funcs) == stats["funcs_multi"],
        "多块函数个数 == stats.funcs_multi",
        "%d vs %d" % (len(funcs), stats["funcs_multi"]))
    chk(len(single) == stats["funcs_single"],
        "单块函数个数 == stats.funcs_single",
        "%d vs %d" % (len(single), stats["funcs_single"]))

    # 单块函数的终止形态也必须合法
    bad_single = sum(1 for k in single.values() if k not in END_KINDS)
    chk(bad_single == 0, "单块函数的终止形态合法", "违规 %d" % bad_single)

    # 每个函数的入口不能同时出现在 multi 与 single 里
    common = set(funcs) & set(single)
    chk(not common, "multi 与 single 无交集", "重复 %d 个" % len(common))

    # ---- 3. 头文件自身的内部一致 ----
    chk(h["count"] == len(h["va"]) == len(h["blocks"]) == len(h["edges"])
        == len(h["cases"]),
        "头文件四个数组等长且 == kCfgFuncCount",
        "%d / %d %d %d %d" % (h["count"], len(h["va"]), len(h["blocks"]),
                              len(h["edges"]), len(h["cases"])))
    chk(h["va"] == sorted(h["va"]) and len(set(h["va"])) == len(h["va"]),
        "kCfgFuncVA 升序且无重复", "")
    # 总数常量必须等于 **多块数组之和 + 单块部分** —— 因为数组只覆盖多块函数。
    # 这三个等式就是"头文件 vs JSON"最容易对不上的地方，所以逐个钉。
    chk(h["block_count"] == h["block_sum"] + len(single),
        "kBlockCount == kCfgBlockSum + 单块函数数",
        "%d vs %d" % (h["block_count"], h["block_sum"] + len(single)))
    chk(h["edge_count"] == h["edge_sum"] + sum(len(v) for v in single_edges.values()),
        "kEdgeCount == kCfgEdgeSum + 单块函数出边数",
        "%d vs %d" % (h["edge_count"],
                      h["edge_sum"] + sum(len(v) for v in single_edges.values())))
    chk(h["switch_edge_count"] == h["case_sum"] + single_case_edges,
        "kSwitchEdgeCount == kCfgCaseSum + 单块函数的跳表边数",
        "%d vs %d" % (h["switch_edge_count"], h["case_sum"] + single_case_edges))

    chk(h["block_sum"] == sum(h["blocks"]),
        "kCfgBlockSum == ΣkCfgBlocks", "%d vs %d" % (h["block_sum"], sum(h["blocks"])))
    chk(h["edge_sum"] == sum(h["edges"]),
        "kCfgEdgeSum == ΣkCfgEdges", "%d vs %d" % (h["edge_sum"], sum(h["edges"])))
    chk(h["case_sum"] == sum(h["cases"]),
        "kCfgCaseSum == ΣkCfgCases", "%d vs %d" % (h["case_sum"], sum(h["cases"])))
    chk(h["single_count"] == len(single),
        "kSingleFuncCount == single 条目数",
        "%d vs %d" % (h["single_count"], len(single)))

    # ---- 4. 两条路互相钉住（最关键的一组）----
    chk(h["count"] == len(funcs),
        "头文件函数数 == blocks.json 多块函数数",
        "%d vs %d" % (h["count"], len(funcs)))
    chk(h["block_count"] == n_blocks + len(single),
        "头文件总块数 == 解包出的总块数 + 单块",
        "%d vs %d" % (h["block_count"], n_blocks + len(single)))
    chk(h["edge_count"] == n_edges + single_edge_n,
        "头文件总边数 == 多块解包边数 + 单块边数",
        "%d vs %d" % (h["edge_count"], n_edges + single_edge_n))
    chk(h["switch_edge_count"] == n_cases + single_case_edges,
        "头文件跳表边数 == 多块 + 单块",
        "%d vs %d" % (h["switch_edge_count"], n_cases + single_case_edges))
    # 头文件总数 == JSON 自己的 stats（两条路各算一遍）
    chk(h["block_count"] == stats["blocks"],
        "头文件 kBlockCount == JSON stats.blocks",
        "%d vs %d" % (h["block_count"], stats["blocks"]))
    chk(h["edge_count"] == stats["edges"],
        "头文件 kEdgeCount == JSON stats.edges",
        "%d vs %d" % (h["edge_count"], stats["edges"]))
    chk(h["switch_edge_count"] == stats["switch_edges"],
        "头文件 kSwitchEdgeCount == JSON stats.switch_edges",
        "%d vs %d" % (h["switch_edge_count"], stats["switch_edges"]))

    va_row = dict(zip(h["va"], zip(h["blocks"], h["edges"], h["cases"])))
    mismatch = 0
    for fkey, rec in funcs.items():
        va = int(fkey, 0)
        row = va_row.get(va)
        if row is None:
            mismatch += 1
            continue
        blks = rec["b"]
        want = (len(blks), sum(len(b[4]) for b in blks),
                sum(len(b[4]) for b in blks if b[3] == END_KINDS.index("switch")))
        if row != want:
            mismatch += 1
    chk(mismatch == 0, "逐函数：头文件三元组 == blocks.json 实算",
        "不符 %d 个函数" % mismatch)

    # ---- 5. 计数恒等式（每函数，独立再算一遍）----
    viol_lo = 0
    viol_hi = 0
    for fkey, rec in funcs.items():
        blks = rec["b"]
        b = len(blks)
        e = sum(len(x[4]) for x in blks)
        c = sum(len(x[4]) for x in blks if x[3] == END_KINDS.index("switch"))
        if e < b - 1:
            viol_lo += 1
        if e > 2 * b + c:
            viol_hi += 1
    # 单块函数同样要满足（b = 1，下界退化成 e >= 0）
    for k, edges in single_edges.items():
        b = 1
        e = len(edges)
        c = len(edges) if single.get(k) == "switch" else 0
        if e < b - 1:
            viol_lo += 1
        if e > 2 * b + c:
            viol_hi += 1
    chk(viol_lo == 0, "每函数 b-1 <= e", "违规 %d" % viol_lo)
    chk(viol_hi == 0, "每函数 e <= 2b+c", "违规 %d" % viol_hi)

    # ---- 6. 覆盖：S1 的每个函数都要有交代（块数 ≥1）----
    fun = json.load(open(os.path.join(ROOT, "db", "funcs.json"), encoding="utf-8"))
    fjs = fun["funcs"]
    missing = 0
    for k in fjs:
        if k not in funcs and k not in single:
            missing += 1
    chk(missing == 0, "S1 的每个函数都出现在 multi 或 single 里",
        "缺失 %d 个" % missing)
    chk(len(funcs) + len(single) == len(fjs),
        "multi + single == S1 函数数",
        "%d + %d vs %d" % (len(funcs), len(single), len(fjs)))
    stray = set(single_edges) - set(single)
    chk(not stray, "single_edges 的键都在 single 里", "游离 %d 个" % len(stray))
    chk(set(funcs) | set(single) == set(fjs),
        "multi ∪ single 与 S1 函数表逐键相等",
        "差 %d 个" % len((set(funcs) | set(single)) ^ set(fjs)))

    if not args.quiet or fails:
        print("=== Stage 2 独立对账（tools/cfgcheck.py）===")
        print("通过 %d 项" % len(oks))
        for n, d in oks:
            print("  OK  %s%s" % (n, ("  [%s]" % d) if d else ""))
        if fails:
            print("失败 %d 项：" % len(fails))
            for n, d in fails:
                print("  XX  %s  [%s]" % (n, d))
    return 1 if fails else 0


if __name__ == "__main__":
    raise SystemExit(main())
