"""
pcxhash.py -- 用参考实现算出全部 PCX 的 RGBA 哈希表，供 C++ 端对账。

和 ra2core.exe --pcxhash 的输出逐行 diff，为空才算 C++ 解码器正确。
哈希用 FNV-1a（逐字节），喂进去的顺序：
    u32 宽、u32 高、u32 平面数（各按小端拆成 4 字节）
    然后 Width*Height*4 字节的 RGBA（R,G,B,A 逐字节，8 位模式走内嵌调色板）

这个顺序必须和 src/main.cpp 的 Hash_Pcx 完全一致 —— 它俩就是同一份规格的
两种实现，任何一格解码差异都会让哈希对不上。

用法：
  python tools/pcxhash.py D:\\westwood\\RA2YR\\ra2.mix > build/pcx_cpp.txt
  ra2core --pcxhash D:\\westwood\\RA2YR\\ra2.mix > build/pcx_py.txt
  两种输出的非 # 行直接 diff
"""

from __future__ import annotations

import struct
import sys

sys.path.insert(0, r"E:\ra2source\tools")
import mixdump  # noqa: E402
import pcxdec   # noqa: E402

FNV_OFFSET = 2166136261
FNV_PRIME = 16777619


def fnv_bytes(h: int, data: bytes) -> int:
    for b in data:
        h = ((h ^ b) * FNV_PRIME) & 0xFFFFFFFF
    return h


def run(path: str, max_depth: int = 4) -> int:
    m = mixdump.MixFile(path)
    print("# mix=%s" % path)
    seq = ok = bad = 0
    for owner, h, off, size, depth in mixdump.iter_leaves(m, max_depth=max_depth):
        head = owner.head(off, 2)
        if head[:2] != b"\x0a\x05":
            continue
        data = owner.read(off, size)
        try:
            p = pcxdec.Pcx(data)
        except pcxdec.PcxError:
            print("BAD %d 0x%08X %d" % (seq, h, size))
            seq += 1
            bad += 1
            continue
        rgba = p.to_rgba()
        acc = FNV_OFFSET
        acc = fnv_bytes(acc, struct.pack("<III", p.w, p.h, p.nplanes))
        acc = fnv_bytes(acc, rgba)
        print("%d 0x%08X %dx%d p%d %d 0x%08X" % (seq, h, p.w, p.h, p.nplanes, len(rgba), acc))
        seq += 1
        ok += 1
    print("# 共 %d 个 PCX（解码失败 %d）" % (ok, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    d = int(sys.argv[2]) if len(sys.argv) > 2 else 4
    sys.exit(run(sys.argv[1], d))
