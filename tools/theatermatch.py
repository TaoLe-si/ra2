"""
theatermatch.py -- 判断每个剧场控制 INI 属于哪个剧场。

思路（不靠猜名字，靠数据）：
  剧场 INI 给出一串瓦片基名（Clear01、mslop01 ...）。
  瓦片文件就在 ra2.mix/isoXXX.mix 里，扩展名按剧场不同（tem/sno/urb/des/lun/ubn）。
  所以把"基名 + 扩展名"算成 Westwood CRC，去每个 iso 归档的 ID 集合里撞，
  命中率最高的那个就是它真正属于的剧场。

用法：
  python tools/theatermatch.py
"""

from __future__ import annotations

import sys

sys.path.insert(0, r"E:\ra2source\tools")
import mixdump  # noqa: E402
import tileset  # noqa: E402

RA2 = r"D:\westwood\RA2YR\ra2.mix"
RA2MD = r"D:\westwood\RA2YR\ra2md.mix"

# 剧场 -> 扩展名 / 对应地形归档
THEATERS = [
    ("TEMPERATE", "tem", "isotemp.mix"),
    ("SNOW", "sno", "isosn.mix"),
    ("URBAN", "urb", "isourb.mix"),
    ("DESERT", "des", "isodes.mix"),
    ("LUNAR", "lun", "isolun.mix"),
    ("NEWURBAN", "ubn", "isoubn.mix"),
]

# 装 INI 的顶层条目（theaterlocate.py 查出来的）
INI_HOSTS = [(RA2, 0xA8548FD9), (RA2MD, 0xFBE0D09D)]


def collect_ids(path: str, sub_name: str) -> set:
    root = mixdump.MixFile(path)
    hit = root.find(sub_name)
    if hit is None:
        root.close()
        return set()
    off, size = hit
    sub = mixdump.open_nested(root, off, size)
    ids = {h for h, _, _ in sub.entries} if sub else set()
    root.close()
    return ids


def main() -> None:
    # 1) 先把各 iso 归档的 ID 集合抓齐（isogen 是通用瓦片，所有剧场共用）
    pools = {}
    for path in (RA2, RA2MD):
        for _t, _e, mixname in THEATERS:
            if mixname in pools:
                continue
            ids = collect_ids(path, mixname)
            if ids:
                pools[mixname] = ids
                print("  %-14s 来自 %s，条目 %d" % (
                    mixname, path.rsplit("\\", 1)[-1], len(ids)))
    gen = collect_ids(RA2, "isogen.mix") | collect_ids(RA2, "generic.mix")
    print("  isogen+generic 条目 %d" % len(gen))

    # 2) 逐个 INI 试
    for path, host in INI_HOSTS:
        root = mixdump.MixFile(path)
        hit = [(o, s) for h, o, s in root.entries if h == host]
        if not hit:
            root.close()
            continue
        off, size = hit[0]
        sub = mixdump.open_nested(root, off, size)
        for sh, soff, ssize in sub.entries:
            if ssize < 64 or ssize > 2 << 20:
                continue
            data = sub.read(soff, ssize)
            if b"TilesInSet" not in data:
                continue
            tmp = r"E:\ra2source\build\_t.ini"
            open(tmp, "wb").write(data)
            tiles, sets = tileset.load_tiles(tmp)
            print("\nINI id=0x%08X (%s) 瓦片集 %d 瓦片 %d" % (
                sh, path.rsplit("\\", 1)[-1], len(sets), len(tiles)))
            for tname, ext, mixname in THEATERS:
                pool = pools.get(mixname, set()) | gen
                n = sum(1 for t in tiles
                        if mixdump.westwood_crc("%s.%s" % (t, ext)) in pool)
                n_up = sum(1 for t in tiles
                           if mixdump.westwood_crc("%s.%s" % (t, ext.upper())) in pool)
                print("    %-10s .%-3s %-14s 命中 %d/%d (大写 %d)" % (
                    tname, ext, mixname, n, len(tiles), n_up))
        root.close()


if __name__ == "__main__":
    main()
