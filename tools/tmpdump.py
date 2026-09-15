"""
tmpdump.py -- 解 RA2/TS 的 TMP 等距地形瓦片，渲成 PNG 用肉眼验收。

格式（ModEnc 权威 + 本项目实测反推）：
  FileHeader 16 字节：
    +0  int32 BlockWidth        横向 cell 数
    +4  int32 BlockHeight       纵向 cell 数
    +8  int32 BlockImageWidth   每个 cell 的画布宽（RA2 = 60）
    +12 int32 BlockImageHeight  每个 cell 的画布高（RA2 = 30）
  偏移表：BlockWidth*BlockHeight 个 u32（相对 FileHeader 起点），0 = 空 cell
  每个非空 cell 的 52 字节 TileCellHeader：
    +0  int32 TileX           +4  int32 TileY
    +8  u32 ExtraDataOffset   +12 u32 ZDataOffset    +16 u32 ExtraZDataOffset
    +20 int32 ExtraX          +24 int32 ExtraY
    +28 u32 ExtraWidth        +32 u32 ExtraHeight
    +36 u8  Bitfield（bit0=HasExtraData, bit1=HasZData, bit2=HasDamagedData）
    +37 u8[3] padding
    +40 u8 Height  +41 u8 LandType  +42 u8 SlopeType
    +43 u8[3] TopLeftRadarColor    +46 u8[3] BottomRightRadarColor
    +49 u8[3] padding
  之后：iso 像素 BlockImageWidth*BlockImageHeight/2 字节（等距菱形，见下）
        z 数据同长；extra 像素 ExtraWidth*ExtraHeight；extra-z 同长
  三个偏移都相对该 cell 的 TileCellHeader 起点，区段长度靠相减得到。

实测验收（1x1 瓦片，文件长度必须严丝合缝）：
  0x82A81C56  extra(30x16=480)  offsets 2332+20 ; 2352+480 = 2832 = size ✓
  0x8E387F79  extra(60x10=600)  2472+600 = 3072 = size ✓
  0x92D36910  extra(60x24=1440) 3312+1440 = 4752 = size ✓
  0x85BE8273  extra(59x30=1770) 3642+1770 = 5412 = size ✓
  0x89D457AB  extra(59x29=1711) 3583+1711 = 5294 = size ✓

等距菱形的 900 字节怎么铺开：
  60x30 的菱形，顶点在 (30,0) (60,15) (30,30) (0,15)，斜率 2。
  逐行像素宽度 = 2 + 4*min(y, 29-y)，行首 x = 29 - 2*min(y, 29-y)。
  合计 sum(2,6,...,58,58,...,2) = 900 ✓。数据就是按这个顺序紧排的。

用法：
  python tools/tmpdump.py <顶层mix> <0x归档ID> <序号...>      渲染单个瓦片
  python tools/tmpdump.py <顶层mix> <0x归档ID> --sheet 32      渲染前 N 个 1x1 瓦片
"""

from __future__ import annotations

import struct
import sys

sys.path.insert(0, r"E:\ra2source\tools")
import mixdump  # noqa: E402
import shppng  # noqa: E402

BG = (24, 24, 32)

TILE_HEADER = 52


def row_geometry(cw: int, ch: int):
    """返回 [(x, width), ...] 共 ch 行，描述菱形每一行的横向位置与宽度。

    60x30 的菱形顶点在 (30,0)(60,15)(30,30)(0,15)，边缘斜率 = cw/ch/2 = 2，
    所以往下走一行左右各外扩 cw/ch 个像素：
        宽度 w(y) = 2 + 2*(cw/ch)*min(y, ch-1-y)
        行首 x(y) = (cw/2 - 1) - (cw/ch)*min(y, ch-1-y)
    RA2: w = 2,6,...,58,58,...,6,2，合计 900 = 60*30/2 ✓
    """
    step = cw // ch
    rows = []
    for y in range(ch):
        d = min(y, ch - 1 - y)
        rows.append(((cw // 2 - 1) - step * d, 2 + 2 * step * d))
    return rows


def parse(data: bytes):
    bw, bh, cw, ch = struct.unpack_from("<4i", data, 0)
    if not (1 <= bw <= 10 and 1 <= bh <= 10):
        raise ValueError("BlockWidth/Height 不合理: %d x %d" % (bw, bh))
    if not (8 <= cw <= 256 and 8 <= ch <= 256):
        raise ValueError("BlockImage 不合理: %d x %d" % (cw, ch))
    n = bw * bh
    offs = struct.unpack_from("<%dI" % n, data, 16)
    cells = []
    for i, off in enumerate(offs):
        if off == 0:
            cells.append(None)
            continue
        base = off            # 相对 FileHeader 起点
        if base + TILE_HEADER > len(data):
            cells.append(None)
            continue
        (tx, ty, exoff, zoff, ezoff, exx, exy, exw, exh,
         bitfield) = struct.unpack_from("<2i3I2i2IB", data, base)
        pad1 = data[base + 37:base + 40]
        height, land, slope = data[base + 40], data[base + 41], data[base + 42]
        tl = data[base + 43:base + 46]
        br = data[base + 46:base + 49]
        pad2 = data[base + 49:base + 52]
        cells.append(dict(
            idx=i, base=base, tx=tx, ty=ty,
            extra_off=exoff, z_off=zoff, extraz_off=ezoff,
            extra_x=exx, extra_y=exy, extra_w=exw, extra_h=exh,
            bitfield=bitfield, has_extra=bool(bitfield & 1),
            has_z=bool(bitfield & 2), damaged=bool(bitfield & 4),
            pad1=pad1, pad2=pad2,
            height=height, land=land, slope=slope,
            tl_radar=tuple(tl), br_radar=tuple(br),
        ))
    return dict(bw=bw, bh=bh, cw=cw, ch=ch, cells=cells)


def iso_pixels(data: bytes, cell: dict, cw: int, ch: int):
    """把 900 字节的等距数据铺进 cw x ch 的画布（菱形外面是 None）。"""
    start = cell["base"] + TILE_HEADER
    n = cw * ch // 2
    blob = data[start:start + n]
    if len(blob) < n:
        raise ValueError("iso 数据不足：%d/%d" % (len(blob), n))
    canvas = [None] * (cw * ch)
    p = 0
    for y, (x, w) in enumerate(row_geometry(cw, ch)):
        for k in range(w):
            if p >= len(blob):
                break
            canvas[y * cw + x + k] = blob[p]
            p += 1
    return canvas, p, n


def render_tile(data, cell, cw, ch, pal, scale=2):
    canvas, used, total = iso_pixels(data, cell, cw, ch)
    W, H = cw * scale, ch * scale
    rows = [bytearray(bytes(BG) * W) for _ in range(H)]
    for y in range(ch):
        for x in range(cw):
            v = canvas[y * cw + x]
            if v is None or v == 0:
                continue
            r, g, b = pal.rgb[v]
            for dy in range(scale):
                row = rows[y * scale + dy]
                for dx in range(scale):
                    o = (x * scale + dx) * 3
                    row[o], row[o + 1], row[o + 2] = r, g, b
    return rows, used, total


def main() -> None:
    if len(sys.argv) < 3:
        print(__doc__)
        return
    top = sys.argv[1]
    arch = int(sys.argv[2], 16)
    rest = sys.argv[3:]

    sheet = 0
    idxs = []
    for a in rest:
        if a == "--sheet":
            sheet = 32
        elif sheet and sheet == 32 and a.isdigit():
            sheet = int(a)
        else:
            idxs.append(int(a))

    m = mixdump.MixFile(top)
    hit = [e for e in m.entries if e[0] == arch]
    if not hit:
        print("[x] 顶层没有 0x%08X" % arch)
        return
    _, off, size = hit[0]
    sub = mixdump.open_nested(m, off, size)
    if sub is None:
        print("[x] 0x%08X 不是 MIX" % arch)
        return

    import shpdump3
    pd = shpdump3.read_named("TEMPERAT.PAL")
    if not pd:
        print("[x] 找不到 TEMPERAT.PAL")
        return
    pal = shppng.shpdump.Palette(pd)

    print("TMP 文本里的瓦片数 = %d；选 %s" % (sub.count, idxs or "前 %d 个" % sheet))

    tiles = []
    if sheet:
        k = 0
        for i, (h, so, ss) in enumerate(sub.entries):
            d = sub.read(so, min(32, ss))
            if len(d) < 16 or d[:8] != b"\x01\x00\x00\x00\x01\x00\x00\x00":
                continue
            try:
                t = parse(sub.read(so, ss))
            except ValueError:
                continue
            tiles.append((i, h, t, sub.read(so, ss)))
            k += 1
            if k >= sheet:
                break
    else:
        for i in idxs:
            h, so, ss = sub.entries[i]
            d = sub.read(so, ss)
            tiles.append((i, h, parse(d), d))

    for i, h, t, d in tiles:
        c = t["cells"][0]
        print("  [%d] 0x%08X %dx%d 画布 %dx%d  bitfield=0x%02X extra=%s z=%s "
              "extraWH=%dx%d xy=(%d,%d) height=%d land=%d slope=%d radar=%s/%s"
              % (i, h, t["bw"], t["bh"], t["cw"], t["ch"], c["bitfield"],
                 c["has_extra"], c["has_z"], c["extra_w"], c["extra_h"],
                 c["extra_x"], c["extra_y"], c["height"], c["land"], c["slope"],
                 c["tl_radar"], c["br_radar"]))

    # 拼联络表
    scale = 2
    pad = 4
    cols = min(8, max(1, len(tiles)))
    rows_n = (len(tiles) + cols - 1) // cols
    cw = tiles[0][2]["cw"] * scale + pad
    ch = tiles[0][2]["ch"] * scale + pad
    W, H = cw * cols, ch * rows_n
    canvas = [bytes(BG) * W for _ in range(H)]
    for k, (i, h, t, d) in enumerate(tiles):
        gx = (k % cols) * cw
        gy = (k // cols) * ch
        trs, used, total = render_tile(d, t["cells"][0], t["cw"], t["ch"], pal, scale)
        for y, row in enumerate(trs):
            base = canvas[gy + y]
            ba = bytearray(base)
            ba[gx * 3:(gx + t["cw"] * scale) * 3] = row
            canvas[gy + y] = bytes(ba)

    out = r"E:\ra2source\build\tmp_%08X.png" % arch
    shppng.write_png(out, W, H, canvas)
    print("已写出 %s (%dx%d)" % (out, W, H))


if __name__ == "__main__":
    main()
