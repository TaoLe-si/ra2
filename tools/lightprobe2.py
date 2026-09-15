"""
lightprobe2.py -- 换个思路找光向量：先看 .text 到底引用了 normals 附近的哪些 .data 地址。

lightprobe.py 的直接结论是"法线表指针数组的 VA 在整个文件里只出现 1 次（就是它自己）"，
说明代码不是用绝对地址取法线表的 —— 那必定有个全局变量在装着它，或者 VoxLib 在
初始化时把指针算出来存到别处。

所以本脚本：
  1. 扫 .text 里所有"32 位绝对地址操作数"，统计它们落在 normals 附近（±4KB）的引用；
  2. 把这些被引用的 .data 槽位内容打出来（是 VA 就再追一层）；
  3. 顺带把 .text 里 movss/fld 引用的所有浮点常量列出来，找像"光向量/环境光"的。

用法：
  python tools/lightprobe2.py [--exe ...] [--near 0x00846A08]
"""

from __future__ import annotations

import argparse
import struct
import sys

sys.path.insert(0, r"E:\ra2source\tools")
from peimage import PEImage

# 32 位绝对寻址的指令前缀（后面紧跟 imm32）
ABS_PREFIX = {
    4: None,  # 占位
}
PATTERNS = [
    (b"\xa1", "mov eax,[i]"),
    (b"\x8b\x0d", "mov ecx,[i]"),
    (b"\x8b\x15", "mov edx,[i]"),
    (b"\x8b\x1d", "mov ebx,[i]"),
    (b"\x8b\x35", "mov esi,[i]"),
    (b"\x8b\x3d", "mov edi,[i]"),
    (b"\x8d\x05", "lea eax,[i]"),
    (b"\x8d\x0d", "lea ecx,[i]"),
    (b"\x8d\x15", "lea edx,[i]"),
    (b"\x8d\x1d", "lea ebx,[i]"),
    (b"\x8d\x35", "lea esi,[i]"),
    (b"\x8d\x3d", "lea edi,[i]"),
    (b"\x8b\x04\x85", "mov eax,[i+eax*4]"),
    (b"\x8b\x04\x8d", "mov eax,[i+ecx*4]"),
    (b"\x8b\x04\x95", "mov eax,[i+edx*4]"),
    (b"\x8b\x04\x9d", "mov eax,[i+ebx*4]"),
    (b"\x8b\x04\xb5", "mov eax,[i+esi*4]"),
    (b"\x8b\x04\xbd", "mov eax,[i+edi*4]"),
    (b"\x8d\x04\x85", "lea eax,[i+eax*4]"),
    (b"\x8d\x04\x8d", "lea eax,[i+ecx*4]"),
    (b"\xf3\x0f\x10\x05", "movss xmm0,[i]"),
    (b"\xf3\x0f\x10\x0d", "movss xmm1,[i]"),
    (b"\xf3\x0f\x10\x15", "movss xmm2,[i]"),
    (b"\xf3\x0f\x10\x1d", "movss xmm3,[i]"),
    (b"\xd9\x05", "fld [i]"),
    (b"\xd8\x0d", "fmul [i]"),
    (b"\xd8\x05", "fadd [i]"),
]


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default=r"D:\westwood\RA2YR\gamemd.exe")
    ap.add_argument("--near", default="0x00846A08")
    ap.add_argument("--win", type=int, default=0x2000)
    a = ap.parse_args()

    pe = PEImage(a.exe)
    trva, tend = pe.text_range()
    tdata = pe.read(trva, tend - trva)
    tva = pe.va(trva)
    near = int(a.near, 0)
    lo, hi = near - a.win, near + a.win

    refs = {}
    for pat, name in PATTERNS:
        off = 0
        while True:
            i = tdata.find(pat, off)
            if i < 0:
                break
            off = i + 1
            p = i + len(pat)
            if p + 4 > len(tdata):
                break
            (imm,) = struct.unpack_from("<I", tdata, p)
            if lo <= imm <= hi:
                refs.setdefault(imm, []).append((tva + i, name))
    print("[.] .text 引用 [%#010x .. %#010x] 的地址：%d 个" % (lo, hi, len(refs)))
    for imm in sorted(refs):
        val = pe.read(imm - pe.image_base, 4)
        v = struct.unpack("<I", val)[0] if val and len(val) == 4 else None
        f = struct.unpack("<f", val)[0] if val and len(val) == 4 else None
        names = ", ".join(n for _, n in refs[imm][:3])
        print("  %#010x  内容=%#010x (float %+.6f)  [%s]  被引用 %d 次 (%s)  sec=%s"
              % (imm, v or 0, f or 0.0, pe.section_of(imm - pe.image_base),
                 len(refs[imm]), names, pe.section_of(imm - pe.image_base)))
        for va, nm in refs[imm][:6]:
            print("        %#010x  %s" % (va, nm))


if __name__ == "__main__":
    main()
