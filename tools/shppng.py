"""
shppng.py -- 把 MIX 里的 SHP 按帧渲成 PNG 联络表（contact sheet），肉眼验收解码。

为什么要有这个：ASCII 预览只能看轮廓，配色/抗锯齿/透明对不对必须看图。
PNG 用纯 zlib 手写，不依赖 Pillow。

用法：
  python shppng.py <SHP名> [帧范围 如 0:36] [调色板名]
    调色板省略时，用帧表里的 FrameColor 在所有 768 字节 PAL 里自动挑（同 C++ 的 Pick_Palette）

  python shppng.py COMPASS.SHP 0:4
"""

from __future__ import annotations

import os
import struct
import sys
import zlib

sys.path.insert(0, r"E:\ra2source\tools")
import mixdump  # noqa: E402
import shpdump  # noqa: E402
import shpdump3  # noqa: E402

MIXES = [r"D:\westwood\RA2YR\ra2.mix", r"D:\westwood\RA2YR\ra2md.mix"]
BG = (24, 24, 32)          # 透明处填的背景色
CELL_PAD = 2


# ---------------------------------------------------------------- PNG
def write_png(path, w, h, rgb_rows):
    """rgb_rows: 每行为 bytes，长度 w*3。"""
    raw = b"".join(b"\x00" + row for row in rgb_rows)

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


# ---------------------------------------------------------------- 调色板
def all_palettes():
    """扫 MIX 里所有 768 字节的候选调色板，返回 [(name, Palette)]。"""
    out = []
    tab = shpdump3.load_name_table()
    rev = {v.upper(): k for k, v in tab.items()}
    for path in MIXES:
        m = mixdump.MixFile(path)
        for owner, hid, off, size, d in mixdump.iter_leaves(m):
            if size != 768:
                continue
            try:
                out.append((rev.get(hid, "0x%08X" % hid),
                            shpdump.Palette(owner.read(off, 768))))
            except ValueError:
                pass
        m.close()
    return out


def pick_palette(frame, pals):
    """按 FrameColor 挑最接近的调色板（同 C++ ShpFile::Pick_Palette）。"""
    best, best_err = 0, 1e30
    acc = [0.0, 0.0, 0.0]
    n = 0
    for px in frame.pixels:
        if px:
            acc[0] += px
            n += 1
    if n == 0:
        return 0, 0.0
    # 用索引平均值做粗筛不够准，这里老老实实按 C++ 的算法来
    for i, (_nm, pal) in enumerate(pals):
        a = [0.0, 0.0, 0.0]
        for px in frame.pixels:
            if px == 0:
                continue
            r, g, b = pal.rgb[px]
            a[0] += r
            a[1] += g
            a[2] += b
        err = sum((a[k] / n - frame.color[k]) ** 2 for k in range(3))
        if err < best_err:
            best_err, best = err, i
    return best, best_err ** 0.5


# ---------------------------------------------------------------- 主流程
def main() -> None:
    if len(sys.argv) < 2:
        print(__doc__)
        return
    name = sys.argv[1]
    rng = sys.argv[2] if len(sys.argv) > 2 else "0:24"
    pal_name = sys.argv[3] if len(sys.argv) > 3 else None

    a, _, b = rng.partition(":")
    lo = int(a or 0)
    hi = int(b or (lo + 24))

    data = shpdump3.read_named(name)
    if not data:
        print("找不到", name)
        return
    shp = shpdump.ShpFile(data)
    hi = min(hi, shp.count)

    if pal_name:
        p = shpdump3.read_named(pal_name)
        if not p:
            print("找不到调色板", pal_name)
            return
        pals = [(pal_name, shpdump.Palette(p))]
    else:
        print("扫描候选调色板 ...", end="", flush=True)
        pals = all_palettes()
        print(" %d 个" % len(pals))

    frames = []
    for i in range(lo, hi):
        f = shp.frames[i]
        idx, err = (0, 0.0) if pal_name else pick_palette(f, pals)
        frames.append((i, f, pals[idx][1], pals[idx][0], err))
        print("  f%-4d %3dx%-3d flags=0x%02X  pal=%-16s 色差 %.2f"
              % (i, f.w, f.h, f.flags, pals[idx][0], err))

    # 网格布局
    cols = 8
    cw = max(f.w for _, f, _, _, _ in frames) + CELL_PAD
    ch = max(f.h for _, f, _, _, _ in frames) + CELL_PAD
    rows = (len(frames) + cols - 1) // cols
    W, H = cw * cols, ch * rows
    canvas = bytearray()
    for _ in range(H):
        canvas += bytes(BG) * W

    for k, (i, f, pal, pnm, err) in enumerate(frames):
        gx = (k % cols) * cw
        gy = (k // cols) * ch
        for y in range(f.h):
            row = bytearray(canvas[(gy + y) * W * 3:(gy + y) * W * 3 + W * 3])
            for x in range(f.w):
                v = f.pixels[y * f.w + x]
                if v == 0:
                    continue
                r, g, b = pal.rgb[v]
                o = (gx + x) * 3
                row[o] = r
                row[o + 1] = g
                row[o + 2] = b
            canvas[(gy + y) * W * 3:(gy + y) * W * 3 + W * 3] = row

    out = os.path.join(r"E:\ra2source\build",
                       "%s_%s.png" % (os.path.splitext(name)[0], rng.replace(":", "-")))
    os.makedirs(os.path.dirname(out), exist_ok=True)
    rgb_rows = [bytes(canvas[y * W * 3:(y + 1) * W * 3]) for y in range(H)]
    write_png(out, W, H, rgb_rows)
    print("已写出 %s  (%dx%d)" % (out, W, H))


if __name__ == "__main__":
    main()
