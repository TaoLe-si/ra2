"""
lightprobe.py -- 在 gamemd.exe 里定位体素光照用的光向量。

思路（不猜常数，只跟引用）：
  1. 已知法线表指针数组在 VA 0x004469E4（见 tools/normaldump.py）。
  2. 在 .text 里搜这个立即数，找到**加载它的那条指令**所在函数。
  3. 从该函数附近扫出所有"绝对地址取浮点数"的指令（movss / fld / mov r32,[imm32]），
     把被引用的地址内容打成 float 三元组看。

为什么值得做：体素明暗是 RA2 画面辨识度最高的一层（顶面亮、侧面中、底面暗）。
光向量猜错，整个"光影复刻"就是自嗨。

用法：
  python tools/lightprobe.py
  python tools/lightprobe.py --exe D:\\westwood\\RA2\\game.exe --ptr 0x004469E4
"""

from __future__ import annotations

import argparse
import struct
import sys

sys.path.insert(0, r"E:\ra2source\tools")
from peimage import PEImage

# 绝对地址取操作数用的指令前缀（x86，32 位绝对寻址）
PATTERNS = [
    (b"\xf3\x0f\x10\x05", "movss xmm0,[imm32]"),
    (b"\xf3\x0f\x10\x0d", "movss xmm1,[imm32]"),
    (b"\xf3\x0f\x10\x15", "movss xmm2,[imm32]"),
    (b"\xf3\x0f\x10\x1d", "movss xmm3,[imm32]"),
    (b"\xd9\x05", "fld dword[imm32]"),
    (b"\xa1", "mov eax,[imm32]"),
    (b"\x8b\x0d", "mov ecx,[imm32]"),
    (b"\x8b\x15", "mov edx,[imm32]"),
    (b"\x8b\x35", "mov esi,[imm32]"),
    (b"\x8b\x3d", "mov edi,[imm32]"),
]


def find_hits(data: bytes, needle: bytes):
    out = []
    i = data.find(needle)
    while i >= 0:
        out.append(i)
        i = data.find(needle, i + 1)
    return out


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default=r"D:\westwood\RA2YR\gamemd.exe")
    ap.add_argument("--ptr", default="0x004469E4")
    ap.add_argument("--max", type=int, default=6, help="最多看几个引用点")
    a = ap.parse_args()

    pe = PEImage(a.exe)
    trva, trva_end = pe.text_range()
    tdata = pe.read(trva, trva_end - trva)
    if tdata is None:
        sys.exit("[x] 读不出 .text")
    tva = pe.va(trva)

    ptrs = [int(x, 0) for x in a.ptr.split(",")]
    hits = []
    for p in ptrs:
        for i in find_hits(tdata, struct.pack("<I", p)):
            hits.append((i, p))
    hits.sort()
    print("[.] %s" % a.exe)
    print("[.] 搜指针立即数 %s：%d 处"
          % (", ".join("%#010x" % p for p in ptrs), len(hits)))
    if not hits:
        return

    for h, ptr in hits[: a.max]:
        print("[.]   -> 命中 %#010x 于 VA %#010x" % (ptr, tva + h))
        va = tva + h
        start = max(0, h - 0x400)
        lo = tdata.rfind(b"\x55\x8b\xec", start, h)
        fstart = lo if lo >= 0 else start
        fend = min(len(tdata), h + 0x800)
        print("\n==== 引用点 VA=%#010x  函数 [%#010x .. %#010x]（%d 字节）===="
              % (va, tva + fstart, tva + fend, fend - fstart))
        seen = set()
        for pat, name in PATTERNS:
            off = fstart
            while True:
                i = tdata.find(pat, off, fend)
                if i < 0:
                    break
                off = i + 1
                imm_off = i + len(pat)
                if imm_off + 4 > fend:
                    break
                (imm,) = struct.unpack_from("<I", tdata, imm_off)
                if imm < pe.image_base or imm > pe.image_base + 0x00200000:
                    continue
                if imm in seen:
                    continue
                seen.add(imm)
                vals = pe.read(imm - pe.image_base, 16)
                if not vals or len(vals) < 12:
                    continue
                f = struct.unpack_from("<3f", vals, 0)
                m = max(abs(x) for x in f)
                if m > 1.2 or m < 0.2:
                    continue
                print("  %#010x  %-20s -> %#010x  %+.6f %+.6f %+.6f   |v|=%.6f  [%s]"
                      % (tva + i, name, imm, f[0], f[1], f[2],
                         (f[0] * f[0] + f[1] * f[1] + f[2] * f[2]) ** 0.5,
                         pe.section_of(imm - pe.image_base)))


if __name__ == "__main__":
    main()
