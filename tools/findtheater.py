"""
findtheater.py -- 在 MIX 里按**内容**找剧场定义（terrain INI）。

按名字撞 CRC 找不到 urban.ini / temperat.ini，那就按内容找：
剧场 INI 里一定有 `TilesInSet=` 或 `[TileSet` 这种关键字。
本脚本遍历归档里所有"看起来是文本"的叶子，把命中关键字的抽出来。

用法：
  python tools/findtheater.py D:\\westwood\\RA2YR\\ra2.mix E:\\ra2source\\build\\theater
"""

from __future__ import annotations

import os
import sys

sys.path.insert(0, r"E:\ra2source\tools")
import mixdump  # noqa: E402

KEYS = (b"TilesInSet", b"[TileSet", b"ClearTile", b"RoughTile", b"CliffSet",
        b"Theater=", b"ControlFileName", b"Morphable")


def main() -> None:
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    path, outdir = sys.argv[1], sys.argv[2]
    os.makedirs(outdir, exist_ok=True)

    m = mixdump.MixFile(path)
    print("%s 遍历叶子..." % path)
    n = 0
    for owner, h, off, size, depth in mixdump.iter_leaves(m):
        if size > 2 << 20 or size < 64:
            continue
        data = owner.read(off, size)
        if data.count(0) * 20 > len(data):
            continue                      # 二进制
        hits = [k for k in KEYS if k in data]
        if not hits:
            continue
        n += 1
        name = "%08X_%d.ini" % (h, size)
        out = os.path.join(outdir, name)
        open(out, "wb").write(data)
        print("[命中] id=0x%08X depth=%d size=%-8d 关键字=%s -> %s" % (
            h, depth, size, b",".join(hits).decode("ascii", "ignore"), out))
    print("共 %d 个" % n)
    m.close()


if __name__ == "__main__":
    main()
