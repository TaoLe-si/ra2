"""
remapshpcheck.py -- 验证 SHP（步兵）走 remap 也生效。

和 remapcheck.py 同一套判据，只是素材走 SHP + 外部 .PAL 那条路
（步兵不是体素，没有 VXL 头自报区间，用的是实测的 16..31）。

用法：
  python tools/remapshpcheck.py GI.SHP unittem.pal
"""

from __future__ import annotations

import subprocess
import sys

VIEW = r"E:\ra2source\build\ra2view.exe"
MIX = r"D:\westwood\RA2YR\ra2.mix"
FRAME = r"E:\ra2source\build\frame.raw"


def render(shp: str, pal: str, remap: str | None):
    cmd = [VIEW, MIX, shp, pal, "--offscreen"]
    if remap:
        cmd += ["--remap", remap]
    r = subprocess.run(cmd, capture_output=True, text=True, cwd=r"E:\ra2source")
    if "[OK] 离屏渲染" not in r.stdout:
        print(r.stdout)
        raise SystemExit("[x] 渲染失败")
    return open(FRAME, "rb").read()


def main() -> None:
    shp = sys.argv[1] if len(sys.argv) > 1 else "GI.SHP"
    pal = sys.argv[2] if len(sys.argv) > 2 else "unittem.pal"
    a = render(shp, pal, "DarkRed")
    b = render(shp, pal, "DarkBlue")
    n = render(shp, pal, None)

    px = len(a) // 4
    diff_ab = diff_an = diff_bn = red_wins = blue_wins = 0
    for i in range(px):
        o = i * 4
        ra, ga, ba = a[o], a[o + 1], a[o + 2]
        rb, gb, bb = b[o], b[o + 1], b[o + 2]
        rn, gn, bn = n[o], n[o + 1], n[o + 2]
        if (ra, ga, ba) != (rb, gb, bb):
            diff_ab += 1
            if ra - ba > rb - bb:
                red_wins += 1
            elif rb - bb > ra - ba:
                blue_wins += 1
        if (ra, ga, ba) != (rn, gn, bn):
            diff_an += 1
        if (rb, gb, bb) != (rn, gn, bn):
            diff_bn += 1

    print("SHP %s / %s  画布 %d 像素" % (shp, pal, px))
    print("  红 vs 蓝   差异 %d 像素" % diff_ab)
    print("  红 vs 无remap 差异 %d" % diff_an)
    print("  蓝 vs 无remap 差异 %d" % diff_bn)
    print("  差异像素里偏红 %d / 偏蓝 %d" % (red_wins, blue_wins))

    ok = diff_ab > 0 and diff_an > 0 and diff_bn > 0 and red_wins > blue_wins
    print("[%s]" % ("OK" if ok else "FAIL"))
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
