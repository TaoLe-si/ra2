"""
mapcellstat.py -- 统计一张地图的格子瓦片/高度分布。

为什么需要：画布上出现"整格大小的黑色菱形"时，有两种可能 ——
  (a) 那一格本来就没有瓦片（tile 为空），原版会画成悬崖侧面或水；
  (b) 我们的解码/贴图错位了。
先把数据的分布摆出来，才能判定是 (a) 还是 (b)。

用法：
  python tools/mapcellstat.py D:\\westwood\\RA2YR\\Arena.mmx
"""

from __future__ import annotations

import base64
import re
import struct
import sys

sys.path.insert(0, r"E:\ra2source\tools")
import lzo1x      # noqa: E402
import mixdump    # noqa: E402

_SEC_RE = re.compile(rb"(?m)^\[([A-Za-z0-9_]+)\][ \t]*\r?$")


def read_map_blob(path: str) -> tuple[bytes, str]:
    """从 .mmx/.yro（加密 MIX）或裸 .map 里取出 .map 正文。"""
    d = open(path, "rb").read()
    if d[:4] == b"\x00\x00\x00\x00" or b"[" in d[:512]:
        return d, "(裸 map)"
    m = mixdump.MixFile(path)
    for owner, h, off, size, _depth in mixdump.iter_leaves(m):
        blob = owner.read(off, size)
        if b"[IsoMapPack5" in blob:
            return blob, "0x%08X" % h
    raise SystemExit("[x] MIX 里没有含 [IsoMapPack5 的条目")


def sections(d: bytes) -> dict:
    marks = [(x.start(), x.group(1).decode()) for x in _SEC_RE.finditer(d)]
    out = {}
    for i, (off, name) in enumerate(marks):
        end = marks[i + 1][0] if i + 1 < len(marks) else len(d)
        body = d[off:end]
        nl = body.find(b"\n")
        out[name] = body[nl + 1:] if nl >= 0 else b""
    return out


def main() -> None:
    path = sys.argv[1]
    blob, where = read_map_blob(path)
    secs = sections(blob)

    maptxt = secs.get("Map", b"").decode("ascii", "ignore")
    size = None
    theater = ""
    for line in maptxt.splitlines():
        line = line.strip()
        if line.lower().startswith("size="):
            size = [int(x) for x in line.split("=")[1].split(",")]
        if line.lower().startswith("theater="):
            theater = line.split("=", 1)[1].strip()
    print("地图条目 %s  剧场 %s  [Map] Size=%s" % (where, theater, size))

    txt = secs.get("IsoMapPack5", b"").decode("ascii", "ignore")
    parts = []
    for line in txt.splitlines():
        line = line.strip()
        if line and "=" in line:
            parts.append(line.split("=", 1)[1].strip())
    packed = base64.b64decode("".join(parts))
    raw = lzo1x.decompress_chunks(packed)
    n = len(raw) // 11
    print("IsoMapPack5 解出 %d 字节 = %d 条记录" % (len(raw), n))

    tiles = []
    levels = []
    for i in range(n):
        x, y, t, sub, lv, ice = struct.unpack_from("<hhIBBH", raw, i * 11)[0:6] \
            if False else (0, 0, 0, 0, 0, 0)
    # 上面的写法太绕，老实来：
    tiles = []
    levels = []
    subs = []
    for i in range(n):
        o = i * 11
        x, y = struct.unpack_from("<hh", raw, o)
        (t,) = struct.unpack_from("<i", raw, o + 4)
        sub = raw[o + 8]
        lv = raw[o + 9]
        ice = raw[o + 10]
        if x == 0 and y == 0 and t == 0:
            continue          # 终止记录
        tiles.append(t)
        levels.append(lv)
        subs.append(sub)

    W = size[2] if size else 0
    H = size[3] if size else 0
    print("逻辑 %dx%d = %d 格" % (W, H, W * H))

    neg = sum(1 for t in tiles if t < 0)
    print("tile < 0 的记录: %d" % neg)
    print("tile 取值范围: %d .. %d" % (min(tiles), max(tiles)))

    from collections import Counter
    lv_hist = Counter(levels)
    print("Level 分布:", dict(sorted(lv_hist.items())))
    sub_hist = Counter(subs)
    print("SubTile 分布（前 12）:", dict(sorted(sub_hist.items())[:12]))

    # 按 (cx, cy) 落盘成一张 ASCII 高度图，看"黑洞"是不是高度台阶的边
    odd = [(t, lv, s) for t, lv, s in zip(tiles, levels, subs)]
    print("奇偶记录数: %d（应等于 2W-1)*H = %d" % (len(odd), (2 * W - 1) * H))


if __name__ == "__main__":
    main()
