"""
framecheck.py -- 校验 DX12 真的画出了对的东西。

为什么需要它：渲染"没报错"不等于"画对了"。ra2view --offscreen 把后台缓冲
拷回 CPU 存成 build/frame.raw（RGBA8 原样），这里把它跟 Python 参考解码的
结果对拍 —— 两张图一致，才说明 MIX 解密、SHP 解码、调色板、纹理上传、
着色器查表这一整条链路都是对的。

用法：
  python tools/framecheck.py [raw路径] [宽] [高] [左上角x] [左上角y]
"""

from __future__ import annotations

import struct
import sys

sys.path.insert(0, r"E:\ra2source\tools")
import mixdump  # noqa: E402
from shpdump import Palette, ShpFile, _fetch  # noqa: E402

_RAMP = " .:-=+*#%@"


def load_raw(path, w, h):
    d = open(path, "rb").read()
    assert len(d) == w * h * 4, "大小 %d 与 %dx%d 不符" % (len(d), w, h)
    return d


def px(d, w, x, y):
    o = (y * w + x) * 4
    return d[o], d[o + 1], d[o + 2], d[o + 3]


def ascii_of(d, w, h, x0, y0, sw, sh, cols=100):
    rows = max(1, int(cols * sh / sw / 2.1))
    out = []
    for ry in range(rows):
        line = []
        for rx in range(cols):
            x = x0 + rx * sw // cols
            y = y0 + ry * sh // rows
            r, g, b, a = px(d, w, x, y)
            lum = (r * 299 + g * 587 + b * 114) // 1000
            line.append(_RAMP[min(9, lum * 10 // 256)])
        out.append("".join(line))
    return "\n".join(out)


def main() -> None:
    raw_path = sys.argv[1] if len(sys.argv) > 1 else r"E:\ra2source\build\frame.raw"
    W = int(sys.argv[2]) if len(sys.argv) > 2 else 1024
    H = int(sys.argv[3]) if len(sys.argv) > 3 else 768
    X0 = int(sys.argv[4]) if len(sys.argv) > 4 else 8
    Y0 = int(sys.argv[5]) if len(sys.argv) > 5 else 8

    d = load_raw(raw_path, W, H)
    print("读到 %s  %dx%d" % (raw_path, W, H))

    # ---- 1. 清屏色是否落在精灵之外 ----
    clear = px(d, W, 2, 2)
    print("清屏色（左上角 2,2）= rgba%s" % (clear,))
    if abs(clear[0] - 13) < 6 and abs(clear[1] - 13) < 6 and abs(clear[2] - 20) < 6:
        print("  -> 与设定的清屏色 (0.05,0.05,0.08)*255=(13,13,20) 一致")
    else:
        print("  [!] 与预期清屏色不符")

    # ---- 2. 跟 Python 参考解码逐像素对拍 ----
    m = mixdump.MixFile(r"D:\westwood\RA2YR\ra2md.mix")
    shp = ShpFile(_fetch(m, "AutoLoginQuery.shp"))
    pal = Palette(_fetch(m, "AutoLoginQuery.PAL"))
    f = shp.frames[0]
    print("\n参考：AutoLoginQuery.shp %dx%d 调色板 AutoLoginQuery.PAL" % (f.w, f.h))

    same = 0
    diff = 0
    first_diffs = []
    for y in range(min(f.h, H - Y0)):
        for x in range(min(f.w, W - X0)):
            idx = f.pixels[y * f.w + x]
            er, eg, eb = pal.map(idx)
            ea = 0 if idx == 0 else 255
            gr, gg, gb, ga = px(d, W, X0 + x, Y0 + y)
            if (er, eg, eb, ea) == (gr, gg, gb, ga):
                same += 1
            else:
                diff += 1
                if len(first_diffs) < 5:
                    first_diffs.append((x, y, (er, eg, eb, ea), (gr, gg, gb, ga)))
    total = same + diff
    print("逐像素比对 %d 个：一致 %d，不一致 %d（%.2f%%）"
          % (total, same, diff, 100.0 * same / max(1, total)))
    for x, y, e, g in first_diffs:
        print("   首个不一致 (%d,%d) 期望 rgba%s 实得 rgba%s" % (x, y, e, g))

    print("\nDX12 实际输出（精灵区域 %dx%d）：" % (f.w, f.h))
    print(ascii_of(d, W, H, X0, Y0, min(f.w, W - X0), min(f.h, H - Y0), 100))


if __name__ == "__main__":
    main()
