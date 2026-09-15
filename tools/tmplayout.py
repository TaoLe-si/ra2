"""
tmplayout.py -- 定量判定 TMP 的 900 字节等距数据到底怎么铺进 60x30 的菱形。

为什么需要：菱形轮廓是几何画出来的，所以"看起来是菱形"不能证明行序对。
平滑纹理（草地）在任何行序下看起来都像纹理。必须用可量化的判据：

  **正确布局下，空间上相邻的像素在数据流里也相邻 —— 平均差值最小。**

要求候选布局必须把菱形**铺满**（一个像素不漏），否则"没铺到"的地方会让
邻差统计失真（之前试过非满铺候选，垂直邻差恒为 0，结论毫无意义）。

用法：
  python tools/tmplayout.py
"""

from __future__ import annotations

import struct
import sys

sys.path.insert(0, r"E:\ra2source\tools")
import mixdump  # noqa: E402

TOP = r"D:\westwood\RA2YR\ra2.mix"
THEATER = 0x0F5D1D99          # 温带地形归档
CW, CH = 60, 30
N = CW * CH // 2              # 900


def diamond_rows(cw=CW, ch=CH):
    """等距菱形逐行 (行首 x, 行宽 w)。60x30：宽 2,6,...,58,58,...,6,2 合计 900。"""
    rows = []
    for y in range(ch):
        d = min(y, ch - 1 - y)
        rows.append((cw // 2 - 1 - 2 * d, 2 + 4 * d))
    return rows


def positions(order, cw=CW, ch=CH):
    """返回 [(x, y), ...] 共 900 个位置，顺序由 order 决定。"""
    rows = diamond_rows(cw, ch)
    if order == "rows-tb":
        seq = [(x + k, y) for y, (x, w) in enumerate(rows) for k in range(w)]
    elif order == "rows-bt":
        seq = [(x + k, y) for y, (x, w) in enumerate(reversed(rows))
               for k in range(w)]
    elif order == "rows-banded":
        # 上半第 k 行 紧接 下半第 k 行（两行等宽）
        seq = []
        for k in range(ch // 2):
            for y in (k, ch - 1 - k):
                x, w = rows[y]
                seq += [(x + j, y) for j in range(w)]
    elif order == "cols-lr":
        seq = []
        for x in range(cw):
            for y in range(ch):
                xx, w = rows[y]
                if xx <= x < xx + w:
                    seq.append((x, y))
    elif order == "cols-rl":
        seq = []
        for x in range(cw - 1, -1, -1):
            for y in range(ch):
                xx, w = rows[y]
                if xx <= x < xx + w:
                    seq.append((x, y))
    else:
        raise ValueError(order)
    assert len(seq) == N, "%s 只铺了 %d 个位置" % (order, len(seq))
    return seq


ORDERS = ["rows-tb", "rows-bt", "rows-banded", "cols-lr", "cols-rl"]
_POS = {o: positions(o) for o in ORDERS}


def build(blob, order, cw=CW, ch=CH):
    canvas = [None] * (cw * ch)
    for i, (x, y) in enumerate(_POS[order]):
        canvas[y * cw + x] = blob[i]
    return canvas


def score(canvas, cw=CW, ch=CH):
    """返回 (水平邻差均值, 垂直邻差均值)，两侧都有像素才计入。"""
    hd = vd = hn = vn = 0
    for y in range(ch):
        for x in range(cw):
            v = canvas[y * cw + x]
            if v is None:
                continue
            if x + 1 < cw and canvas[y * cw + x + 1] is not None:
                hd += abs(v - canvas[y * cw + x + 1])
                hn += 1
            if y + 1 < ch and canvas[(y + 1) * cw + x] is not None:
                vd += abs(v - canvas[(y + 1) * cw + x])
                vn += 1
    return hd / max(1, hn), vd / max(1, vn)


def collect(max_tiles=200):
    m = mixdump.MixFile(TOP)
    off, size = [(o, s) for h, o, s in m.entries if h == THEATER][0]
    sub = mixdump.open_nested(m, off, size)
    out = []
    for i, (h, so, ss) in enumerate(sub.entries):
        d = sub.read(so, min(80, ss))
        if len(d) < 80 or d[:8] != b"\x01\x00\x00\x00\x01\x00\x00\x00":
            continue
        b = sub.read(so, ss)
        off0 = struct.unpack_from("<I", b, 16)[0]
        blob = b[off0 + 52:off0 + 52 + N]
        if len(blob) < N:
            continue
        out.append((i, h, blob))
        if len(out) >= max_tiles:
            break
    return out


def main() -> None:
    tiles = collect()
    print("用 %d 个 1x1 瓦片做统计。判据：相邻像素差越小 = 空间连续性越好\n" % len(tiles))

    acc = {o: [0.0, 0.0] for o in ORDERS}
    wins = {o: 0 for o in ORDERS}
    for _i, _h, blob in tiles:
        per = {}
        for o in ORDERS:
            hh, vv = score(build(blob, o))
            per[o] = hh + vv
            acc[o][0] += hh
            acc[o][1] += vv
        wins[min(per, key=per.get)] += 1

    n = len(tiles)
    print("%-16s %10s %10s %10s %10s" % ("布局", "水平", "垂直", "合计", "逐瓦片胜出"))
    for o, (hh, vv) in sorted(acc.items(), key=lambda kv: kv[1][0] + kv[1][1]):
        print("%-16s %10.2f %10.2f %10.2f %6d/%d"
              % (o, hh / n, vv / n, (hh + vv) / n, wins[o], n))

    best = min(acc, key=lambda o: acc[o][0] + acc[o][1])
    print("\n最优布局：%s" % best)


if __name__ == "__main__":
    main()
