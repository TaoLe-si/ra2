"""
lightprobe3.py -- 已定位到"取法线表"的那条指令（VA 0x00758670，
mov eax,[0x008469E0 + edx*4]，edx 即 VXL 头的 NormalsType）。本脚本把
该函数附近引用的**所有浮点常量**列出来，从中挑出光向量 / 环境光 / 分级数。

判据（不靠眼缘）：
  * 光向量：3 个连续 float，模长在 0.5..2 之间，且 z 分量绝对值最大（光从上方来）；
  * 环境光 / 亮度系数：单个 float，量级 0..1；
  * 分级数：形如 31/32/63/255 的 float 或整数常量。

用法：
  python tools/lightprobe3.py [--va 0x00758670] [--win 0x400]
"""

from __future__ import annotations

import argparse
import struct
import sys

sys.path.insert(0, r"E:\ra2source\tools")
from peimage import PEImage

PATTERNS = [
    (b"\xd9\x05", "fld"),
    (b"\xd8\x0d", "fmul"),
    (b"\xd8\x05", "fadd"),
    (b"\xd8\x25", "fsub"),
    (b"\xd8\x35", "fdiv"),
    (b"\xda\x0d", "fimul int32?"),
    (b"\xf3\x0f\x10\x05", "movss xmm0"),
    (b"\xf3\x0f\x10\x0d", "movss xmm1"),
    (b"\xf3\x0f\x10\x15", "movss xmm2"),
    (b"\xf3\x0f\x10\x1d", "movss xmm3"),
    (b"\xa1", "mov eax"),
    (b"\x8b\x0d", "mov ecx"),
    (b"\x8b\x15", "mov edx"),
]


def uniq(seq):
    seen = set()
    out = []
    for x in seq:
        if x not in seen:
            seen.add(x)
            out.append(x)
    return out


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default=r"D:\westwood\RA2YR\gamemd.exe")
    ap.add_argument("--va", default="0x00758670")
    ap.add_argument("--win", type=int, default=0x600)
    a = ap.parse_args()

    pe = PEImage(a.exe)
    trva, tend = pe.text_range()
    tdata = pe.read(trva, tend - trva)
    tva = pe.va(trva)
    va = int(a.va, 0)
    off = va - tva
    lo = max(0, off - a.win)
    hi = min(len(tdata), off + a.win)
    print("[.] 函数窗口 VA [%#010x .. %#010x]，%d 字节" % (tva + lo, tva + hi, hi - lo))

    for pat, name in PATTERNS:
        i = lo
        while True:
            j = tdata.find(pat, i, hi)
            if j < 0:
                break
            i = j + 1
            p = j + len(pat)
            if p + 4 > hi:
                break
            (imm,) = struct.unpack_from("<I", tdata, p)
            if not (pe.image_base <= imm < pe.image_base + 0x00400000):
                continue
            rva = imm - pe.image_base
            sec = pe.section_of(rva)
            vals = pe.read(rva, 12)
            if not vals or len(vals) < 12:
                continue
            f = struct.unpack_from("<3f", vals, 0)
            u = struct.unpack_from("<3I", vals, 0)
            print("  %#010x  %-12s -> %#010x [%s]  float: %+.6f %+.6f %+.6f"
                  "   int: %d %d %d"
                  % (tva + j, name, imm, sec, f[0], f[1], f[2], u[0], u[1], u[2]))


if __name__ == "__main__":
    main()
