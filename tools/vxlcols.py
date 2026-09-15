"""vxlcols.py -- 逐列摊开 VXL 的 span 数据，用来确定列内编码。

用法:
  python tools/vxlcols.py one   D:\\westwood\\RA2YR\\ra2.mix 0x90DF4F6A [limb] [n]
  python tools/vxlcols.py scan  D:\\westwood\\RA2YR\\ra2.mix   # 全样本统计
"""
from __future__ import annotations

import os
import sys
from collections import Counter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mixdump import MixFile, iter_leaves          # noqa: E402
from vxlstruct import VxlFile, NO_SPAN            # noqa: E402


def columns(v: VxlFile, limb: int):
    """产出 (idx, x, y, bytes) —— 列的原始数据切片（相对数据块）。"""
    t = v.tailers[limb]
    st, en = v.span_tables(limb)
    base = t.span_data_ofs
    data = v.body[base:]
    out = []
    for i in range(t.x * t.y):
        if st[i] == NO_SPAN:
            out.append((i, i % t.x, i // t.x, None))
            continue
        a, b = st[i], en[i]
        out.append((i, i % t.x, i // t.x, data[a:b + 1]))
    return out


def one(path: str, want: int, limb: int = 0, n: int = 24):
    m = MixFile(path)
    data = None
    for owner, h, off, size, depth in iter_leaves(m):
        if h == want and size >= 1024 and owner.read(off, 15) == b"Voxel Animation":
            data = owner.read(off, size)
            break
    if data is None:
        print("[x] 没有 id=0x%08X" % want)
        return
    v = VxlFile(data)
    t = v.tailers[limb]
    print(v.summary())
    print("limb %d '%s' %dx%dx%d nt=%d  span_data_ofs=%d body=%d"
          % (limb, v.headers[limb].name, t.x, t.y, t.z, t.normals_type,
             t.span_data_ofs, v.body_size))
    st, en = v.span_tables(limb)
    cols = [c for c in columns(v, limb) if c[3] is not None]
    print("非空列 %d / %d" % (len(cols), t.x * t.y))
    prev_end = None
    for i, x, y, b in cols[:n]:
        gap = "" if prev_end is None else " gap=%d" % (st[i] - prev_end - 1)
        print("  [%2d] (%2d,%2d) [%4d..%4d] len=%-3d%s  %s"
              % (i, x, y, st[i], en[i], len(b), gap, b.hex(" ")))
        prev_end = en[i]
    if len(cols) > n:
        print("  ... 还有 %d 列" % (len(cols) - n))


def scan(path: str):
    m = MixFile(path)
    lens = Counter()
    zs = Counter()
    nts = Counter()
    heads = Counter()
    total_cols = 0
    files = 0
    for owner, h, off, size, depth in iter_leaves(m):
        if size < 1024:
            continue
        head = owner.read(off, 15)
        if head != b"Voxel Animation":
            continue
        try:
            v = VxlFile(owner.read(off, size))
        except Exception as e:
            print("  [!] 0x%08X 解析失败: %s" % (h, e))
            continue
        files += 1
        for li in range(v.num_limbs):
            t = v.tailers[li]
            zs[t.z] += 1
            nts[t.normals_type] += 1
            for _i, _x, _y, b in columns(v, li):
                if b is None:
                    continue
                total_cols += 1
                lens[len(b)] += 1
                heads[b[:2].hex()] += 1
    print("VXL %d 个，非空列 %d 个" % (files, total_cols))
    print("列长分布 (top20): %s" % lens.most_common(20))
    print("Z 分布: %s" % zs.most_common(12))
    print("normals_type 分布: %s" % nts.most_common())
    print("列前 2 字节 (top15): %s" % heads.most_common(15))


def main() -> None:
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    cmd, path = sys.argv[1], sys.argv[2]
    if cmd == "one":
        one(path, int(sys.argv[3], 16),
            int(sys.argv[4]) if len(sys.argv) > 4 else 0,
            int(sys.argv[5]) if len(sys.argv) > 5 else 24)
    elif cmd == "scan":
        scan(path)


if __name__ == "__main__":
    main()
