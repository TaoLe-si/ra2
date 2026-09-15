"""vxlverify.py -- 在全部 VXL 样本上严格验证 span 解码器。

每条判据都来自实测：终止符 delta 必须正好落在 Z、计数重复字节必须等于 n、
字节必须刚好消费完、z+n 不得越出 Z。任何一条不成立就说明格式没破对。

用法: python tools/vxlverify.py D:\\westwood\\RA2YR\\ra2.mix [D:\\westwood\\RA2YR\\ra2md.mix ...]
"""
from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mixdump import MixFile, iter_leaves          # noqa: E402
from vxlstruct import VxlFile, VxlError           # noqa: E402


def run(path: str) -> int:
    m = MixFile(path)
    files = cols = vox = 0
    bad = 0
    for owner, h, off, size, depth in iter_leaves(m):
        if size < 1024 or owner.read(off, 15) != b"Voxel Animation":
            continue
        try:
            v = VxlFile(owner.read(off, size))
        except VxlError as e:
            print("  [!] 0x%08X 结构错: %s" % (h, e))
            bad += 1
            continue
        files += 1
        for li in range(v.num_limbs):
            t = v.tailers[li]
            for i in range(t.x * t.y):
                try:
                    vs = v.decode_column(li, i)
                except VxlError as e:
                    if bad < 10:
                        print("  [!] 0x%08X limb%d: %s" % (h, li, e))
                    bad += 1
                    continue
                cols += 1
                vox += len(vs)
    print("%s: VXL %d 个，列 %d，体素 %d，异常 %d" % (path, files, cols, vox, bad))
    return bad


def main() -> None:
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    total = 0
    for p in sys.argv[1:]:
        total += run(p)
    print("=== 合计异常 %d ===" % total)
    sys.exit(1 if total else 0)


if __name__ == "__main__":
    main()
