"""
theaterprobe.py -- 定位剧场素材：剧场 INI（temperat.ini / urban.ini ...）
与装 TMP 地形的子归档（isotemp.mix / isourb.mix ...）。

modenc 的 IsoMapPack5 页面说得很明确：
  地形 TMP 在 ra2.mix/isotemp.mix/*.tmp 里，
  瓦片信息在该剧场的 terrain INI（temperat.ini / snow.ini ...）里。
但 MIX 只存 CRC，所以这里就是把候选名逐个算出 CRC 去撞归档。

用法：
  python tools/theaterprobe.py D:\\westwood\\RA2YR\\ra2.mix [ra2md.mix ...]
"""

from __future__ import annotations

import sys

sys.path.insert(0, r"E:\ra2source\tools")
import mixdump  # noqa: E402

# 剧场 INI：RA2 本体 temperate / snow / urban，YR 追加 desert / lunar / new urban
THEATERS = ["temperat", "snow", "urban", "desert", "lunar", "newurb",
            "newurban", "temperate"]

# 装 TMP 的子归档。RA2 里常见的命名习惯：iso + 剧场缩写
ISO_MIX = ["isotemp", "isosn", "isourb", "isodes", "isolun", "isoubn",
           "isogen", "generic", "snow", "urban", "desert", "lunar",
           "temperat", "newurb", "isomix", "iso", "isotem"]

EXT_INI = ("ini", "INI", "mix", "MIX")


def main() -> None:
    paths = sys.argv[1:]
    if not paths:
        print(__doc__)
        sys.exit(1)

    for p in paths:
        m = mixdump.MixFile(p)
        ids = {h: (off, size) for h, off, size in m.entries}
        print("%s  条目=%d" % (p, m.count))
        cands = set()
        for t in THEATERS:
            for e in EXT_INI:
                cands.add("%s.%s" % (t, e))
        for t in ISO_MIX:
            for e in EXT_INI:
                cands.add("%s.%s" % (t, e))
        for n in sorted(cands):
            c = mixdump.westwood_crc(n)
            if c in ids:
                off, size = ids[c]
                print("   [命中] %-16s id=0x%08X off=%-10d size=%-10d head=%s" % (
                    n, c, off, size, m.head(off, 16).hex(" ")))
        m.close()


if __name__ == "__main__":
    main()
