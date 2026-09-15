"""bmp2png.py -- 把 ra2core 写出的 24 位 BMP 转成 PNG，方便直接预览。

ra2core 只写 BMP（不想为了测试引 zlib），但 BMP 在对话里没法直接看。
用法: python tools/bmp2png.py in.bmp out.png
"""
from __future__ import annotations

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pcxdec import write_png          # noqa: E402


def main() -> None:
    src, dst = sys.argv[1], sys.argv[2]
    d = open(src, "rb").read()
    if d[:2] != b"BM":
        raise SystemExit("不是 BMP")
    pix = struct.unpack_from("<I", d, 10)[0]
    w, h = struct.unpack_from("<ii", d, 18)
    bpp = struct.unpack_from("<H", d, 28)[0]
    if bpp != 24:
        raise SystemExit("只支持 24 位，实际 %d" % bpp)
    stride = (w * 3 + 3) & ~3
    out = bytearray(w * h * 4)
    for y in range(h):
        row = pix + (h - 1 - y) * stride          # BMP 自下而上
        for x in range(w):
            b, g, r = d[row + x * 3], d[row + x * 3 + 1], d[row + x * 3 + 2]
            s = (y * w + x) * 4
            out[s] = r
            out[s + 1] = g
            out[s + 2] = b
            out[s + 3] = 255
    write_png(dst, w, h, bytes(out))
    print("[OK] %dx%d -> %s" % (w, h, dst))


if __name__ == "__main__":
    main()
