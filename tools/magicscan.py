"""
magicscan.py -- 在全部 MIX 里按魔数找条目（用于定位 VXL / HVA 到底在哪个包）。

VXL/HVA 在 RA2 里不像 SHP/PCX 那么好找 —— 它们数量少、体积小，
而且可能藏在某层嵌套归档里。与其猜文件名，不如把每个包都扫一遍。

用法：
  python tools/magicscan.py D:\\westwood\\RA2YR Voxel HVA!
"""

from __future__ import annotations

import glob
import os
import sys

sys.path.insert(0, r"E:\ra2source\tools")
import mixdump  # noqa: E402

SKIP = ("movies01.mix", "movies02.mix", "movmd03.mix")


def scan_one(path: str, magics: list[bytes], max_depth: int = 4):
    m = mixdump.MixFile(path)
    hits = []
    leaves = 0

    def walk(mm, d):
        nonlocal leaves
        for h, off, size in mm.entries:
            if off + size > mm.data_size:
                continue
            sub = mixdump.open_nested(mm, off, size) if d < max_depth else None
            if sub is not None:
                walk(sub, d + 1)
                continue
            leaves += 1
            head = mm.head(off, 16)
            for mg in magics:
                if head.startswith(mg):
                    hits.append((h, off, size, d, mg))
                    break

    walk(m, 0)
    return leaves, hits


def main() -> None:
    root = sys.argv[1] if len(sys.argv) > 1 else r"D:\westwood\RA2YR"
    magics = [a.encode("ascii") for a in sys.argv[2:]] or [b"Voxel", b"HVA!"]
    files = []
    for pat in ("*.mix", "*.MIX", "*.mmx", "*.yro"):
        files += glob.glob(os.path.join(root, pat))
    files = sorted(set(f for f in files if os.path.basename(f).lower() not in SKIP))
    total = 0
    for f in files:
        try:
            leaves, hits = scan_one(f, magics)
        except Exception as e:                       # noqa: BLE001
            print("[x] %-28s %s" % (os.path.basename(f), e))
            continue
        total += len(hits)
        if hits:
            print("[OK] %-28s 叶子 %-7d 命中 %d" % (os.path.basename(f), leaves, len(hits)))
            for h, off, size, d, mg in hits[:20]:
                print("       0x%08X d=%d size=%-8d magic=%s" % (h, d, size, mg.decode()))
        else:
            print("[ ] %-28s 叶子 %-7d 命中 0" % (os.path.basename(f), leaves))
    print("[=] 合计命中 %d" % total)


if __name__ == "__main__":
    main()
