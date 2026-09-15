"""
framecheck.py -- 校验 DX12 真的画出了对的东西。

为什么需要它：渲染"没报错"不等于"画对了"。ra2view --offscreen 把后台缓冲
拷回 CPU 存成 build/frame.raw（RGBA8 原样），这里把它跟 Python 参考解码的
结果对拍 —— 两张图一致，才说明 MIX 解密、SHP 解码、调色板、纹理上传、
着色器查表这一整条链路都是对的。

用法：
  python tools/framecheck.py [SHP名] [PAL名] [帧号]
    默认 AutoLoginQuery.shp / AutoLoginQuery.PAL / 帧0（flags=0x2，未压缩+RLE 老样本）
    回归 flags=0x03 用：
      python tools/framecheck.py FULLFNT3.SHP FULLFNT3.PAL 30
      python tools/framecheck.py COMPASS.SHP FULLFNT3.PAL 0

  对应的 C++ 命令（先跑它再跑本脚本）：
    build\\ra2view.exe D:\\westwood\\RA2YR\\ra2.mix COMPASS.SHP FULLFNT3.PAL --offscreen --frame0
"""

from __future__ import annotations

import sys

sys.path.insert(0, r"E:\ra2source\tools")
import shpdump  # noqa: E402
import shpdump3  # noqa: E402

_RAW = r"E:\ra2source\build\frame.raw"
W, H = 1024, 768
X0, Y0 = 8, 8
_RAMP = " .:-=+*#%@"


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
            r, g, b, _a = px(d, w, x, y)
            lum = (r * 299 + g * 587 + b * 114) // 1000
            line.append(_RAMP[min(9, lum * 10 // 256)])
        out.append("".join(line))
    return "\n".join(out)


def main() -> None:
    shp_name = sys.argv[1] if len(sys.argv) > 1 else "AutoLoginQuery.shp"
    pal_name = sys.argv[2] if len(sys.argv) > 2 else "AutoLoginQuery.PAL"
    frame_no = int(sys.argv[3]) if len(sys.argv) > 3 else 0

    d = open(_RAW, "rb").read()
    assert len(d) == W * H * 4, "大小 %d 与 %dx%d 不符" % (len(d), W, H)
    print("读到 %s  %dx%d" % (_RAW, W, H))

    # ---- 1. 清屏色 ----
    clear = px(d, W, 2, 2)
    print("清屏色（左上角 2,2）= rgba%s  %s" % (clear, "OK"
          if abs(clear[0] - 13) < 6 and abs(clear[1] - 13) < 6 and abs(clear[2] - 20) < 6
          else "[!] 与预期 (13,13,20) 不符"))

    # ---- 2. 跟 Python 参考解码逐像素对拍 ----
    sd = shpdump3.read_named(shp_name)
    pd = shpdump3.read_named(pal_name)
    if not sd or not pd:
        print("找不到 %s / %s" % (shp_name, pal_name))
        return
    shp = shpdump.ShpFile(sd)
    pal = shpdump.Palette(pd)
    f = shp.frames[frame_no]
    print("\n参考：%s 帧%d %dx%d flags=0x%02X  调色板 %s"
          % (shp_name, frame_no, f.w, f.h, f.flags, pal_name))

    # 渲染器开了 alpha 混合（SRC_ALPHA/INV_SRC_ALPHA），所以透明像素**应该**
    # 露出清屏色，而不是"调色板里索引 0 的颜色 + alpha=0"。参考侧必须按混合后
    # 的结果比，否则会把正确的混合全判成不一致（这个坑踩过一次：
    # COMPASS 帧0 因为大片透明，被误报成 43%）。
    same = diff = 0
    n_opaque = 0
    first_diffs = []
    for y in range(min(f.h, H - Y0)):
        for x in range(min(f.w, W - X0)):
            idx = f.pixels[y * f.w + x]
            g4 = px(d, W, X0 + x, Y0 + y)
            if idx == 0:
                exp = clear                       # 混合后 = 清屏色
            else:
                er, eg, eb = pal.map(idx)
                exp = (er, eg, eb, 255)
                n_opaque += 1
            if exp == g4:
                same += 1
            else:
                diff += 1
                if len(first_diffs) < 5:
                    first_diffs.append((x, y, exp, g4))
    total = same + diff
    print("逐像素比对 %d 个（其中不透明 %d）：一致 %d，不一致 %d（%.2f%%）"
          % (total, n_opaque, same, diff, 100.0 * same / max(1, total)))
    for x, y, e, g4 in first_diffs:
        print("   不一致 (%d,%d) 期望 rgba%s 实得 rgba%s" % (x, y, e, g4))

    print("\nDX12 实际输出（精灵区域 %dx%d）：" % (f.w, f.h))
    print(ascii_of(d, W, H, X0, Y0, min(f.w, W - X0), min(f.h, H - Y0), 100))


if __name__ == "__main__":
    main()
