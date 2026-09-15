"""
tmpcmp.py -- 把 C++ 写的 BMP 和 Python 参考实现逐像素对账。

为什么需要它：
  C++ 的 TmpFile 是照抄 tmpdump.py 写的，但"照抄"不等于"对"。唯一可信的验收
  方式是同一条数据两边各算一遍，像素级比对 —— 差一个像素就说明抄错了。
  实测抓出过一个真 bug：多 cell 模板按 (bx*cw, by*ch) 堆叠是错的，
  必须用 TileX/TileY 的等距倾斜铺排（当时差异 60.278%，最大分量差 255）。

用法：
  python tools/tmpcmp.py <顶层mix> <0x归档ID> <第几个模板> <bmp路径> [PAL名]
"""

from __future__ import annotations

import struct
import sys

sys.path.insert(0, r"E:\ra2source\tools")
import mixdump   # noqa: E402
import shppng    # noqa: E402
import tmpdump   # noqa: E402
import tmppara   # noqa: E402


def read_bmp(path: str):
    d = open(path, "rb").read()
    assert d[:2] == b"BM", "不是 BMP"
    off = struct.unpack_from("<I", d, 10)[0]
    w = struct.unpack_from("<i", d, 18)[0]
    h = struct.unpack_from("<i", d, 22)[0]
    stride = (w * 3 + 3) & ~3
    px = []
    for y in range(h):
        base = off + (h - 1 - y) * stride       # BMP 是自下而上存的
        px.append([tuple(d[base + x * 3: base + x * 3 + 3][::-1]) for x in range(w)])
    return w, h, px


def main() -> None:
    top = sys.argv[1]
    arch = int(sys.argv[2], 16)
    pick = int(sys.argv[3])
    bmp = sys.argv[4]
    palname = sys.argv[5] if len(sys.argv) > 5 else "TEMPERAT.PAL"

    pal = tmppara.load_pal(palname)
    if pal is None:
        print("[x] 找不到调色板 %s" % palname)
        return

    m = mixdump.MixFile(top)
    hit = [e for e in m.entries if e[0] == arch][0]
    sub = mixdump.open_nested(m, hit[1], hit[2])

    k = -1
    target = None
    for h, off, size in sub.entries:
        raw = sub.read(off, size)
        if len(raw) < 16:
            continue
        try:
            t = tmpdump.parse(raw)
        except ValueError:
            continue
        k += 1
        if k == pick:
            target = (h, raw, t)
            break
    if target is None:
        print("[x] 找不到第 %d 个模板" % pick)
        return
    h, raw, t = target

    W, H, rows = tmppara.render_block(raw, t, pal, True)
    w2, h2, bmppx = read_bmp(bmp)
    print("模板 id=0x%08X  %dx%d 画布 %dx%d" % (h, t["bw"], t["bh"], t["cw"], t["ch"]))
    if (w2, h2) != (W, H):
        print("[x] 画布尺寸不符：BMP %dx%d vs Python %dx%d" % (w2, h2, W, H))
        return

    # C++ 那边画布外的像素是 alpha=0；BMP 没有 alpha，写出来就是黑。
    # Python 参考实现填的是 BG 底色。比对时把 BG 归一化成黑，否则
    # 满屏"不一致"全是底色差异，真差异会被淹掉（实测会虚报 97%）。
    diff = 0
    worst = 0
    for y in range(H):
        for x in range(W):
            a, b = rows[y][x], bmppx[y][x]
            if a == tmppara.BG:
                a = (0, 0, 0)
            if a != b:
                diff += 1
                worst = max(worst, max(abs(a[i] - b[i]) for i in range(3)))
    tot = W * H
    print("逐像素比对：%d/%d 不一致（%.3f%%），最大分量差 %d"
          % (diff, tot, 100.0 * diff / tot, worst))

    out = bmp.replace(".bmp", "_py.png")
    shppng.write_png(out, W, H, [b"".join(bytes(c) for c in r) for r in rows])
    print("已写出 %s 供对照" % out)


if __name__ == "__main__":
    main()
