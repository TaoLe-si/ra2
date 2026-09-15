"""
tileset.py -- 解析剧场控制 INI，把 TileNum 变成 TMP 文件名。

剧场 INI 的结构（实测 RA2/YR，与 TS 一脉相承）：
    [TileSet0000]
    SetName = LAT Snow
    FileName = Clear
    TilesInSet = 1
    [TileSet0001]
    FileName = blank
    TilesInSet = 0
    ...
TilesInSet 个文件，名字是 FileName + 两位序号，**从 01 开始**。

IsoMapPack5 里的 TileIndex 是**跨 set 累加**的全局序号：
第一个 set 的 N0 个瓦片占 0..N0-1，第二个 set 占 N0..N0+N1-1，依此类推。
同一 set 内部再按序号 01..N 排。

用法：
  python tools/tileset.py <theater.ini> [起始序号 结束序号]
"""

from __future__ import annotations

import re
import sys

_SEC_RE = re.compile(r"(?m)^\[([^\]]+)\]\s*$")
_KV_RE = re.compile(r"(?m)^\s*([A-Za-z0-9_]+)\s*=\s*([^\r\n;]*?)\s*$")


def parse_sections(text: str):
    """切成 [(段名, 段体文本)]，跳过注释行。"""
    marks = [(m.start(), m.group(1)) for m in _SEC_RE.finditer(text)]
    out = []
    for i, (off, name) in enumerate(marks):
        end = marks[i + 1][0] if i + 1 < len(marks) else len(text)
        body = text[off:end]
        # 去掉注释
        body = "\n".join(l.split(";")[0] for l in body.splitlines())
        out.append((name, body))
    return out


def load_tiles(path: str):
    """返回 (瓦片基名列表, set 列表)；基名不含扩展名。"""
    text = open(path, "r", encoding="ascii", errors="ignore").read()
    sets = []
    for name, body in parse_sections(text):
        if not name.lower().startswith("tileset"):
            continue
        kv = dict(_KV_RE.findall(body))
        if "FileName" not in kv:
            continue
        sets.append({
            "section": name,
            "setname": kv.get("SetName", ""),
            "file": kv["FileName"],
            "count": int(kv.get("TilesInSet", "0") or 0),
            "morphable": kv.get("Morphable", "").lower() == "true",
        })

    tiles = []
    for s in sets:
        s["base"] = len(tiles)
        for i in range(1, s["count"] + 1):
            tiles.append("%s%02d" % (s["file"], i))
    return tiles, sets


def main() -> None:
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    tiles, sets = load_tiles(sys.argv[1])
    print("%s  瓦片集 %d 个，瓦片总数 %d" % (sys.argv[1], len(sets), len(tiles)))
    lo, hi = 0, min(20, len(tiles))
    if len(sys.argv) >= 4:
        lo, hi = int(sys.argv[2]), int(sys.argv[3])
    for i in range(lo, min(hi, len(tiles))):
        print("  %5d  %s" % (i, tiles[i]))


if __name__ == "__main__":
    main()
