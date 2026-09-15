"""
lightprobe4.py -- 反汇编"取法线表"那段，把体素着色的算式读出来。

已定位（tools/lightprobe2.py）：
    VA 0x00758670: mov eax, [0x008469E0 + edx*4]   ; edx = VXL 头的 NormalsType
这就是"按 NormalsType 选法线表"。本脚本把 0x00758670 所在的整段 x87 浮点代码
反汇编出来，配合 .rdata 常量还原光照公式。

用法：
  python tools/lightprobe4.py [--va 0x00758670] [--before 0x120] [--after 0x220]
"""

from __future__ import annotations

import argparse
import struct
import sys

sys.path.insert(0, r"E:\ra2source\tools")
sys.path.insert(0, r"E:\ra2source\tools\pylibs")

from peimage import PEImage
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

# .rdata 里已经确认过的常量（tools/lightprobe3.py 扫出来的）
KNOWN = {
    0x007f6960: 16.0,
    0x007f6964: 0.2,
    0x007f6968: 0.6,
    0x007f696c: 0.8,
    0x007ef710: 0.4,
    0x007ef714: 0.6,
    0x007ef718: 0.0625,
}


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default=r"D:\westwood\RA2YR\gamemd.exe")
    ap.add_argument("--va", default="0x00758670")
    ap.add_argument("--before", type=int, default=0x140)
    ap.add_argument("--after", type=int, default=0x240)
    a = ap.parse_args()

    pe = PEImage(a.exe)
    va = int(a.va, 0)
    start = va - a.before
    n = a.before + a.after
    rva = start - pe.image_base
    code = pe.read(rva, n)
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True
    for ins in md.disasm(code, start):
        txt = "%-8s %s" % (ins.mnemonic, ins.op_str)
        note = ""
        for op in ins.operands:
            if op.type == 3:  # IMM
                v = op.imm
                if v in KNOWN:
                    note += "   ; = %g" % KNOWN[v]
                elif v == 0:
                    pass
        # 内存操作数里的绝对地址
        if ins.op_str.startswith("dword ptr [0x"):
            try:
                addr = int(ins.op_str.split("[0x")[1].split("]")[0], 16)
                if addr in KNOWN:
                    note += "   ; [0x%08x] = %g" % (addr, KNOWN[addr])
            except Exception:
                pass
        mark = "  <<<" if ins.address == va else ""
        print("%08x: %-30s %-24s%s%s" % (ins.address, ins.bytes.hex(), txt, note, mark))


if __name__ == "__main__":
    main()
