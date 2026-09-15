"""
findimm.py -- 在 gamemd.exe 的 .text 里搜"带某个立即数的指令"。

为什么需要它：逆向格式时最快的路是找**长度校验**。比如已知
`size - BodySize == 802 + 120*NumLimbs`，那 802(0x322) 和 120(0x78)
几乎一定作为立即数出现在加载器里。比起从字符串往回追调用链，
直接搜常量一步到位。

用法：
  python tools/findimm.py 0x322 0x78
  python tools/findimm.py 0x322 --ctx 8        # 多打几条上下文
  python tools/findimm.py 0x322 --mnmx         # 同时要求匹配到多个立即数
"""

from __future__ import annotations

import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from peimage import PEImage  # noqa: E402

try:
    from capstone import Cs, CS_ARCH_X86, CS_MODE_32
    from capstone.x86 import X86_OP_IMM
except ImportError:  # pragma: no cover
    raise SystemExit("需要 capstone：请确认 tools/pylibs 下有 capstone 包")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("imms", nargs="+", help="要搜的立即数（可写 0x322 或 120）")
    ap.add_argument("--ctx", type=int, default=4, help="每条命中前后各打几条")
    ap.add_argument("--limit", type=int, default=60, help="最多打多少条命中")
    a = ap.parse_args()
    wants = set()
    for s in a.imms:
        wants.add(int(s, 0) & 0xFFFFFFFF)

    pe = PEImage()
    # text_range() 返回的是 RVA 区间（实测 (0x1000, 0x3E04CD)）。
    rva0, rva1 = pe.text_range()
    data = pe.data[pe.rva_to_off(rva0):pe.rva_to_off(rva1)]
    base = pe.image_base + rva0

    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True
    insns = list(md.disasm(data, base))
    print("[i] .text 反汇编 %d 条指令，@0x%08X" % (len(insns), base))

    # 收集命中下标
    hits = []
    for i, ins in enumerate(insns):
        for op in ins.operands:
            if op.type == X86_OP_IMM and (op.imm & 0xFFFFFFFF) in wants:
                hits.append((i, ins, op.imm & 0xFFFFFFFF))
                break
    print("[i] 命中 %d 条" % len(hits))

    # 命中密集的地方最可能是加载器：把命中地址聚成簇，按簇内命中数排序
    clusters = []
    for i, ins, v in hits:
        if clusters and ins.address - clusters[-1][-1][1].address < 0x200:
            clusters[-1].append((i, ins, v))
        else:
            clusters.append([(i, ins, v)])
    clusters.sort(key=lambda c: -len(c))
    print("[i] 聚成 %d 簇，按命中密度排序：\n" % len(clusters))
    shown = 0
    for c in clusters:
        if shown >= a.limit:
            break
        first, last = c[0][1].address, c[-1][1].address
        print("=== 簇 @0x%08X..0x%08X  命中 %d 个立即数: %s"
              % (first, last, len(c), " ".join("0x%X" % v for _, _, v in c[:8])))
        lo = max(0, c[0][0] - a.ctx)
        hi = min(len(insns), c[-1][0] + a.ctx + 1)
        for j in range(lo, hi):
            ins = insns[j]
            mark = "  >>" if any(j == k for k, _, _ in c) else "    "
            print("%s 0x%08X  %-8s %s" % (mark, ins.address, ins.mnemonic, ins.op_str))
        print()
        shown += 1


if __name__ == "__main__":
    main()
