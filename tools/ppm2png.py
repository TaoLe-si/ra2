"""
ppm2png.py -- 把 ra2core --vxlit 出的 P6 PPM 转成 PNG，方便直接看。

为什么工具链里要有这一环：ra2core 刻意保持"零系统依赖"（连 zlib 都不链），
所以离屏渲染只能吐无压缩的 PPM；转 PNG 这件事交给 Python 更合适。

用法：
  python tools/ppm2png.py <in.ppm> <out.png> [背景RGB，默认 404048]
"""

from __future__ import annotations

import struct
import sys
import zlib


def read_ppm(path):
    d = open(path, "rb").read()
    # P6\n<w> <h>\n<max>\n<数据>
    assert d[:2] == b"P6", "不是 P6 PPM"
    i = 2
    fields = []
    while len(fields) < 3:
        while d[i:i + 1].isspace():
            i += 1
        if d[i:i + 1] == b"#":
            while d[i:i + 1] not in (b"\n", b""):
                i += 1
            continue
        j = i
        while not d[j:j + 1].isspace():
            j += 1
        fields.append(int(d[i:j]))
        i = j
    i += 1
    w, h, mx = fields
    return w, h, d[i:i + w * h * 3]


def write_png(path, w, h, rgb, bg=(0x40, 0x40, 0x48)):
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for x in range(w):
            o = (y * w + x) * 3
            raw += bytes(rgb[o:o + 3]) if len(rgb) >= o + 3 else bytes(bg)

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    open(path, "wb").write(png)


def main() -> None:
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    w, h, rgb = read_ppm(sys.argv[1])
    bg = (0x40, 0x40, 0x48)
    if len(sys.argv) > 3:
        bg = tuple(int(v) for v in sys.argv[3].split(","))
    write_png(sys.argv[2], w, h, rgb, bg)
    print("已写出 %s（%dx%d）" % (sys.argv[2], w, h))


if __name__ == "__main__":
    main()
