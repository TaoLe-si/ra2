"""
shpdump.py -- 解 SHP(TS) 帧 + 套 RA2 调色板，导出 BMP 用肉眼验证。

为什么先出 Python 版：格式理解对不对，看一眼图就知道；改成 C++ 再调就是自虐。
Python 版跑通后照抄成 src/gfx/ShpFile.cpp / Palette.cpp。

用法：
  python tools/shpdump.py info <mix> <文件名>            # 打印头/帧表
  python tools/shpdump.py png  <mix> <shp名> <pal名> <out.bmp> [帧号]
  python tools/shpdump.py id   <mix> <0xSHPID> <0xPALID> <out.bmp> [帧号]

调色板：RA2 的 .PAL 是 768 字节，每色 3 字节 RGB，分量取值 0..63（6 bit），
需要 <<2 扩到 8 bit。这一点从 FSSLG.PAL 的首色 3f 00 3f（品红关键色）可以印证。
"""

from __future__ import annotations

import struct
import sys

sys.path.insert(0, r"E:\ra2source\tools")
import mixdump  # noqa: E402


# ---------------------------------------------------------------- 调色板
class Palette:
    def __init__(self, data: bytes):
        if len(data) != 768:
            raise ValueError("PAL 必须是 768 字节，实为 %d" % len(data))
        self.rgb = []
        for i in range(256):
            r, g, b = data[i * 3], data[i * 3 + 1], data[i * 3 + 2]
            # 6 bit -> 8 bit。用 (v<<2)|(v>>4) 而不是 v*4：后者最大只到 252。
            self.rgb.append((min(255, (r << 2) | (r >> 4)),
                             min(255, (g << 2) | (g >> 4)),
                             min(255, (b << 2) | (b >> 4))))

    def map(self, idx: int):
        return self.rgb[idx]


# ---------------------------------------------------------------- SHP(TS)
class ShpFrame:
    __slots__ = ("x", "y", "w", "h", "flags", "color", "offset", "pixels")


class ShpFile:
    """SHP(TS)：8 字节头 + 每帧 24 字节帧表 + 帧数据。

    帧表（moddingwiki 权威 + 实测）：
      +0  u16 X  +2 u16 Y  +4 u16 W  +6 u16 H
      +8  u32 flags（bit0=HasTransparency, bit1=UsesRle）
      +12 u8[4]  FrameColor（RGB，小地图用）
      +16 u32    Reserved
      +20 u32    DataOffset
    """

    def __init__(self, data: bytes):
        self.data = data
        zero, self.w, self.h, self.count = struct.unpack_from("<HHHH", data, 0)
        if zero != 0:
            raise ValueError("SHP(TS) 头两字节必须为 0，实为 0x%04X" % zero)
        self.frames = []
        for i in range(self.count):
            base = 8 + i * 24
            f = ShpFrame()
            (f.x, f.y, f.w, f.h, f.flags) = struct.unpack_from("<HHHHI", data, base)
            f.color = data[base + 12:base + 15]
            f.offset = struct.unpack_from("<I", data, base + 20)[0]
            # 帧数据的结束 = 下一帧的 offset，最后一帧到文件尾
            nxt = None
            if i + 1 < self.count:
                nxt = struct.unpack_from("<I", data, base + 24 + 20)[0]
            end = nxt if (nxt and nxt > f.offset) else len(data)
            f.pixels = self._decode(data[f.offset:end], f)
            self.frames.append(f)

    # ---- 帧数据解码 ----
    def _decode(self, src: bytes, f: ShpFrame) -> bytearray:
        w, h = f.w, f.h
        if f.flags & 0x02:                 # UsesRle：RLE-Zero，每行 u16 行长
            return self._rle_zero(src, w, h)
        return bytearray(src[:w * h])      # 未压缩

    @staticmethod
    def _rle_zero(src: bytes, w: int, h: int) -> bytearray:
        """Westwood RLE-Zero（TS 变体）：
           每行开头 u16 = 该行输入字节数（含这 2 字节）；
           行内遇到 0x00 则后一字节是"重复多少个透明像素"。"""
        out = bytearray()
        p = 0
        for _ in range(h):
            if p + 2 > len(src):
                break
            line = src[p] | (src[p + 1] << 8)
            p += 2
            end = min(p + max(0, line - 2), len(src))
            while p < end:
                v = src[p]
                p += 1
                if v == 0:
                    if p >= end:
                        break
                    out += bytes(src[p])   # 透明像素
                    p += 1
                else:
                    out.append(v)
        # 不足一帧就补透明，方便 caller 不用处理半帧
        if len(out) < w * h:
            out += bytes(w * h - len(out))
        return out[:w * h]


# ---------------------------------------------------------------- BMP 输出
def write_bmp(path: str, w: int, h: int, pixels, pal: Palette, scale: int = 1):
    """24-bit BMP，自下而上。scale 用于放大看得清。"""
    W, H = w * scale, h * scale
    row = (W * 3 + 3) & ~3
    body = bytearray(row * H)
    for y in range(H):
        sy = y // scale
        rowbuf = bytearray(row)
        for x in range(W):
            idx = pixels[sy * w + (x // scale)]
            r, g, b = pal.map(idx)
            o = x * 3
            rowbuf[o] = b
            rowbuf[o + 1] = g
            rowbuf[o + 2] = r
        body[(H - 1 - y) * row:(H - y) * row] = rowbuf

    hdr = bytearray()
    hdr += b"BM"
    hdr += struct.pack("<I", 14 + 40 + len(body))
    hdr += struct.pack("<HHI", 0, 0, 14 + 40)
    hdr += struct.pack("<IiiHHIIiiII", 40, W, H, 1, 24, 0, len(body), 0, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(hdr)
        f.write(body)


# ---------------------------------------------------------------- ASCII 预览
# BMP 是二进制、没法在终端里看，而"调色板套错了"这种错误一眼就能从缩略图看出来。
# 所以直接用亮度映射成字符，在终端里目视检查。
_RAMP = " .:-=+*#%@"


def ascii_preview(w: int, h: int, pixels, pal: Palette, cols: int = 96) -> str:
    rows = max(1, int(cols * h / w / 2.1))   # 终端字符高宽比约 2.1
    out = []
    for ry in range(rows):
        line = []
        for rx in range(cols):
            sx = min(w - 1, rx * w // cols)
            sy = min(h - 1, ry * h // rows)
            r, g, b = pal.map(pixels[sy * w + sx])
            lum = (r * 299 + g * 587 + b * 114) // 1000
            line.append(_RAMP[min(len(_RAMP) - 1, lum * len(_RAMP) // 256)])
        out.append("".join(line))
    return "\n".join(out)


def color_stats(pixels, pal: Palette, top: int = 8):
    from collections import Counter
    c = Counter(pixels)
    return [(idx, n, pal.map(idx)) for idx, n in c.most_common(top)]


# ---------------------------------------------------------------- CLI
def _fetch(m: mixdump.MixFile, token: str):
    """token 可以是文件名或 0x 开头的 CRC。"""
    if token.lower().startswith("0x"):
        want = int(token, 16)
        for owner, h, off, size, d in mixdump.iter_leaves(m):
            if h == want:
                return owner.read(off, size)
        raise SystemExit("归档里没有 id=%s" % token)
    r = m.find(token)
    if r:
        return m.read(*r)
    for owner, h, off, size, d in mixdump.iter_leaves(m):
        if mixdump.westwood_crc(token) == h:
            return owner.read(off, size)
    raise SystemExit("归档里没有 %s" % token)


def main() -> None:
    if len(sys.argv) < 4:
        print(__doc__)
        sys.exit(1)
    cmd, mix_path = sys.argv[1], sys.argv[2]
    m = mixdump.MixFile(mix_path)

    if cmd == "info":
        raw = _fetch(m, sys.argv[3])
        s = ShpFile(raw)
        print("%s  %dx%d  帧=%d" % (sys.argv[3], s.w, s.h, s.count))
        for i, f in enumerate(s.frames[:16]):
            print("  帧%-3d %d,%d %dx%d flags=0x%X color=%s off=%d 解出像素=%d/%d"
                  % (i, f.x, f.y, f.w, f.h, f.flags, f.color.hex(), f.offset,
                     len(f.pixels), f.w * f.h))
        return

    if cmd == "auto":
        # 自动挑调色板。判据来自格式本身：帧表里存了 FrameColor，
        # 即该帧**在正确调色板下**所有非透明像素的平均色（8 bit 分量）。
        # 所以遍历所有 768 字节的候选调色板，算平均色，取最接近的那个。
        # 这比"猜哪个 PAL 配哪个 SHP"可靠得多。
        shp_tok = sys.argv[3]
        frame = int(sys.argv[4]) if len(sys.argv) > 4 else 0
        s = ShpFile(_fetch(m, shp_tok))
        f = s.frames[frame]
        want = f.color  # 3 字节 RGB
        print("%s 帧%d %dx%d  帧自带平均色 rgb(%d,%d,%d)"
              % (shp_tok, frame, f.w, f.h, want[0], want[1], want[2]))

        cands = []
        for owner, hid, off, size, d in mixdump.iter_leaves(m):
            if size != 768:
                continue
            try:
                pal = Palette(owner.read(off, size))
            except Exception:
                continue
            n = 0
            acc = [0, 0, 0]
            for px in f.pixels:
                if px == 0:
                    continue                     # 透明不计入平均
                r, g, b = pal.map(px)
                acc[0] += r
                acc[1] += g
                acc[2] += b
                n += 1
            if n == 0:
                continue
            err = sum((acc[i] / n - want[i]) ** 2 for i in range(3))
            cands.append((err ** 0.5, hid, pal))
        cands.sort(key=lambda t: t[0])
        print("候选调色板 %d 个，最贴近的 5 个：" % len(cands))
        for err, hid, pal in cands[:5]:
            name = "0x%08X" % hid
            print("   %s  色差 %.1f" % (name, err))
        if not cands:
            return
        err, hid, pal = cands[0]
        print("\n用 0x%08X（色差 %.1f）渲染：\n" % (hid, err))
        print(ascii_preview(f.w, f.h, f.pixels, pal, 100))
        return

    if cmd == "ascii":
        shp_tok, pal_tok = sys.argv[3], sys.argv[4]
        frame = int(sys.argv[5]) if len(sys.argv) > 5 else 0
        cols = int(sys.argv[6]) if len(sys.argv) > 6 else 96
        s = ShpFile(_fetch(m, shp_tok))
        pal = Palette(_fetch(m, pal_tok))
        f = s.frames[frame]
        print("%s 帧%d %dx%d  调色板 %s" % (shp_tok, frame, f.w, f.h, pal_tok))
        print(ascii_preview(f.w, f.h, f.pixels, pal, cols))
        print("主要用色（索引, 像素数, RGB）:")
        for idx, n, rgb in color_stats(f.pixels, pal):
            print("   %3d  %8d  rgb%s" % (idx, n, rgb))
        return

    if cmd in ("png", "id"):
        shp_tok, pal_tok, out = sys.argv[3], sys.argv[4], sys.argv[5]
        frame = int(sys.argv[6]) if len(sys.argv) > 6 else 0
        scale = int(sys.argv[7]) if len(sys.argv) > 7 else 1
        s = ShpFile(_fetch(m, shp_tok))
        pal = Palette(_fetch(m, pal_tok))
        f = s.frames[frame]
        write_bmp(out, f.w, f.h, f.pixels, pal, scale)
        print("[OK] %s 帧%d %dx%d -> %s (放大 %dx)" % (shp_tok, frame, f.w, f.h, out, scale))
        return

    print(__doc__)


if __name__ == "__main__":
    main()
