"""
mapdecode.py -- 解开 RA2/YR 地图的 [IsoMapPack5]，验证解压正确性。

链路：
  .mmx/.yro（加密 MIX） -> <NAME>.map -> [IsoMapPack5] 段（base64）
  -> 分块 LZO 解压 -> 11 字节一条的瓦片记录

每条瓦片记录（modenc IsoMapPack5）：
  int16 X, int16 Y, int32 TileIndex, uint8 TileSubIndex, uint8 Level, uint8 IceGrowth
末尾还有 4 字节全 0 的终止记录（X=Y=0 被游戏当作结束）。

用法：
  python tools/mapdecode.py E:\\ra2source\\build\\map_arena\\ARENA.map
"""

from __future__ import annotations

import base64
import re
import struct
import sys

sys.path.insert(0, r"E:\ra2source\tools")
import lzo1x  # noqa: E402

_SEC_RE = re.compile(rb"(?m)^\[([A-Za-z0-9_]+)\][ \t]*\r?$")


def sections(path: str):
    d = open(path, "rb").read()
    marks = [(m.start(), m.group(1).decode("ascii")) for m in _SEC_RE.finditer(d)]
    out = {}
    for i, (off, name) in enumerate(marks):
        end = marks[i + 1][0] if i + 1 < len(marks) else len(d)
        body = d[off:end]
        nl = body.find(b"\n")
        out[name] = body[nl + 1:] if nl >= 0 else b""
    return out


def b64_section(payload: bytes) -> bytes:
    """段里每行都是 `数字=base64...`（行号只是序号，不是数据）。"""
    txt = payload.decode("ascii", "ignore")
    parts = []
    for line in txt.splitlines():
        line = line.strip()
        if not line or "=" not in line:
            continue
        parts.append(line.split("=", 1)[1].strip())
    return base64.b64decode("".join(parts))


def main() -> None:
    path = sys.argv[1]
    secs = sections(path)

    m = secs.get("Map", b"").decode("ascii", "ignore")
    print("[Map]")
    for line in m.splitlines():
        if line.strip():
            print("   " + line)
    size = None
    for line in m.splitlines():
        if line.strip().lower().startswith("size="):
            size = [int(x) for x in line.split("=")[1].split(",")]
    theater = ""
    for line in m.splitlines():
        if line.strip().lower().startswith("theater="):
            theater = line.split("=")[1].strip()

    raw = b64_section(secs["IsoMapPack5"])
    print("\n[IsoMapPack5] base64 解出 %d 字节" % len(raw))
    data = lzo1x.decompress_chunks(raw)
    print("LZO 解压后 %d 字节" % len(data))

    cells = (size[2] * 2 - 1) * size[3] if size else 0
    print("地图 %s，尺寸 %s -> 单元数 %d，期望字节 %d" % (theater, size, cells, cells * 11 + 4))

    n = (len(data) - 4) // 11
    ok = 0
    bad = []
    tiles = {}
    levels = {}
    for i in range(n):
        o = i * 11
        x, y = struct.unpack_from("<hh", data, o)
        ti, = struct.unpack_from("<i", data, o + 4)
        sub, lvl, ice = data[o + 8], data[o + 9], data[o + 10]
        if 0 <= x <= (size[2] * 2 - 2) and 0 <= y <= size[3] - 1 and -1 <= ti < 100000:
            ok += 1
        else:
            bad.append((i, x, y, ti))
        tiles[ti] = tiles.get(ti, 0) + 1
        levels[lvl] = levels.get(lvl, 0) + 1
    print("\n瓦片记录 %d 条，合法 %d，越界 %d" % (n, ok, len(bad)))
    if bad:
        print("  前 5 条越界: %s" % bad[:5])
    print("  末尾 4 字节: %s" % data[len(data) - 4:].hex(" "))
    print("  不同 TileIndex 个数 %d，范围 %d..%d" % (
        len(tiles), min(tiles), max(tiles)))
    print("  Level 分布: %s" % sorted(levels.items()))
    print("  最常见的 10 个 TileIndex: %s" % sorted(
        tiles.items(), key=lambda kv: -kv[1])[:10])


if __name__ == "__main__":
    main()
