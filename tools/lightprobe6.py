"""
lightprobe6.py -- 通用"找调用者 + 反汇编上文"工具，用来一路追光向量的来源。

链路已经追到：
  0x00753C80  ──call──> 0x00758670（生成明暗 LUT: dot(N,L)*16）
  0x00753C80 里 L = 0x5AF4D0(旋转矩阵, 某个输入向量) 的结果
本脚本递归往上追，看那个"输入向量"最后是常量还是全局变量。

用法：
  python tools/lightprobe6.py --target 0x00753C80
  python tools/lightprobe6.py --target 0x00753C80 --depth 2
"""

from __future__ import annotations

import argparse
import struct
import sys

sys.path.insert(0, r"E:\ra2source\tools")
sys.path.insert(0, r"E:\ra2source\tools\pylibs")

from peimage import PEImage
from capstone import Cs, CS_ARCH_X86, CS_MODE_32


def find_callers(tdata, tva, target):
    out = []
    for i in range(len(tdata) - 5):
        if tdata[i] != 0xE8:
            continue
        (rel,) = struct.unpack_from("<i", tdata, i + 1)
        dst = (tva + i + 5 + rel) & 0xFFFFFFFF
        if dst == target:
            out.append(tva + i)
    return out


def disas(pe, md, va, before, after):
    start = va - before
    code = pe.read(start - pe.image_base, before + after)
    if not code:
        return
    for ins in md.disasm(code, start):
        extra = ""
        if "0x" in ins.op_str and ("ptr [0x" in ins.op_str):
            try:
                addr = int(ins.op_str.split("[0x")[1].split("]")[0].split()[0], 16)
                b = pe.read(addr - pe.image_base, 12)
                if b and len(b) == 12:
                    f = struct.unpack_from("<3f", b, 0)
                    extra = "   ; [%#x] = %+.6f %+.6f %+.6f" % (addr, f[0], f[1], f[2])
            except Exception:
                pass
        mark = "  <<<" if ins.address == va else ""
        print("%08x: %-26s %-8s %s%s%s" % (ins.address, ins.bytes.hex(), ins.mnemonic,
                                           ins.op_str, extra, mark))


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default=r"D:\westwood\RA2YR\gamemd.exe")
    ap.add_argument("--target", default="0x00753C80")
    ap.add_argument("--before", type=int, default=0xA0)
    ap.add_argument("--after", type=int, default=0x20)
    ap.add_argument("--depth", type=int, default=1)
    a = ap.parse_args()

    pe = PEImage(a.exe)
    trva, tend = pe.text_range()
    tdata = pe.read(trva, tend - trva)
    tva = pe.va(trva)
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True

    queue = [(int(a.target, 0), 0)]
    seen = set()
    while queue:
        tgt, d = queue.pop(0)
        if tgt in seen:
            continue
        seen.add(tgt)
        callers = find_callers(tdata, tva, tgt)
        print("\n########## %#010x 的调用者：%d 处（深度 %d）##########"
              % (tgt, len(callers), d))
        for c in callers:
            print("  %#010x" % c)
        for c in callers[:3]:
            print("\n---- %#010x 上文 ----" % c)
            disas(pe, md, c, a.before, a.after)
        if d < a.depth:
            for c in callers[:3]:
                queue.append((c, d + 1))


if __name__ == "__main__":
    main()
