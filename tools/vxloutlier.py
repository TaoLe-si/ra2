# -*- coding: utf-8 -*-
"""探针：某个 VXL 里有没有落在主体之外的"细长薄片"（渲染时看起来像一根板）。

做法：按 (x,y) 列统计体素的 z 范围，再看哪些列明显脱离主体簇。
如果所有列都在合理范围内，那薄片就是模型自带的几何，不是解码错。
"""
import os
import sys

sys.path.insert(0, r"E:\ra2source\tools")
from mixdump import MixFile, iter_leaves          # noqa: E402
from vxlstruct import VxlFile                     # noqa: E402

MIXES = [r"D:\westwood\RA2YR\ra2.mix", r"D:\westwood\RA2YR\ra2md.mix"]


def find(keep, hid):
    for p in MIXES:
        m = MixFile(p)
        keep.append(m)
        for owner, h, off, size, depth in iter_leaves(m):
            if h == hid:
                return owner.read(off, size)
    return None


def main():
    hid = int(sys.argv[1], 16) if len(sys.argv) > 1 else 0xC774A88C
    keep = []
    d = find(keep, hid)
    v = VxlFile(d)
    print("limbs=%d body=%d size=%d" % (v.num_limbs, v.body_size, len(d)))
    for li in range(v.num_limbs):
        t = v.tailers[li]
        vox = list(v.voxels(li))
        print("limb %d %-12s %dx%dx%d 体素=%d" % (
            li, v.headers[li].name, t.x, t.y, t.z, len(vox)))
        print("   min_bounds=%s" % (t.min_bounds,))
        print("   max_bounds=%s" % (t.max_bounds,))
        print("   transform=%s" % (["%.3f" % x for x in t.transform],))
        # 按 (x,y) 统计 z 范围
        cols = {}
        for x, y, z, _c, _n in vox:
            lo, hi = cols.get((x, y), (255, -1))
            cols[(x, y)] = (min(lo, z), max(hi, z))
        zs = [hi for _lo, hi in cols.values()]
        zs.sort()
        print("   列数=%d 列顶 z 分布: min=%d 中位=%d max=%d" % (
            len(cols), zs[0], zs[len(zs) // 2], zs[-1]))
        # 找 z 顶明显低于中位的列（薄片）
        med = zs[len(zs) // 2]
        odd = [(k, vv) for k, vv in cols.items() if vv[1] - vv[0] <= 1]
        print("   只有 1~2 格厚的列: %d 个（例：%s）" % (
            len(odd), sorted(odd)[:6]))


if __name__ == "__main__":
    main()
