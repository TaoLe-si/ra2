"""
tmppara.py -- 验证"多 cell 模板按 TileX/TileY 等距铺排"这一假设。

背景：
  最初 TmpFile 的实现是把 cell 按 (bx*cw, by*ch) 简单堆叠，渲染 2x5 的模板时
  和参考实现差了 60% 的像素，图也糊成一团。翻出每个 cell 的 TileX/TileY 才看
  明白：多 cell 模板里 Tx/Ty 是 **cell 在模板整体画布里的像素偏移**（等距坐标），
  相邻 cell 的偏移是 (±cw/2, ch/2)，也就是菱形密铺 —— 不是矩形堆叠。

  Tx/Ty 联合 ExtraX/ExtraY 都落在同一套"模板画布"坐标里。画布原点要减去
  所有 cell 的 (min Tx, min Ty)，因为 Tx 可以为负（by 方向是往左下走）。

用法：
  python tools/tmppara.py <顶层mix> <0x归档ID> <序号>     渲染第 N 个可解析模板
"""

from __future__ import annotations

import sys

sys.path.insert(0, r"E:\ra2source\tools")
import mixdump   # noqa: E402
import shpdump3  # noqa: E402
import shppng    # noqa: E402
import tmpdump   # noqa: E402

# 底色必须是一个"调色板里不可能出现"的颜色，否则对账时会误判。
# 6 bit -> 8 bit 的展开只会产生 {4v} ∪ {4v+1} ∪ {4v+2} ∪ {4v+3} 里那些值，
# 其中低于 64 的只能是 4 的倍数 —— 所以 (1,2,3) 永远撞不上一个真实调色板色。
# 原先用的是 (24,24,32)，结果它正好等于 TEMPERAT.PAL 索引 226，
# 害得逐像素对账虚报 1.388% 不一致（306 个像素全是它）。
BG = (1, 2, 3)


def load_pal(name: str = "TEMPERAT.PAL"):
    d = shpdump3.read_named(name)
    return shppng.shpdump.Palette(d) if d else None


def render_block(raw: bytes, t: dict, pal, with_extra: bool = True,
                 extra_in_bounds: bool = False):
    """按 Tx/Ty 把整块模板拼出来。返回 (W, H, rows)。

    extra_in_bounds 控制画布原点是否把 extra 的负坐标也算进去 ——
    纯 cell 的包围盒是"引擎原样"的候选，含 extra 的包围盒是"看得更全"的候选，
    到底哪个对得看图。
    """
    bw, bh, cw, ch = t["bw"], t["bh"], t["cw"], t["ch"]
    cells = [c for c in t["cells"] if c]
    minx = min(c["tx"] for c in cells)
    miny = min(c["ty"] for c in cells)
    maxx = max(c["tx"] for c in cells) + cw
    maxy = max(c["ty"] for c in cells) + ch
    if extra_in_bounds and with_extra:
        for c in cells:
            if not (c["has_extra"] and 0 < c["extra_w"] < 512 and 0 < c["extra_h"] < 512):
                continue
            minx = min(minx, c["extra_x"])
            miny = min(miny, c["extra_y"])
            maxx = max(maxx, c["extra_x"] + c["extra_w"])
            maxy = max(maxy, c["extra_y"] + c["extra_h"])
    W, H = maxx - minx, maxy - miny
    rows = [[BG] * W for _ in range(H)]
    for c in cells:
        ox, oy = c["tx"] - minx, c["ty"] - miny
        canvas, used, total = tmpdump.iso_pixels(raw, c, cw, ch)
        for y in range(ch):
            for x in range(cw):
                v = canvas[y * cw + x]
                if v:
                    rows[oy + y][ox + x] = pal.rgb[v]
        if with_extra and c["has_extra"] and 0 < c["extra_w"] < 512 and 0 < c["extra_h"] < 512:
            st = c["base"] + c["extra_off"]
            need = st + c["extra_w"] * c["extra_h"]
            if need > len(raw):
                continue
            for y in range(c["extra_h"]):
                dy = c["extra_y"] - miny + y
                if not (0 <= dy < H):
                    continue
                for x in range(c["extra_w"]):
                    dx = c["extra_x"] - minx + x
                    if not (0 <= dx < W):
                        continue
                    v = raw[st + y * c["extra_w"] + x]
                    if v:
                        rows[dy][dx] = pal.rgb[v]
    return W, H, rows


def main() -> None:
    top, arch, pick = sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3])
    pal = load_pal()
    m = mixdump.MixFile(top)
    hit = [e for e in m.entries if e[0] == arch][0]
    sub = mixdump.open_nested(m, hit[1], hit[2])
    k = -1
    for h, off, size in sub.entries:
        raw = sub.read(off, size)
        if len(raw) < 16:
            continue
        try:
            t = tmpdump.parse(raw)
        except ValueError:
            continue
        k += 1
        if k != pick:
            continue
        cells = [c for c in t["cells"] if c]
        print("id=0x%08X  %dx%d 画布 %dx%d  非空 cell %d/%d"
              % (h, t["bw"], t["bh"], t["cw"], t["ch"], len(cells), len(t["cells"])))
        for c in cells:
            print("  cell%-2d base=%-6d Tx=%-5d Ty=%-4d bit=0x%02X extraXY=(%d,%d) extraWH=%dx%d"
                  % (c["idx"], c["base"], c["tx"], c["ty"], c["bitfield"],
                     c["extra_x"], c["extra_y"], c["extra_w"], c["extra_h"]))
        W, H, rows = render_block(raw, t, pal)
        print("平行四边形画布 %dx%d" % (W, H))
        out = r"E:\ra2source\build\tmp_para_0x%08X_%d.png" % (arch, pick)
        shppng.write_png(out, W, H, [b"".join(bytes(p) for p in r) for r in rows])
        print("已写出", out)
        return
    print("[x] 没有第 %d 个模板" % pick)


if __name__ == "__main__":
    main()
