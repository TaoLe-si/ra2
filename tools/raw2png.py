"""raw2png.py -- 把 ra2view --offscreen 回读的 build/frame.raw 转成 PNG。

帧格式：RGBA8，宽高由参数给（默认 1024x768）。
用法: python tools/raw2png.py build/frame.raw out.png [w] [h]
"""
from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pcxdec import write_png          # noqa: E402


def main() -> None:
    src, dst = sys.argv[1], sys.argv[2]
    w = int(sys.argv[3]) if len(sys.argv) > 3 else 1024
    h = int(sys.argv[4]) if len(sys.argv) > 4 else 768
    d = open(src, "rb").read()
    if len(d) < w * h * 4:
        raise SystemExit("frame.raw 只有 %d 字节，装不下 %dx%d RGBA" % (len(d), w, h))
    # DX12 的 R8G8B8A8 回读是 RGBA 序，直接用。
    write_png(dst, w, h, d[: w * h * 4])
    print("[OK] %dx%d -> %s" % (w, h, dst))


if __name__ == "__main__":
    main()
