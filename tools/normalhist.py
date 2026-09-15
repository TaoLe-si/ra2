"""
normalhist.py -- 统计真实 VXL 里的 normal 字节分布。

为什么需要：gamemd.exe 里找到一段 361 项单位向量，但法线表到底多长、
从哪开始，要靠"体素实际引用了哪些索引"来定。如果体素的 normal 最大是 255，
那表至少 256 项；如果最大是 105，那可能表只有 106 项。

用法：
  python tools/normalhist.py [0xID ...]
"""

from __future__ import annotations

import sys

sys.path.insert(0, r"E:\ra2source\tools")
from mixdump import MixFile, iter_leaves          # noqa: E402
from vxlstruct import VxlFile                     # noqa: E402

MIXES = [r"D:\westwood\RA2YR\ra2.mix", r"D:\westwood\RA2YR\ra2md.mix"]

# 几个代表：1 肢坦克、3 肢加特林炮塔、13 肢机甲
SAMPLES = [0xAE458B95, 0x18EEDFED, 0x891E5F6E, 0x8C848DEE]


def load(keep, hid):
    for p in MIXES:
        m = MixFile(p)
        keep.append(m)
        for owner, h, off, size, depth in iter_leaves(m):
            if h == hid:
                return owner.read(off, size)
    return None


def main() -> None:
    ids = [int(a, 16) for a in sys.argv[1:]] or SAMPLES
    keep = []
    for hid in ids:
        d = load(keep, hid)
        if d is None:
            print("0x%08X 找不到" % hid)
            continue
        v = VxlFile(d)
        vals = []
        nt = set()
        for li in range(v.num_limbs):
            nt.add(v.tailers[li].normals_type)
            for _x, _y, _z, _c, n in v.voxels(li):
                vals.append(n)
        hist = {}
        for n in vals:
            hist[n] = hist.get(n, 0) + 1
        ks = sorted(hist)
        print("0x%08X  肢=%d normals_type=%s  体素=%d  normal 取值 %d..%d，不同值 %d 个"
              % (hid, v.num_limbs, sorted(nt), len(vals), ks[0], ks[-1], len(ks)))
        top = sorted(hist.items(), key=lambda kv: -kv[1])[:12]
        print("   最常见的 12 个：%s" % top)
        # 有没有超过 255 的？（不可能，是 u8）
        # 看 0 和 255 有没有被用
        for probe in (0, 1, 254, 255):
            print("     normal=%3d 的体素数 %d" % (probe, hist.get(probe, 0)))


if __name__ == "__main__":
    main()
