"""
palramp.py -- 验证 VXL 调色板是不是"每 16 个色一条明暗渐变"。

动机：从 gamemd.exe 反汇编出来的明暗 LUT 是

    LUT[i] = dot(N_i, L) >= 0 ? (int)(dot * 16.0) : 0     ; 0..16，共 17 级

那个 *16 很可疑 —— 如果只是"亮度缩放"，乘 255 或 31 更自然。乘 16 强烈暗示
**调色板本身是按 16 个色一组排的明暗渐变**，LUT 值是"组内的第几级"。

判据（可证伪）：
  * 若成立：从 32 号开始，每 16 个连续索引的亮度应当单调（升或降），
    且同一组内的色相基本一致（只是明暗不同）；
  * 若不成立：亮度序列杂乱，或者周期性出现在 8 / 32 而不是 16。

用法：
  python tools/palramp.py                # 默认看几个典型单位
  python tools/palramp.py --n 12         # 多看几个
"""

from __future__ import annotations

import argparse
import os
import struct
import sys

sys.path.insert(0, r"E:\ra2source\tools")
import mixdump

MIXES = [r"D:\westwood\RA2YR\ra2.mix", r"D:\westwood\RA2YR\ra2md.mix"]
DEFAULT_UNITS = ["MTNK", "HTNK", "JEEP", "3DDK", "APC", "HARV", "FVPWR", "GTNK",
                 "MTNK", "SHAD", "TNKD", "CMIN", "ZEP", "KIROV", "DRED"]


def build_index():
    idx = {}
    mixes = []
    for p in MIXES:
        if not os.path.exists(p):
            continue
        m = mixdump.MixFile(p)
        mixes.append(m)
        for owner, h, off, size, depth in mixdump.iter_leaves(m):
            if h not in idx:
                idx[h] = (owner, off, size)
    return idx, mixes


def read_vxl_palette(idx, image):
    c = mixdump.westwood_crc("%s.VXL" % image)
    hit = idx.get(c)
    if not hit:
        return None
    m, off, size = hit
    data = m.read(off, size)
    if not data:
        return None
    if data[:15] != b"Voxel Animation":
        return None
    return data[34:34 + 768]


def lum(r, g, b):
    return 0.299 * r + 0.587 * g + 0.114 * b


def analyze(name, pal):
    cols = [pal[i * 3:i * 3 + 3] for i in range(256)]
    L = [lum(*c) for c in cols]
    print("\n==== %s ====" % name)
    # 只看 32..255
    body = L[32:]
    # 测周期：对每个候选周期 p，算"组内单调递增/递减"的比例
    best = []
    for p in (8, 16, 32, 64):
        ok = tot = 0
        for g0 in range(0, len(body) - p + 1, p):
            grp = body[g0:g0 + p]
            tot += 1
            d = [grp[i + 1] - grp[i] for i in range(p - 1)]
            if all(x >= -0.5 for x in d) or all(x <= 0.5 for x in d):
                ok += 1
        best.append((p, ok, tot))
    print("  周期内单调性：%s" % ", ".join("p=%d %d/%d" % b for b in best))
    p = 16
    print("  32..255 每 16 个一组：")
    for g in range(0, 224, 16):
        grp = body[g:g + 16]
        first = cols[32 + g]
        last = cols[32 + g + 15]
        print("    %3d-%3d  L %6.1f -> %6.1f  首色(%3d,%3d,%3d) 末色(%3d,%3d,%3d)  %s"
              % (32 + g, 32 + g + 15, grp[0], grp[-1],
                 first[0], first[1], first[2], last[0], last[1], last[2],
                 "升" if grp[-1] > grp[0] else "降"))


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=4)
    ap.add_argument("units", nargs="*")
    a = ap.parse_args()
    idx, _ = build_index()
    print("[.] MIX 叶子条目：%d" % len(idx))
    names = a.units or DEFAULT_UNITS[: a.n]
    done = set()
    for u in names:
        if u in done:
            continue
        done.add(u)
        pal = read_vxl_palette(idx, u)
        if pal is None:
            print("[-] %s: 找不到 VXL" % u)
            continue
        analyze(u, pal)


if __name__ == "__main__":
    main()
