"""
mapprobe.py -- 侦察 RA2/YR 地图包（.mmx / .yro / .map）的结构。

已知（2026-09-15 实测）：
  .mmx / .yro 不是裸地图，而是 **加密 MIX 归档**（flags=0x00030000，
  即 0x00020000 加密 + 0x00010000 尾 SHA1）。所以直接用 mixdump.MixFile 打开。

本脚本目标：
  1. 列出归档内的条目，并把 CRC 反查成文件名（候选名来自规则 + 归档内文本）
  2. 找到地图正文（.map / .ini 类）并打印 [Map] 段的关键字段
  3. 把 IsoMapPack5 之类的大段按 base64 解出来，报告长度，供后续解压算法验证

用法：
  python tools/mapprobe.py list  D:\\westwood\\RA2YR\\Arena.mmx
  python tools/mapprobe.py dump  D:\\westwood\\RA2YR\\Arena.mmx <out目录>
"""

from __future__ import annotations

import base64
import os
import struct
import sys

sys.path.insert(0, r"E:\ra2source\tools")
import mixdump  # noqa: E402


# 地图包里可能出现的名字。MIX 只存 CRC，所以只能拿候选名去撞。
CANDIDATE_NAMES = []
for stem in ("ARENA", "ICE_AGE", "HILLS", "MONSTERM", "MAP", "MAPS", "ARENAMD"):
    pass


def guess_names(m: mixdump.MixFile, map_stem: str):
    """把归档里的 ID 反查成名字。候选来自：地图名 + 常见后缀 / 常见文件名。"""
    exts = ("map", "mpr", "ini", "yro", "mmx", "txt", "bin", "sno", "tem")
    cands = set()
    stems = {map_stem.upper(), "MAP", "MAPS", "SCENARIO", "SCENARIOMD"}
    for s in list(stems):
        for e in exts:
            cands.add("%s.%s" % (s, e))
            cands.add("%s.%s" % (s, e.upper()))
    # 通用文件名
    for n in ("MAP.MAP", "map.map", "ARENA.MAP", "mission.ini", "MISSION.INI",
              "rules.ini", "RULES.INI", "ai.ini", "AI.INI", "art.ini",
              "temperat.ini", "TEMPERAT.INI", "theaters.ini", "THEATERS.INI",
              "overlay.ini", "OVERLAY.INI", "terrain.ini", "TERRAIN.INI"):
        cands.add(n)

    table = {}
    for name in cands:
        c = mixdump.westwood_crc(name)
        if c in [h for h, _, _ in m.entries]:
            table[c] = name
    return table


def main() -> None:
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    cmd, path = sys.argv[1], sys.argv[2]
    m = mixdump.MixFile(path)
    stem = os.path.splitext(os.path.basename(path))[0]
    print("%s  flags=0x%08X 条目=%d 数据区=%d" % (path, m.flags, m.count, m.data_size))
    table = guess_names(m, stem)

    if cmd == "list":
        for h, off, size in m.entries:
            head = m.head(off, 24)
            print("  id=0x%08X  %-16s size=%-8d  head=%s" % (
                h, table.get(h, "?"), size, head[:24].hex(" ")))
            if size < 4096:
                try:
                    t = m.read(off, size).decode("ascii", "ignore")
                except Exception:
                    t = ""
                printable = sum(1 for ch in t if ch.isprintable() or ch in "\r\n")
                if printable > len(t) * 0.9 and len(t) > 0:
                    print("      ---- 文本前 400 字符 ----")
                    for line in t[:400].splitlines():
                        print("      | " + line)
        return

    if cmd == "dump" and len(sys.argv) >= 4:
        outdir = sys.argv[3]
        os.makedirs(outdir, exist_ok=True)
        for h, off, size in m.entries:
            name = table.get(h, "%08X.bin" % h)
            open(os.path.join(outdir, name), "wb").write(m.read(off, size))
            print("  -> %-20s %d 字节" % (name, size))
        return

    print(__doc__)


if __name__ == "__main__":
    main()
