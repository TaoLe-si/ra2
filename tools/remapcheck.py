"""
remapcheck.py -- 验证阵营色 remap 真的作用到了渲染结果上。

无 GUI 时的判据：同一个单位分别用两个阵营色离屏渲染，逐像素比。
只有"索引落在 remap 区间"的那些像素该变，其余一个字节都不能动。

用法：
  python tools/remapcheck.py MTNK
"""

from __future__ import annotations

import subprocess
import sys

VIEW = r"E:\ra2source\build\ra2view.exe"
MIX = r"D:\westwood\RA2YR\ra2.mix"
MIX2 = r"D:\westwood\RA2YR\ra2md.mix"
FRAME = r"E:\ra2source\build\frame.raw"


def render(unit: str, remap: str | None):
    cmd = [VIEW, MIX, "--addmix", MIX2, "--unit", unit, "--offscreen"]
    if remap:
        cmd += ["--remap", remap]
    r = subprocess.run(cmd, capture_output=True, text=True, cwd=r"E:\ra2source")
    if "[OK] 离屏渲染" not in r.stdout:
        print(r.stdout)
        raise SystemExit("[x] 渲染失败")
    return open(FRAME, "rb").read()


def main() -> None:
    unit = sys.argv[1] if len(sys.argv) > 1 else "MTNK"
    a = render(unit, "DarkRed")
    b = render(unit, "DarkBlue")
    n = render(unit, None)

    if len(a) != len(b) or len(a) != len(n):
        raise SystemExit("[x] 三帧大小不一致")

    px = len(a) // 4
    diff_ab = 0
    diff_an = 0
    diff_bn = 0
    # 统计"变了"的像素里，红色版是不是真的偏红、蓝色版偏蓝
    red_wins = 0
    blue_wins = 0
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

    opaque = sum(1 for i in range(px) if n[i * 4 + 3] != 0)
    print("单位 %s  画布 %d 像素（不透明 %d）" % (unit, px, opaque))
    print("  红 vs 蓝   差异 %d 像素（%.2f%%）" % (diff_ab, 100.0 * diff_ab / px))
    print("  红 vs 无remap 差异 %d 像素" % diff_an)
    print("  蓝 vs 无remap 差异 %d 像素" % diff_bn)
    print("  差异像素里偏红 %d / 偏蓝 %d" % (red_wins, blue_wins))

    ok = True
    checks = [
        ("红蓝两版必须有差异", diff_ab > 0),
        ("红版与不 remap 必须有差异", diff_an > 0),
        ("蓝版与不 remap 必须有差异", diff_bn > 0),
        ("改动量应当只占少数（只有阵营色区变）", diff_ab < px * 0.5),
        ("红色版在差异像素上整体更红", red_wins > blue_wins),
    ]
    for name, good in checks:
        print("  %s %s" % ("✓" if good else "✗", name))
        if not good:
            ok = False
    print("[%s]" % ("OK" if ok else "FAIL"))
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
