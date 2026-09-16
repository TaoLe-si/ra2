"""
shadowcheck.py -- 验证体素地面投影阴影。

无窗口时的判据全靠 stdout 那行自检输出：
   体素画布 WxH 本体像素 N，阴影 S（露出 V，P% 被本体盖住）

判据：
  1) 开阴影后画布**变大** —— bbox 把地面落点一起纳入了，不然阴影会被裁。
  2) 阴影像素 S > 0，露出 V > 0（全被本体盖住等于看不见影子）。
  3) 露出 V < S（影子不可能全在模型外面，那就不是投影而是另一个东西）。
  4) 换个光方位，露出面积必须跟着变 —— 影子是"沿光方向投"的，
     不是固定贴在脚下的一个团。

用法：
  python tools/shadowcheck.py MTNK YTNK HTNK
"""

from __future__ import annotations

import re
import subprocess
import sys

VIEW = r"E:\ra2source\build\ra2view.exe"
MIX = r"D:\westwood\RA2YR\ra2.mix"
MIX2 = r"D:\westwood\RA2YR\ra2md.mix"

RE_CANVAS = re.compile(r"体素画布 (\d+)x(\d+) 本体像素 (\d+)"
                       r"(?:，阴影 (\d+)（露出 (\d+)，(\d+)% 被本体盖住）)?")


def run(unit: str, extra: list[str] | None = None) -> dict:
    cmd = [VIEW, MIX, "--addmix", MIX2, "--unit", unit, "--offscreen"]
    cmd += extra or []
    r = subprocess.run(cmd, capture_output=True, text=True, cwd=r"E:\ra2source")
    if "[OK] 离屏渲染" not in r.stdout:
        print(r.stdout[-800:])
        raise SystemExit("[x] %s 渲染失败" % unit)
    m = RE_CANVAS.search(r.stdout)
    if not m:
        print(r.stdout[-800:])
        raise SystemExit("[x] %s 没有自检输出" % unit)
    g = lambda i: int(m.group(i)) if m.group(i) else 0
    return {"w": g(1), "h": g(2), "body": g(3), "shadow": g(4), "visible": g(5)}


def main() -> None:
    units = sys.argv[1:] or ["MTNK", "YTNK"]
    all_ok = True
    for unit in units:
        base = run(unit)
        sh = run(unit, ["--shadow"])
        # 换个光方位：方位反转，影子该跑到另一侧，露出面积得变
        alt = run(unit, ["--shadow", "--light", "1,-1,2"])
        print("单位 %s" % unit)
        print("  无阴影  画布 %dx%d 本体 %d" % (base["w"], base["h"], base["body"]))
        print("  有阴影  画布 %dx%d 本体 %d 阴影 %d 露出 %d"
              % (sh["w"], sh["h"], sh["body"], sh["shadow"], sh["visible"]))
        print("  换光方位 阴影 %d 露出 %d" % (alt["shadow"], alt["visible"]))

        # 开阴影后 bbox 会变大，窗口 fit 的缩放跟着变一点点，本体像素数
        # 会有 <1% 的浮动（量化所致）。所以是"近似不变"而不是"严格相等"。
        drift = abs(sh["body"] - base["body"]) * 100.0 / max(1, base["body"])
        checks = [
            ("开阴影后画布变大（bbox 纳入了地面落点）",
             sh["w"] * sh["h"] > base["w"] * base["h"]),
            ("阴影像素 > 0", sh["shadow"] > 0),
            ("有露出来的影子（没被本体全盖住）", sh["visible"] > 0),
            ("露出 < 阴影总数", sh["visible"] < sh["shadow"]),
            ("本体像素数基本不变（%.2f%% 浮动）" % drift, drift < 5.0),
            ("换光方位后露出面积变化", alt["visible"] != sh["visible"]),
        ]
        for name, good in checks:
            print("    %s %s" % ("✓" if good else "✗", name))
            if not good:
                all_ok = False
    print("[%s]" % ("OK" if all_ok else "FAIL"))
    sys.exit(0 if all_ok else 1)


if __name__ == "__main__":
    main()
