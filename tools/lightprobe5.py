"""
lightprobe5.py -- 找"生成明暗 LUT"函数的调用者，把光向量挖出来。

tools/lightprobe4.py 已经把函数读出来了（x87 反汇编）：

  函数A VA 0x00758670（简单版，ecx=光向量, edx=NormalsType）：
      for i in 0..count-1:
          d = dot(normals[type][i], L)          ; fld/fmul/fadd 三个分量
          lut[i] = (d >= 0) ? (int)(d * 16.0)   ; fmul [0x7f6960]=16 -> call ftol
                            : 0
      写到全局字节数组 0x00B45990（256 项），最后 3 项 (253/254/255) 写死 16。

  函数B VA 0x007586F0（双光源版，多了 normalize(A+B) 和一项 [esp+0x38] 系数）。

所以"明暗分级"就是 **0..16 共 17 级**（*16 而不是 *31/*255），这一点直接来自
二进制，不是猜的。剩下的问题只有一个：**L 到底是多少**。

本脚本：
  1. 扫 .text 找所有 call 0x758670 / 0x7586F0 / 0x758880 的位置；
  2. 把每个调用点前面 ~0x120 字节反汇编出来，看 ecx/edx 是怎么来的；
  3. 顺带把 0x00B45990 附近（明暗 LUT 全局区）打印出来。

用法：
  python tools/lightprobe5.py
"""

from __future__ import annotations

import argparse
import struct
import sys

sys.path.insert(0, r"E:\ra2source\tools")
sys.path.insert(0, r"E:\ra2source\tools\pylibs")

from peimage import PEImage
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

TARGETS = {
    0x00758670: "A: LUT=dot(N,L)*16",
    0x007586F0: "B: 双光源版",
    0x00758880: "C: 第三个",
}


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default=r"D:\westwood\RA2YR\gamemd.exe")
    ap.add_argument("--before", type=int, default=0x120)
    a = ap.parse_args()

    pe = PEImage(a.exe)
    trva, tend = pe.text_range()
    tdata = pe.read(trva, tend - trva)
    tva = pe.va(trva)

    # call rel32 的编码：E8 <rel32>，目标 = 下一条指令地址 + rel32
    hits = []
    for i in range(len(tdata) - 5):
        if tdata[i] != 0xE8:
            continue
        (rel,) = struct.unpack_from("<i", tdata, i + 1)
        dst = (tva + i + 5 + rel) & 0xFFFFFFFF
        if dst in TARGETS:
            hits.append((tva + i, dst))
    print("[.] call 命中：%d 处" % len(hits))
    for src, dst in hits:
        print("   %#010x -> %#010x  (%s)" % (src, dst, TARGETS[dst]))

    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True
    for src, dst in hits[:4]:
        start = src - a.before
        rva = start - pe.image_base
        code = pe.read(rva, a.before + 0x40)
        print("\n==== 调用点 %#010x 的上文 ====" % src)
        for ins in md.disasm(code, start):
            mark = "  <<< call" if ins.address == src else ""
            extra = ""
            if ins.op_str.startswith("dword ptr [0x"):
                try:
                    addr = int(ins.op_str.split("[0x")[1].split("]")[0], 16)
                    b = pe.read(addr - pe.image_base, 12)
                    if b and len(b) == 12:
                        f = struct.unpack_from("<3f", b, 0)
                        extra = "   ; [%#x] float3 = %+.6f %+.6f %+.6f (|v|=%.6f)" % (
                            addr, f[0], f[1], f[2],
                            (f[0] * f[0] + f[1] * f[1] + f[2] * f[2]) ** 0.5)
                except Exception:
                    pass
            print("%08x: %-28s %-8s %s%s%s" % (ins.address, ins.bytes.hex(),
                                                ins.mnemonic, ins.op_str, extra, mark))


if __name__ == "__main__":
    main()
