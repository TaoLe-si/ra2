"""vxlname.py -- 用 Westwood CRC 反查 VXL/HVA 的原始文件名。

MIX 索引里存的是 `CRC(大写文件名)`（实测锚点：ZEP.VXL=0x8C848DEE 正是基洛夫、
MTNK.VXL=0x3F85975D 对应 Grizzly、HTNK.HVA=0x81EF4670 在库），所以拿候选名
算 CRC 就能认人。

候选 = INI（rules/rulesmd/art/artmd/ai/aimd）里的节名 + 全部 [A-Z0-9_]{2,12}
token，再套后缀。RA2 的坦克炮塔是**独立文件** `<名>TUR.VXL`（实测
MTNKTUR/HTNKTUR/GTNKTUR/SREFTUR 四个都在 ra2.mix 里），所以后缀不能省。

置信度：`CRC(名.VXL)` 命中 + `CRC(名.HVA)` 也在 HVA 库里 = 双重命中；
MIX 里随机 id 落进 184 条表的概率约 184/2^32，基本排除碰撞。

用法:
  python tools/vxlname.py <vxl_id_hex> [hva_id_hex]     # 单个反查
  python tools/vxlname.py --dump [ra2|ra2md|both]       # 全表
"""
from __future__ import annotations

import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mixdump import westwood_crc                 # noqa: E402
from vxlhva_aabb import collect                  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
BUILD = os.path.join(HERE, "..", "build")
RA2DIR = r"D:\westwood\RA2YR"
INI_FILES = ["rulesmd.ini", "rules.ini", "artmd.ini", "art.ini",
             "aimd.ini", "ai.ini"]

# 后缀按"命中率高的先试"排；空串是车体本身。
SUFFIXES = ["", "TUR", "BARL", "BBL", "WO", "TURRET", "BARREL", "GUN", "MG"]

_TOKEN = re.compile(r"[A-Za-z0-9_]{2,12}")


def candidates() -> set[str]:
    """全部可能的名字（已大写）：节名 + 正文 token。"""
    names: set[str] = set()
    for ini in INI_FILES:
        p = os.path.join(RA2DIR, ini)
        if not os.path.exists(p):
            p = os.path.join(BUILD, ini)
            if not os.path.exists(p):
                continue
        with open(p, "r", encoding="latin-1", errors="replace") as f:
            for line in f:
                s = line.strip()
                if s.startswith("[") and s.endswith("]"):
                    names.add(s[1:-1].strip().upper())
                for t in _TOKEN.findall(s):
                    names.add(t.upper())
    return {n for n in names if not n.isdigit()}


def crc_of(name: str) -> int:
    return westwood_crc(name)


def build_index(pool: set[str]) -> dict[int, list[str]]:
    """CRC(名+后缀+.VXL) -> [(名, 后缀)]。"""
    idx: dict[int, list[str]] = {}
    for base in pool:
        for suf in SUFFIXES:
            idx.setdefault(crc_of(base + suf + ".VXL"), []).append(base + suf)
    return idx


def dump(mix_path: str, pool: set[str], idx: dict[int, list[str]]) -> None:
    vxls, hvas = collect(mix_path)
    hva_ids = {i for i, _ in hvas}
    by_crc: dict[int, str] = {}
    for i, b in vxls:
        by_crc[i] = b
    hit_v = hit_h = 0
    lines = []
    for vid in sorted(by_crc):
        names = idx.get(vid, [])
        pick = None
        for n in names:
            if crc_of(n + ".HVA") in hva_ids:
                pick = n
                hit_h += 1
                break
        if pick is None and names:
            pick = names[0]
            hit_v += 1
        if pick:
            lines.append("  %-14s VXL=0x%08X HVA=0x%08X%s"
                         % (pick, vid, crc_of(pick + ".HVA"),
                            "  *" if crc_of(pick + ".HVA") in hva_ids else ""))
        else:
            lines.append("  %-14s VXL=0x%08X" % ("<?>", vid))
    print("\n".join(lines))
    named = sum(1 for l in lines if "<?>" not in l)
    print("# %s: VXL %d 个，命名 %d 个（其中 HVA 双重命中 %d）"
          % (os.path.basename(mix_path), len(by_crc), named, hit_h))


def main() -> None:
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    pool = candidates()
    print("候选名 %d 个（节名+token），后缀 %d 种" % (len(pool), len(SUFFIXES)))

    if sys.argv[1] == "--dump":
        which = sys.argv[2] if len(sys.argv) > 2 else "ra2"
        idx = build_index(pool)
        if which in ("ra2", "both"):
            dump(os.path.join(RA2DIR, "ra2.mix"), pool, idx)
        if which in ("ra2md", "both"):
            dump(os.path.join(RA2DIR, "ra2md.mix"), pool, idx)
        return

    vid = int(sys.argv[1], 16)
    hid = int(sys.argv[2], 16) if len(sys.argv) > 2 else None
    hits = []
    for base in pool:
        for suf in SUFFIXES:
            if crc_of(base + suf + ".VXL") == vid:
                hits.append(base + suf)
    if not hits:
        print("0x%08X 没命中（%d 个候选 × %d 后缀全试过）" % (vid, len(pool), len(SUFFIXES)))
        sys.exit(2)
    for n in hits:
        ch = crc_of(n + ".HVA")
        mark = ""
        if hid is not None:
            mark = "   HVA 0x%08X %s" % (ch, "命中" if ch == hid else "≠0x%08X" % hid)
        print("  %-14s VXL=0x%08X%s" % (n, vid, mark))


if __name__ == "__main__":
    main()
