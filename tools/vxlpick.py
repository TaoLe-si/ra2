"""vxlpick.py -- 从 MIX 里挑出 VXL 样本。

用法:
  python tools/vxlpick.py scan  D:\\westwood\\RA2YR\\ra2.mix
  python tools/vxlpick.py get   D:\\westwood\\RA2YR\\ra2.mix 0x90DF4F6A out.vxl
  python tools/vxlpick.py dump  D:\\westwood\\RA2YR\\ra2.mix 0x90DF4F6A [limb]
"""
from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from mixdump import MixFile, iter_leaves                      # noqa: E402
from vxlstruct import VxlFile, VxlError, dump                 # noqa: E402


def find_all(path: str):
    m = MixFile(path)
    out = []
    for owner, h, off, size, depth in iter_leaves(m):
        if size < 1024:
            continue
        head = owner.read(off, 15)
        if head != b"Voxel Animation":
            continue
        out.append((h, size, depth, owner, off))
    return out


def main() -> None:
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    cmd, path = sys.argv[1], sys.argv[2]
    if cmd == "scan":
        hits = find_all(path)
        hits.sort(key=lambda r: r[1])
        print("%s 里 VXL 共 %d 个" % (path, len(hits)))
        for h, size, depth, _o, _f in hits[:30]:
            print("  id=0x%08X size=%-8d depth=%d" % (h, size, depth))
        return
    if len(sys.argv) < 4:
        print(__doc__)
        sys.exit(1)
    want = int(sys.argv[3], 16)
    for h, size, depth, owner, off in find_all(path):
        if h != want:
            continue
        data = owner.read(off, size)
        if cmd == "get":
            open(sys.argv[4], "wb").write(data)
            v = VxlFile(data)
            print("[OK] id=0x%08X size=%d -> %s" % (h, size, sys.argv[4]))
            print("     " + v.summary())
        elif cmd == "dump":
            v = VxlFile(data)
            dump(v, int(sys.argv[4]) if len(sys.argv) > 4 else 0,
                 max_cols=48)
        return
    print("[x] 没有 id=0x%08X" % want)
    sys.exit(1)


if __name__ == "__main__":
    main()
