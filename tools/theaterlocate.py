"""
theaterlocate.py -- 确定每个剧场 INI 藏在 ra2.mix / ra2md.mix 的哪个子归档里，
并通过 [General] 前几行判断它属于哪个剧场。

用法：
  python tools/theaterlocate.py D:\\westwood\\RA2YR\\ra2.mix [ra2md.mix ...]
"""

from __future__ import annotations

import re
import sys

sys.path.insert(0, r"E:\ra2source\tools")
import mixdump  # noqa: E402


def theatre_hint(text: str) -> str:
    """从 TileSet0000 的 SetName 猜剧场名。"""
    i = text.find("[TileSet0000]")
    if i < 0:
        return "?"
    j = text.find("[", i + 10)
    seg = text[i:j]
    m = re.search(r"(?im)^\s*SetName\s*=\s*(.+?)\s*$", seg)
    return m.group(1) if m else "?"


def main() -> None:
    for p in sys.argv[1:]:
        print("===== %s" % p)
        m = mixdump.MixFile(p)
        for h, off, size in m.entries:
            sub = mixdump.open_nested(m, off, size)
            if sub is None:
                continue
            for sh, soff, ssize in sub.entries:
                if ssize < 64 or ssize > 2 << 20:
                    continue
                data = sub.read(soff, ssize)
                if b"TilesInSet" not in data:
                    continue
                t = data.decode("ascii", "ignore")
                print("  顶层 id=0x%08X  内 id=0x%08X  size=%-8d  剧场线索=%s" % (
                    h, sh, ssize, theatre_hint(t)))
                # 顺手看 [General] 里有没有显式的名字
                for key in ("TheaterName", "Name", "ControlFileName", "Extension"):
                    mm = re.search(r"(?im)^\s*%s\s*=\s*(.+?)\s*$" % key, t)
                    if mm:
                        print("      %s = %s" % (key, mm.group(1)))
        m.close()


if __name__ == "__main__":
    main()
