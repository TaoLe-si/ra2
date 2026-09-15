"""
tmpregress.py -- 跑遍 ra2.mix 里所有"明文 MIX"归档，把 C++ 与参考实现的
TMP 渲染哈希逐条对账。这是 TMP + 明文 MIX 两条链路的全量回归。

用法：
  python tools/tmpregress.py [顶层mix] [调色板ID]

为什么只跑明文归档：
  加密归档（flags 带 0x00020000）不是地形，里面的 SHP/INI 走别的路径。
  地形素材（ISOGEN / GENERIC / 各气候 tileset）全在明文归档里 ——
  这正是之前漏掉、导致地形整个不可见的那一类。
"""

from __future__ import annotations

import subprocess
import sys

sys.path.insert(0, r"E:\ra2source\tools")
import mixdump   # noqa: E402

EXE = r"E:\ra2source\build\ra2core.exe"
PY = r"C:\Users\Administrator\.workbuddy\binaries\python\versions\3.13.12\python.exe"


def main() -> None:
    top = sys.argv[1] if len(sys.argv) > 1 else r"D:\westwood\RA2YR\ra2.mix"
    pal = sys.argv[2] if len(sys.argv) > 2 else "0x9C58DE40"   # TEMPERAT.PAL

    m = mixdump.MixFile(top)
    plains = []
    for h, off, size in m.entries:
        sub = mixdump.open_nested(m, off, size)
        if sub is not None and not sub.encrypted:
            plains.append((h, sub.count))

    print("明文归档 %d 个，共 %d 条目" % (len(plains), sum(c for _, c in plains)))
    fails = 0
    total_tiles = 0
    for h, cnt in plains:
        cpp = subprocess.run(
            [EXE, "--tmphash", top, "0x%08X" % h, pal],
            capture_output=True, text=True, encoding="utf-8", errors="replace")
        if cpp.returncode != 0:
            print("  [x] 0x%08X C++ 退出码 %d：%s" % (h, cpp.returncode, cpp.stdout[:200]))
            fails += 1
            continue
        open(r"E:\ra2source\build\_reg_cpp.txt", "w", encoding="utf-8").write(cpp.stdout)

        py = subprocess.run(
            [PY, r"E:\ra2source\tools\tmphash.py", top, "0x%08X" % h,
             "--check", r"E:\ra2source\build\_reg_cpp.txt"],
            capture_output=True, text=True, encoding="utf-8", errors="replace")
        lines = [l for l in cpp.stdout.splitlines() if l and not l.startswith("#")]
        total_tiles += len(lines)
        head = py.stdout.strip().splitlines()[-1] if py.stdout.strip() else "(空)"
        tag = "OK " if py.returncode == 0 else "[x]"
        if py.returncode != 0:
            fails += 1
        print("  %s 0x%08X 条目=%-5d TMP=%-5d %s" % (tag, h, cnt, len(lines), head))

    print("\n合计对账 %d 个模板；失败归档 %d 个" % (total_tiles, fails))
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
