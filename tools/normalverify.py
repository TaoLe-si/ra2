"""
normalverify.py -- 用"外表面法线必须朝外"验证法线表对不对。

看图判断"明暗对不对"太主观，这里用硬判据：

    每个 (x,y) 列里 **z 最大的体素** 是这一列的顶面，它的法线必须朝上（n.z > 0）；
    **z 最小的体素** 是底面，法线必须朝下（n.z < 0）。

表不是一张，是 **4 张按精度递增的 LOD 表**，由一个 VA 指针数组索引
（gamemd.exe off=0x004469E4；槽位 = NormalsType - 1）：

    槽位[0] rva=0x00446A08   16 项
    槽位[1] rva=0x00446AC8   36 项   <- NormalsType=2 用（JEEP 实测 normal 0..35）
    槽位[2] rva=0x00446C78   64 项
    槽位[3] rva=0x00446F78  245 项   <- NormalsType=4 用（GTNK 实测 normal 0..244）

用法：
  python tools/normalverify.py [0xID ...]
"""

from __future__ import annotations

import struct
import sys

sys.path.insert(0, r"E:\ra2source\tools")
from mixdump import MixFile, iter_leaves          # noqa: E402
from vxlstruct import VxlFile                     # noqa: E402

MIXES = [r"D:\westwood\RA2YR\ra2.mix", r"D:\westwood\RA2YR\ra2md.mix"]
EXE = r"D:\westwood\RA2YR\gamemd.exe"

# 槽位起点 rva（.data 里 rva == 文件偏移）与项数
SLOTS = [
    (0x00446A08, 16),
    (0x00446AC8, 36),
    (0x00446C78, 64),
    (0x00446F78, 245),
]

SAMPLES = [0xAE458B95,   # GTNK 灰熊，type=4
           0x8C848DEE,   # ZEP 基洛夫，type=4
           0x18EEDFED,   # YTNKTUR 加特林炮塔，type=4
           0x891E5F6E]   # JEEP，type=2


def load_tables():
    d = open(EXE, "rb").read()
    out = []
    for rva, n in SLOTS:
        out.append([struct.unpack_from("<3f", d, rva + i * 12) for i in range(n)])
    return out


def find(keep, hid):
    for p in MIXES:
        m = MixFile(p)
        keep.append(m)
        for owner, h, off, size, depth in iter_leaves(m):
            if h == hid:
                return owner.read(off, size)
    return None


def score(v, li, normals):
    vox = list(v.voxels(li))
    if not vox:
        return None
    cols = {}
    for x, y, z, _c, n in vox:
        cols.setdefault((x, y), []).append((z, n))
    st = {"top": [0, 0], "bottom": [0, 0],
          "+X": [0, 0], "-X": [0, 0], "+Y": [0, 0], "-Y": [0, 0]}
    N = len(normals)
    for _k, items in cols.items():
        if len(items) < 2:
            continue
        items.sort()
        _zlo, nlo = items[0]
        _zhi, nhi = items[-1]
        if nhi < N:
            st["top"][1] += 1
            if normals[nhi][2] > 0:
                st["top"][0] += 1
        if nlo < N:
            st["bottom"][1] += 1
            if normals[nlo][2] < 0:
                st["bottom"][0] += 1
    xs = [x for x, _y, _z, _c, _n in vox]
    ys = [y for _x, y, _z, _c, _n in vox]
    xmin, xmax, ymin, ymax = min(xs), max(xs), min(ys), max(ys)
    for x, y, _z, _c, n in vox:
        if n >= N:
            continue
        nx, ny, _nz = normals[n]
        if x == xmax:
            st["+X"][1] += 1
            st["+X"][0] += 1 if nx > 0 else 0
        if x == xmin:
            st["-X"][1] += 1
            st["-X"][0] += 1 if nx < 0 else 0
        if y == ymax:
            st["+Y"][1] += 1
            st["+Y"][0] += 1 if ny > 0 else 0
        if y == ymin:
            st["-Y"][1] += 1
            st["-Y"][0] += 1 if ny < 0 else 0
    return st


def main() -> None:
    ids = [int(a, 16) for a in sys.argv[1:]] or SAMPLES
    tables = load_tables()
    keep = []
    grand = {}
    for hid in ids:
        d = find(keep, hid)
        v = VxlFile(d)
        nt = sorted(set(t.normals_type for t in v.tailers))
        slot = (nt[0] - 1) if 1 <= nt[0] <= 4 else 3
        normals = tables[slot]
        print("0x%08X  肢=%d normals_type=%s -> 槽位[%d] %d 项"
              % (hid, v.num_limbs, nt, slot, len(normals)))
        for li in range(v.num_limbs):
            st = score(v, li, normals)
            if not st:
                continue
            parts = []
            for k in ("top", "bottom", "+X", "-X", "+Y", "-Y"):
                hit, tot = st[k]
                if tot == 0:
                    continue
                g = grand.get(k, [0, 0])
                grand[k] = [g[0] + hit, g[1] + tot]
                parts.append("%s %3.0f%%(%d)" % (k, 100.0 * hit / tot, tot))
            print("   limb[%d] %-12s %s" % (li, v.headers[li].name, "  ".join(parts)))
        print()

    print("=== 合计（外表面法线朝外的命中率）===")
    ah = at = 0
    for k in ("top", "bottom", "+X", "-X", "+Y", "-Y"):
        if k not in grand:
            continue
        hit, tot = grand[k]
        ah += hit
        at += tot
        print("  %-7s %5.1f%%   (%d/%d)" % (k, 100.0 * hit / tot, hit, tot))
    print("  %-7s %5.1f%%   (%d/%d)" % ("总计", 100.0 * ah / max(1, at), ah, at))
    print("\n（乱猜的期望值 = 50%%；表配对了应该显著高于它）")


if __name__ == "__main__":
    main()
