"""
imgcheck.py -- 没有眼睛的时候怎么判断"地形铺对了"。

离屏渲染出来的图没法目视，就只能用**结构判据**：
  1. 不透明像素的横向/纵向包络应该是菱形（等距地图的外轮廓就是菱形）
  2. 每一行的不透明像素数应该先增后减，峰值出现在地图最宽的那一行
  3. 颜色不能只有一种（那种情况说明调色板错了或全画成同一个色号）
  4. 相邻像素的相似度：地形是有大片同色的，纯噪声会接近 0

用法：
  python tools/imgcheck.py build/arena.ppm
"""

from __future__ import annotations

import struct
import sys


def read_ppm(path: str):
    f = open(path, "rb")
    assert f.readline().strip() == b"P6", "只认 P6"
    while True:
        line = f.readline()
        if not line.startswith(b"#"):
            break
    w, h = map(int, line.split())
    maxv = int(f.readline().strip())
    assert maxv == 255
    data = f.read(3 * w * h)
    f.close()
    return w, h, data


def main() -> None:
    path = sys.argv[1]
    w, h, d = read_ppm(path)
    print("%s  %dx%d" % (path, w, h))

    rows = [0] * h
    cols = [0] * w
    colors = {}
    for y in range(h):
        base = y * w * 3
        for x in range(w):
            o = base + x * 3
            r, g, b = d[o], d[o + 1], d[o + 2]
            if r == 0 and g == 0 and b == 0:
                continue                      # 背景（透明处写成了黑）
            rows[y] += 1
            cols[x] += 1
            c = (r >> 3, g >> 3, b >> 3)
            colors[c] = colors.get(c, 0) + 1

    nonzero_rows = [i for i, v in enumerate(rows) if v > 0]
    nonzero_cols = [i for i, v in enumerate(cols) if v > 0]
    print("不透明像素总数 %d" % sum(rows))
    print("纵向包络 %d..%d（%d 行有内容）" % (
        nonzero_rows[0], nonzero_rows[-1], len(nonzero_rows)))
    print("横向包络 %d..%d（%d 列有内容）" % (
        nonzero_cols[0], nonzero_cols[-1], len(nonzero_cols)))
    peak_row = max(range(h), key=lambda i: rows[i])
    print("最宽的一行 y=%d，%d 个像素（画布宽 %d，占 %.1f%%）" % (
        peak_row, rows[peak_row], w, 100.0 * rows[peak_row] / w))

    # 菱形判据：把行分成 8 段，段内平均宽度应先增后减
    seg = 8
    segw = []
    for s in range(seg):
        a = h * s // seg
        b = h * (s + 1) // seg
        segw.append(sum(rows[a:b]) / max(1, b - a))
    print("行宽八段均值：" + " ".join("%.0f" % v for v in segw))
    mid = seg // 2
    grow = all(segw[i] <= segw[i + 1] * 1.05 for i in range(mid))
    shrink = all(segw[i] >= segw[i + 1] * 0.95 for i in range(mid, seg - 1))
    print("菱形判据（先增后减）：%s" % ("通过" if grow and shrink else "不通过"))

    print("不同颜色（5bit 量化）%d 种，最常见 5 种占比：%s" % (
        len(colors),
        ["%.1f%%" % (100.0 * v / sum(rows))
         for _, v in sorted(colors.items(), key=lambda kv: -kv[1])[:5]]))

    # 相邻像素相似度：地形大片同色，噪声会接近 0
    same = 0
    tot = 0
    for y in range(0, h, 7):
        base = y * w * 3
        for x in range(0, w - 1, 3):
            o = base + x * 3
            if d[o] == 0 and d[o + 1] == 0 and d[o + 2] == 0:
                continue
            if d[o] == d[o + 3] and d[o + 1] == d[o + 4] and d[o + 2] == d[o + 5]:
                same += 1
            tot += 1
    print("水平相邻同色比例 %.1f%%（地形应偏高，噪声接近 0）" % (100.0 * same / max(1, tot)))


if __name__ == "__main__":
    main()
