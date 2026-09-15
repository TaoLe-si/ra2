"""uidump.py -- 按 ID 在（嵌套）MIX 里找 SHP/PAL，解出来写成 PNG。

为什么单独写：要复刻侧栏就得知道 SIDE1/TAB00/SIDEBTTN 这些件的**真实像素**
长什么样，光知道尺寸猜不出来。ra2view 只能一次渲一个、还得从 1024x768 的
帧里裁。这里直接把原生尺寸的图落盘。

用法：
  python tools/uidump.py D:\\westwood\\RA2YR\\ra2.mix 0xB10299AD:168x69,1 build/ui_side1.png
  python tools/uidump.py --batch D:\\westwood\\RA2YR\\ra2.mix build/ui
"""

from __future__ import annotations

import os
import struct
import sys
import zlib

sys.path.insert(0, r"E:\ra2source\tools")
import mixdump  # noqa: E402

# 从 db/mix-names.txt 抄下来的 UI 件 ID（ra2.mix）
UI_IDS = {
    "SIDE1": 0xB10299AD,
    "SIDE2": 0x3F8D9E4E,
    "SIDE2B": 0xB0259C24,
    "SIDE3": 0xF3279ED0,
    "SIDEBTTN": 0x2F43F08B,
    "SDBTNBKGD": 0x64459F0D,
    "SDBTNANM": 0xD5D666F2,
    "CREDITS": 0x7637D6E1,
    "POWER": 0xBCD41CAD,
    "RADAR": 0x93ECC2D3,
    "TAB00": 0x04D96724,
    "TAB01": 0xC87367BA,
    "TAB02": 0x46FC6059,
    "TAB03": 0x8A5660C7,
    "BUTTON00": 0x0D2B157D,
    "BUTTON01": 0x304B3CCD,
    "SIDEBAR_PAL": 0x5782B249,
    "SIDEFNT3": 0xAE6040D0,
}


def find_leaf(mix, want, depth=0):
    """深度优先找 id 所在的叶子。返回 (owner_mix, off, size)。"""
    for h, off, size in mix.entries:
        if h == want:
            return mix, off, size
    if depth >= 4:
        return None
    for h, off, size in mix.entries:
        sub = mixdump.open_nested(mix, off, size)
        if sub is None:
            continue
        r = find_leaf(sub, want, depth + 1)
        if r:
            return r
    return None


def read_entry(top, want):
    r = find_leaf(top, want)
    if r is None:
        return None
    owner, off, size = r
    return owner.read(off, size)


# ---- SHP (TS) 解码 ---------------------------------------------------------
def shp_frames(data):
    if len(data) < 8:
        return []
    _zero, w, h, n = struct.unpack_from("<HHHH", data, 0)
    out = []
    for i in range(n):
        o = 8 + i * 24
        if o + 24 > len(data):
            break
        # 24 字节帧头：4 个 u16 + u32 flags + 4 个 u8 颜色 + u32 保留 + u32 数据偏移
        (x, y, fw, fh, flags, c0, c1, c2, c3, _res,
         dofs) = struct.unpack_from("<HHHHIBBBBII", data, o)
        out.append(dict(x=x, y=y, w=fw, h=fh, flags=flags, ofs=dofs))
    return out


def decode_rle_zero(data, off, w, h):
    """RLE-Zero：每行开头 u16 = 该行字节数（含这 2 字节）；行内 00 -> 后一字节是重复数。"""
    px = bytearray(w * h)
    p = off
    for y in range(h):
        if p + 2 > len(data):
            break
        (linelen,) = struct.unpack_from("<H", data, p)
        if linelen < 2:
            break
        end = p + linelen
        p += 2
        line = bytearray()
        while p < end and p < len(data):
            b = data[p]
            p += 1
            if b != 0:
                line.append(b)
                continue
            if p >= end or p >= len(data):
                break
            cnt = data[p]
            p += 1
            line.extend(b"\x00" * cnt)
        px[y * w:y * w + min(w, len(line))] = line[:w]
    return px


def decode_raw(data, off, w, h):
    px = bytearray(w * h)
    n = min(w * h, len(data) - off)
    if n > 0:
        px[0:n] = data[off:off + n]
    return px


def write_png(path, rgb, w, h):
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw += rgb[y * w * 3:(y + 1) * w * 3]

    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload
                + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def load_pal768(data):
    """.PAL 是 6 位分量，按 (v<<2)|(v>>4) 展开。"""
    pal = bytearray(768)
    for i in range(256):
        for c in range(3):
            v = data[i * 3 + c] & 0x3F
            pal[i * 3 + c] = (v << 2) | (v >> 4)
    return pal


def to_rgb(px, w, h, pal):
    """索引 0 视为透明，铺洋红棋盘。"""
    out = bytearray(w * h * 3)
    for i in range(w * h):
        idx = px[i]
        if idx == 0:
            c = (255, 0, 255) if ((i % w // 8 + i // w // 8) & 1) == 0 else (160, 0, 160)
        else:
            c = (pal[idx * 3], pal[idx * 3 + 1], pal[idx * 3 + 2])
        out[i * 3:i * 3 + 3] = bytes(c)
    return out


def dump_one(top, name, sid, pal768, out_dir, frame=-1):
    data = read_entry(top, sid)
    if data is None:
        print("[x] 找不到 0x%08X (%s)" % (sid, name))
        return
    fr = shp_frames(data)
    if not fr:
        print("[x] %s 不是 SHP（%d 字节）" % (name, len(data)))
        return
    print("%-10s %d 字节  帧=%d  帧尺寸=%dx%d flags=0x%X"
          % (name, len(data), len(fr), fr[0]["w"], fr[0]["h"], fr[0]["flags"]))
    idxs = range(len(fr)) if frame < 0 else [frame]
    for i in idxs:
        f = fr[i]
        w, h = f["w"], f["h"]
        if f["flags"] & 0x02:
            px = decode_rle_zero(data, f["ofs"], w, h)
        else:
            px = decode_raw(data, f["ofs"], w, h)
        p = os.path.join(out_dir, "%s_f%d_%dx%d.png" % (name, i, w, h))
        write_png(p, to_rgb(px, w, h, pal768), w, h)


def main() -> None:
    args = sys.argv[1:]
    if len(args) < 3:
        print(__doc__)
        return
    top_path = args[0]
    out_dir = args[1]
    want = args[2:]
    os.makedirs(out_dir, exist_ok=True)
    top = mixdump.MixFile(top_path)

    pal_data = read_entry(top, UI_IDS["SIDEBAR_PAL"])
    if pal_data is None:
        sys.exit("[x] 找不到 SIDEBAR.PAL")
    pal768 = load_pal768(pal_data)
    print("[.] SIDEBAR.PAL 已载入")

    for w in want:
        if w == "ALL":
            for name, sid in UI_IDS.items():
                if name == "SIDEBAR_PAL":
                    continue
                dump_one(top, name, sid, pal768, out_dir)
            continue
        if ":" in w:
            name, sid = w.split(":", 1)
            dump_one(top, name, int(sid, 16), pal768, out_dir)
        else:
            dump_one(top, w, UI_IDS[w], pal768, out_dir)


if __name__ == "__main__":
    main()
