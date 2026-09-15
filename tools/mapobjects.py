"""
mapobjects.py -- 看真实地图里 [Units]/[Infantry]/[Structures]/[Terrain] 到底长什么样。

写对象系统之前必须先拿到事实：这些段的字段个数和顺序在资料里说法不一，
靠猜写出来的解析器一定会错。本脚本直接从 .mmx/.yro（加密 MIX）里剥出
内层 .map，把对象段原样打印出来。

用法：
  python tools/mapobjects.py D:\\westwood\\RA2YR\\Arena.mmx
"""

from __future__ import annotations

import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mixdump import MixFile  # noqa: E402

# 只关心这几段：地形装饰 / 车辆 / 步兵 / 建筑 / 飞机 / 路径点
SECTIONS = ("Terrain", "Units", "Infantry", "Structures", "Aircraft",
            "Waypoints", "CellTags")

_SEC_RE = re.compile(rb"(?m)^\[([A-Za-z0-9_]+)\][ \t]*\r?$")

# IsoMapPack5 这些是 base64 压缩流，跳过。
_BLOB = ("IsoMapPack5", "OverlayPack", "OverlayDataPack", "Digest",
         "IsoMapPack1", "IsoMapPack2", "IsoMapPack3", "IsoMapPack4")


def find_map_blob(mix: MixFile) -> bytes | None:
    """地图 MIX 里找内层 .map。

    不能取"第一个以 [ 开头的条目"——Ice_Age.yro 的第一条是 116 字节的
    [MultiMaps] 描述。判据改成：内容里含 "[IsoMapPack5"。
    """
    # 判据：内容是可打印 ASCII 文本，且不含 base64 压缩流那样的 NUL。
    # 不能取"第一个以 [ 开头的条目"——Ice_Age.yro 的第一条是 116 字节的
    # [MultiMaps] 描述；也不能只看前 64 字节里有没有 [Map，
    # Arena.mmx 的 .map 是以 [SpecialFlags] 开头的。
    best = None
    for _h, off, size in mix.entries:
        if size < 512 or size > 8 * 1024 * 1024:
            continue
        head = mix.head(off, 256)
        if any(c < 9 or (13 < c < 32) or c > 126 for c in head):
            continue
        if best is None or size > best[1]:
            best = (off, size)
    return mix.read(*best) if best else None


def split_sections(text: bytes) -> list[tuple[str, str]]:
    marks = [(m.start(), m.group(1).decode("ascii")) for m in _SEC_RE.finditer(text)]
    out = []
    for i, (off, name) in enumerate(marks):
        end = marks[i + 1][0] if i + 1 < len(marks) else len(text)
        body = text[off:end]
        nl = body.find(b"\n")
        payload = body[nl + 1:] if nl >= 0 else b""
        out.append((name, payload.decode("ascii", "ignore")))
    return out


def iso_to_cell(X: int, Y: int, W: int) -> tuple[int, int]:
    """对象/路径点的 (X,Y) -> 逻辑格 (cx,cy)。

    坐标帧是实测逼出来的，别再重新推：
      * 对象段里的 X,Y **就是 IsoMapPack5 的 X,Y**（对角帧），
        不是格坐标 —— Arena(80x80) 的对象 X 能到 140，远超 80。
      * 反解 cx=(X+Y-1-W)/2, cy=(Y-1-(X-W))/2 只有在 X+Y 为奇数时才是整数。
        **实测对象里 X+Y 奇偶各占一半**（Arena 408 个对象 201 奇 / 207 偶），
        所以必须取整，且必须**下取整**。
      * 为什么是下取整而不是四舍五入：同一路口的 4 个红绿灯
        (81,47)(81,50)(85,50)(85,47) 下取整后得 (23,22)(25,24)(27,22)(25,20)，
        是以 (25,22) 为中心的**完美菱形**；四舍五入会得到 (24,22)(25,24)(27,22)(26,20)，
        中心偏到 (25.5,22)，不对称。
    """
    return (X + Y - 1 - W) // 2, (Y - 1 - (X - W)) // 2


def main() -> None:
    path = sys.argv[1]
    mix = MixFile(path)
    blob = find_map_blob(mix)
    if blob is None:
        print("没找到内层 .map")
        return
    print("%s  内层 .map %d 字节" % (path, len(blob)))

    secs = dict(split_sections(blob))
    mp = dict(l.split("=", 1) for l in secs["Map"].splitlines() if "=" in l)
    W, H = [int(v) for v in mp["Size"].split(",")][2:]
    print("[Map] %dx%d" % (W, H))

    # 坐标帧自检：所有对象换算后必须落在地图内
    pts: list[tuple[int, int]] = []
    for sec in ("Units", "Infantry", "Structures", "Aircraft"):
        for ln in secs.get(sec, "").splitlines():
            if "=" not in ln:
                continue
            f = ln.split("=", 1)[1].split(",")
            pts.append((int(f[3]), int(f[4])))
    for ln in secs.get("Terrain", "").splitlines():
        if "=" not in ln:
            continue
        k = int(ln.split("=", 1)[0])
        pts.append((k // 1000, k % 1000))
    for ln in secs.get("Waypoints", "").splitlines():
        if "=" not in ln:
            continue
        k = int(ln.split("=", 1)[1])
        pts.append((k // 1000, k % 1000))
    bad = 0
    for X, Y in pts:
        cx, cy = iso_to_cell(X, Y, W)
        if not (0 <= cx < W and 0 <= cy < H):
            bad += 1
    print("坐标帧自检: %d 个对象, 越界 %d %s" % (len(pts), bad, "OK" if bad == 0 else "FAIL"))

    for name, payload in split_sections(blob):
        if name not in SECTIONS:
            continue
        lines = [ln for ln in payload.splitlines() if ln.strip()]
        print("\n[%s]  %d 行" % (name, len(lines)))
        for ln in lines[:12]:
            print("   " + ln)
        if len(lines) > 12:
            print("   ... 省略 %d 行" % (len(lines) - 12))

        # 统计逗号个数分布 —— 字段数是否一致一眼看出
        dist: dict[int, int] = {}
        for ln in lines:
            body = ln.split("=", 1)[1] if "=" in ln else ln
            dist[body.count(",")] = dist.get(body.count(","), 0) + 1
        print("   逗号数分布:", dict(sorted(dist.items())))


if __name__ == "__main__":
    main()
