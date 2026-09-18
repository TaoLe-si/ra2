"""
inikeys.py -- 把 rules/art/ai/sound/theme 五族 INI 里**真实出现的键**清点出来。

为什么要这一趟：P2 剩下的活是"把 INI 全量填进 TechnoTypeClass"。
在此之前必须先知道**原版到底写了哪些键**，否则结构体字段就是拍脑袋编的
（工程纪律见 docs/fabrication-audit.md：无证据链的字段 = 编造）。

所以本脚本只做一件事：**清点**。不做语义解释，不猜缺省值。
输出的每一条都能在 INI 里 grep 到，这就是证据。

用法：
  python tools/inikeys.py <游戏目录>
  python tools/inikeys.py <游戏目录> --typekeys      # 只打单位段键表
  python tools/inikeys.py <游戏目录> --sections      # 只打"段名 -> 落在哪个文件"
"""

from __future__ import annotations

import os
import struct
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__))))
from mixdump import MixFile, open_nested  # noqa: E402

SPACE = " \t\r\n\f\v"

# 挂载顺序（src/core/GameVersion.h）：ra2.mix -> ra2md.mix -> expandmd01..99 -> thememd.mix
def mount_order(gamedir: str) -> list[str]:
    out = []
    for n in ("ra2.mix", "ra2md.mix"):
        p = os.path.join(gamedir, n)
        if os.path.exists(p):
            out.append(p)
    for i in range(1, 100):
        p = os.path.join(gamedir, "expandmd%02d.mix" % i)
        if os.path.exists(p):
            out.append(p)
    p = os.path.join(gamedir, "thememd.mix")
    if os.path.exists(p):
        out.append(p)
    return out


def read_any(archives, name: str):
    """按挂载顺序找 name，命中即返回 (bytes, 归档名)。"""
    for m in archives:
        if m is None:
            continue
        hit = m.find(name)
        if hit is not None:
            off, size = hit
            return m.read(off, size), m.path
    return None, None


def parse(text: bytes):
    """极简 INI 解析 —— 与 src/data/Ini.cpp 同一套规则，保留大小写。

    返回 (sections, entries_per_section)。重复键**都保留**（清点要看到全部）。
    """
    sections: list[list] = []
    index: dict[str, int] = {}
    cur = None
    malformed = 0
    for raw in text.decode("latin-1").split("\n"):
        s = raw.strip(SPACE)
        if not s or s[0] == ";":
            continue
        if s[:2] == "//":
            continue
        if s[0] == "[":
            close = s.find("]", 1)
            name = (s[1:] if close < 0 else s[1:close]).strip(SPACE)
            if not name:
                malformed += 1
                continue
            key = name.upper()
            if key in index:
                cur = sections[index[key]]
                continue
            index[key] = len(sections)
            cur = []
            sections.append(cur)
            continue
        eq = s.find("=")
        if eq < 0:
            malformed += 1
            continue
        if cur is None:
            continue
        # 值同样剥掉行内注释：第一个 ';' 之后全丢
        v = s[eq + 1:]
        semi = v.find(";")
        if semi >= 0:
            v = v[:semi]
        cur.append((s[:eq].strip(SPACE), v.strip(SPACE)))
    return sections, malformed


class Ini:
    """一份合并后的 INI：段名(大写) -> [(key, value)]，后加载的覆盖先加载的。"""

    def __init__(self) -> None:
        self.sections: dict[str, list[tuple[str, str]]] = {}
        self.src: dict[str, str] = {}       # 段名 -> 最后写入它的文件
        self.section_order: list[str] = []

    def merge(self, sections, overwrite: bool, src: str) -> None:
        for sec in sections:
            if not sec:
                continue
            # 段名要回头从 index 取 —— 这里用第一项的"父段"不可靠，
            # 所以 parse 的调用方按段名分别喂进来。
            pass

    def add_section(self, name: str, entries, overwrite: bool, src: str) -> None:
        n = name.upper()
        if n not in self.sections:
            self.sections[n] = []
            self.section_order.append(n)
            self.src[n] = src
            self.sections[n].extend(entries)
            return
        if overwrite:
            # 覆盖在位：同名键换掉，新键追加（Westwood INIClass::Load 语义）
            d = dict(self.sections[n])
            for k, v in entries:
                d[k.upper()] = v
            keep = [(k, v) for k, v in self.sections[n] if k.upper() not in
                    {kk.upper() for kk, _ in entries}]
            self.sections[n] = keep + [(k, v) for k, v in d.items()]
            self.src[n] = src
        else:
            have = {k.upper() for k, _ in self.sections[n]}
            self.sections[n].extend((k, v) for k, v in entries if k.upper() not in have)

    def get(self, sec: str, key: str, default: str = "") -> str:
        e = self.sections.get(sec.upper())
        if not e:
            return default
        want = key.upper()
        for k, v in e:
            if k.upper() == want:
                return v
        return default

    def numbered_list(self, sec: str) -> list[str]:
        """编号列表：键全是数字，按数值升序（[InfantryTypes] 是 1..N，不是 0 起）。"""
        e = self.sections.get(sec.upper())
        if not e:
            return []
        items = []
        for k, v in e:
            if k.isdigit():
                items.append((int(k), v.strip()))
        items.sort()
        return [v for _, v in items if v]

    def key_list(self, sec: str, key: str) -> list[str]:
        return [t.strip() for t in self.get(sec, key, "").split(",") if t.strip()]


def load_family(archives, names: list[tuple[str, bool]]):
    """names = [(文件名, overwrite_after_base), ...]，两趟：先 base 再 md。"""
    ini = Ini()
    got = []
    for fname, overwrite in names:
        for m in archives:
            hit = m.find(fname)
            if hit is None:
                continue
            off, size = hit
            blob = m.read(off, size)
            secs, malformed = parse(blob)
            # 段名要跟 parse 的结果一起回来 —— 重新拆一遍。
            for raw in blob.decode("latin-1").split("\n"):
                pass
            got.append((fname, os.path.basename(m.path), size, len(secs), malformed))
            _apply(ini, blob, overwrite, fname)
    return ini, got


def _apply(ini: Ini, blob: bytes, overwrite: bool, src: str) -> None:
    """把blob 按"段"喂给 ini（段名大小写不敏感，键保留原样）。"""
    name = None
    cur: list[tuple[str, str]] = []
    for raw in blob.decode("latin-1").split("\n"):
        s = raw.strip(SPACE)
        if not s or s[0] == ";" or s[:2] == "//":
            continue
        if s[0] == "[":
            if name is not None:
                ini.add_section(name, cur, overwrite, src)
            close = s.find("]", 1)
            name = (s[1:] if close < 0 else s[1:close]).strip(SPACE)
            cur = []
            continue
        eq = s.find("=")
        if eq < 0 or name is None:
            continue
        v = s[eq + 1:]
        semi = v.find(";")
        if semi >= 0:
            v = v[:semi]
        cur.append((s[:eq].strip(SPACE), v.strip(SPACE)))
    if name is not None:
        ini.add_section(name, cur, overwrite, src)


def main() -> None:
    if len(sys.argv) < 2:
        print(__doc__)
        return
    gamedir = sys.argv[1]
    only_keys = "--typekeys" in sys.argv
    only_sections = "--sections" in sys.argv

    paths = mount_order(gamedir)
    print("挂载 %d 个归档：" % len(paths))
    archives = []
    for p in paths:
        try:
            archives.append(MixFile(p))
        except Exception as e:      # noqa: BLE001
            print("  [skip] %s: %s" % (os.path.basename(p), e))
            archives.append(None)
        else:
            print("  %-20s %10d 字节  条目 %d" %
                  (os.path.basename(p), os.path.getsize(p), archives[-1].count))

    # ---- 五族 INI 的落点 ----
    families = {
        "rules": [("RULES.INI", False), ("RULESMD.INI", True)],
        "art": [("ART.INI", False), ("ARTMD.INI", True)],
        "ai": [("AI.INI", False), ("AIMD.INI", True)],
        "sound": [("SOUND.INI", False), ("SOUNDMD.INI", True)],
        "theme": [("THEME.INI", False), ("THEMEMD.INI", True)],
    }
    print("\n=== INI 落点 ===")
    inis = {}
    for fam, names in families.items():
        ini, got = load_family(archives, names)
        inis[fam] = ini
        if not got:
            print("  %-6s 未找到" % fam)
            continue
        for fname, archive, size, nsec, malformed in got:
            print("  %-6s %-14s <- %-18s %8d 字节  段 %-5d 畸形 %d" %
                  (fam, fname, archive, size, nsec, malformed))
        print("        合并后段数 %d" % len(ini.sections))

    rules = inis["rules"]
    art = inis["art"]

    # ---- 四个类型列表 ----
    type_lists = ["VehicleTypes", "AircraftTypes", "InfantryTypes", "BuildingTypes"]
    units = []
    seen = set()
    for t in type_lists:
        for n in rules.numbered_list(t):
            n = n.strip().upper()
            if n and n not in seen:
                seen.add(n)
                units.append(n)
    print("\n=== 类型列表 ===")
    for t in type_lists:
        lst = rules.numbered_list(t)
        print("  [%-14s] %3d 项  首=%s 末=%s" %
              (t, len(lst), lst[0] if lst else "-", lst[-1] if lst else "-"))
    print("  合计去重 %d 个单位名" % len(units))

    # ---- 单位段的键直方图（rules）----
    rules_keys = Counter()
    rules_sec_with = defaultdict(int)
    for u in units:
        elems = rules.sections.get(u)
        if not elems:
            continue
        ku = {k.upper() for k, _ in elems}
        for k in ku:
            rules_keys[k] += 1
            rules_sec_with[k] += 1

    # ---- Image 名 -> art 段的键直方图 ----
    art_keys = Counter()
    images = set()
    for u in units:
        img = rules.get(u, "Image", "").strip().upper() or u
        images.add(img)
    for img in images:
        elems = art.sections.get(img)
        if not elems:
            continue
        for k in {k.upper() for k, _ in elems}:
            art_keys[k] += 1

    if not only_sections:
        print("\n=== rules [单位] 段里出现的键（%d 种）===" % len(rules_keys))
        for k, n in rules_keys.most_common():
            print("  %-28s %4d 个单位" % (k, n))
        print("\n=== art [Image] 段里出现的键（%d 种）===" % len(art_keys))
        for k, n in art_keys.most_common():
            print("  %-28s %4d 个 Image" % (k, n))

    # ---- 武器 / 弹头 / 抛射体 ----
    weapons = set()
    for u in units:
        w = rules.get(u, "Primary", "").strip().upper()
        if w:
            weapons.add(w)
        w = rules.get(u, "Secondary", "").strip().upper()
        if w:
            weapons.add(w)
    whs = set()
    projs = set()
    for w in weapons:
        x = rules.get(w, "Warhead", "").strip().upper()
        if x:
            whs.add(x)
        x = rules.get(w, "Projectile", "").strip().upper()
        if x:
            projs.add(x)
    print("\n=== 战斗链规模 ===")
    print("  武器段 %d  弹头段 %d  抛射体段 %d" % (len(weapons), len(whs), len(projs)))
    for label, group in (("武器", weapons), ("弹头", whs), ("抛射体", projs)):
        c = Counter()
        for name in group:
            for k, _ in rules.sections.get(name, []):
                c[k.upper()] += 1
        print("  -- %s 段的键 %d 种 --" % (label, len(c)))
        for k, n in c.most_common():
            print("     %-28s %4d" % (k, n))

    # ---- 段名清点：哪些段不属于任何已知族 ----
    if only_sections or not only_keys:
        print("\n=== rules 段名总览（前 40，按是否有名）===")
        named = set(units) | weapons | whs | projs
        others = [s for s in rules.section_order if s not in named]
        print("  段总数 %d  已知单位/武器/弹头/抛射体 %d  其余 %d" %
              (len(rules.section_order), len(named & set(rules.section_order)),
               len(others)))
        print("  其余段名（前 60）：")
        for s in others[:60]:
            print("     %s" % s)

    # ---- AI / SOUND / THEME 的段名 ----
    for fam in ("ai", "sound", "theme"):
        ini = inis[fam]
        if not ini.sections:
            continue
        print("\n=== %s 段名 %d 个 ===" % (fam, len(ini.sections)))
        for s in ini.section_order[:40]:
            print("     %-30s %d 键" % (s, len(ini.sections[s])))


if __name__ == "__main__":
    main()
