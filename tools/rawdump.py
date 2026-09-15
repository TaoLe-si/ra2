"""rawdump.py -- 把裸 RGBA 转成 PNG，方便肉眼看。

`ra2game --vxlgpu` 会把 CPU 参考图和 GPU 烘焙图各 dump 成一份裸 RGBA
（build/vxl_cpu.raw / build/vxl_gpu.raw），本脚本把它们写成 PNG。
纯 zlib + struct 手写 PNG，不依赖 Pillow。

用法：
  python tools/rawdump.py build/vxl_cpu.raw 307 199 build/vxl_cpu.png
  python tools/rawdump.py --pair build/vxl_cpu.raw:307x199 build/vxl_gpu.raw:343x275
"""

from __future__ import annotations

import struct
import sys
import zlib

# 用一张洋红棋盘当底：透明像素在 PNG 里不该被看成黑，才分得清
# "这里真的什么都没有"和"这里画了纯黑"。
CHECKER_A = (255, 0, 255)
CHECKER_B = (160, 0, 160)


def write_png(path: str, rgb: bytes, w: int, h: int) -> None:
    raw = bytearray()
    for y in range(h):
        raw.append(0)  # filter type 0
        raw += rgb[y * w * 3:(y + 1) * w * 3]

    def chunk(tag: bytes, data: bytes) -> bytes:
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def convert(src: str, w: int, h: int, dst: str) -> None:
    with open(src, "rb") as f:
        data = f.read()
    need = w * h * 4
    if len(data) < need:
        sys.exit(f"[x] {src} 只有 {len(data)} 字节，至少要 {need}")
    out = bytearray(w * h * 3)
    for y in range(h):
        for x in range(w):
            o = (y * w + x) * 4
            d = (y * w + x) * 3
            if data[o + 3] == 0:
                c = CHECKER_A if ((x // 8 + y // 8) & 1) == 0 else CHECKER_B
                out[d], out[d + 1], out[d + 2] = c
            else:
                out[d] = data[o]
                out[d + 1] = data[o + 1]
                out[d + 2] = data[o + 2]
    write_png(dst, bytes(out), w, h)
    print(f"{src} -> {dst} ({w}x{h})")


def main() -> None:
    a = sys.argv[1:]
    if not a:
        print(__doc__)
        return
    if a[0] == "--pair":
        for spec in a[1:]:
            path, dim = spec.split(":")
            w, h = (int(v) for v in dim.lower().split("x"))
            convert(path, w, h, path.rsplit(".", 1)[0] + ".png")
        return
    if len(a) != 4:
        print(__doc__)
        return
    convert(a[0], int(a[1]), int(a[2]), a[3])


if __name__ == "__main__":
    main()
