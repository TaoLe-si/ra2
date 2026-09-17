"""
techno.py -- TypeDB 打表结果的**独立实现**，用来和 C++ 逐行对账。

为什么要单独写一份：C++ 那份自检（`ra2core --typetable` 里的 Verify）只能证明
"表里存的东西和它自己读的 INI 一致" —— 如果 INI 解析本身就错了（比如合并语义
写反），自检照样全绿。唯一能钉死这件事的是**另写一份、从 MIX 字节重新读起**，
再逐行 diff。

所以这里刻意不复用 src/data/Ini.cpp 的任何逻辑，只复用 tools/mixdump.py
（那份 MIX 解密早已与 C++ 逐字节对齐）。

必须和 C++ 完全一致的几条语义（写错任何一条，对账都会红）：
  1. 合并顺序：先 base 后 md，且 **md 覆盖在位**
     （`IniFile::Merge(overwrite=true)` —— 同名键换值、不换位置也不新增重复键）；
  2. 段内同名键**首个出现者胜**（`IniFile::Find_Entry` 返回第一个）；
  3. 段名/键名大小写不敏感，但**值保留原始大小写**；
  4. 查找顺序：对每个归档，**先本归档顶层、再按条目顺序深度优先**进嵌套 MIX
     （`Read_Deep` 的 Build_Deep_Index 语义）；
  5. 值取到第一个 `;` 为止，两端空白吃掉；`;` 整行与 `//` 整行是注释。

用法：
  python tools/techno.py <游戏目录> --out build/type_raw_py.txt
  python tools/techno.py <游戏目录> --check build/type_raw.txt     # 逐行 diff
"""

from __future__ import annotations

import os
import sys
from collections import OrderedDict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mixdump import MixFile, open_nested, iter_leaves  # noqa: E402

SPACE = " \t\r\n\f\v"

# 挂载顺序（src/core/GameVersion.h）：ra2.mix -> ra2md.mix -> expandmd01..99 -> thememd.mix
ARCHIVE_NAMES = (["ra2.mix", "ra2md.mix"]
                 + ["expandmd%02d.mix" % i for i in range(1, 100)]
                 + ["thememd.mix"])

TYPE_LISTS = ["VehicleTypes", "AircraftTypes", "InfantryTypes", "BuildingTypes"]


def open_archives(gamedir: str):
    out = []
    for n in ARCHIVE_NAMES:
        p = os.path.join(gamedir, n)
        if os.path.exists(p):
            try:
                out.append((n, MixFile(p)))
            except Exception as e:      # noqa: BLE001
                print("  [skip] %s: %s" % (n, e))
    return out


_SUB_CACHE: dict = {}
_DEEP_CACHE: dict = {}


def _sub_of(m: MixFile, off: int, size: int):
    """把条目当嵌套 MIX 打开（缓存 —— C++ 的 Sub_At 也是缓存版）。"""
    key = (id(m), off)
    if key in _SUB_CACHE:
        return _SUB_CACHE[key]
    sub = open_nested(m, off, size)
    _SUB_CACHE[key] = sub
    return sub


def deep_index(m: MixFile) -> dict:
    """复刻 C++ 的 Build_Deep_Index：**先收本层全部条目，再按条目顺序逐个子归档收**，
    且一律 `emplace`（先到的赢）。这个顺序决定了"同名条目在两个地方都有时取哪个"，
    写错的话跨实现对账会红，所以必须照着抄而不是自己发明一个遍历。
    """
    key = id(m)
    if key in _DEEP_CACHE:
        return _DEEP_CACHE[key]
    idx = {}
    for h, off, size in m.entries:
        idx.setdefault(h, (m, off, size))
    for h, off, size in m.entries:
        sub = _sub_of(m, off, size)
        if sub is None:
            continue
        for k, v in deep_index(sub).items():
            idx.setdefault(k, v)
    _DEEP_CACHE[key] = idx
    return idx


def find_deep(named, name: str):
    """在单个归档里按 CRC 找（= C++ 的 MixFileClass::Read_Deep）。"""
    from mixdump import westwood_crc
    _label, m = named
    got = deep_index(m).get(westwood_crc(name))
    if got is None:
        return None
    owner, off, size = got
    if off + size > owner.data_size:
        return None      # 越界条目：C++ Read_Entry 会返回空
    return owner.read(off, size)


# ---------------------------------------------------------------- INI 解析

def _parse(blob: bytes):
    """产出一串 (段名, [(键, 值), ...])，段名保留原样。"""
    secs = []
    name = None
    cur = []
    for raw in blob.decode("latin-1").split("\n"):
        s = raw.strip(SPACE)
        if not s or s[0] == ";" or s[:2] == "//":
            continue
        if s[0] == "[":
            if name is not None:
                secs.append((name, cur))
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
        secs.append((name, cur))
    return secs


class Ini:
    def __init__(self):
        self.sec = OrderedDict()      # 大写段名 -> OrderedDict(大写键 -> (原键, 值))
        self.order = []

    def merge(self, blob: bytes, overwrite: bool):
        for name, entries in _parse(blob):
            n = name.upper()
            if n not in self.sec:
                self.sec[n] = OrderedDict()
                self.order.append(n)
            d = self.sec[n]
            for k, v in entries:
                ku = k.upper()
                if ku in d:
                    if overwrite:
                        # 覆盖在位：换值，键名保持第一次的形态
                        d[ku] = (d[ku][0], v)
                    # 否则追加但按名取值仍取第一个 —— 表里只留第一个，等价
                else:
                    d[ku] = (k, v)

    def numbered_list(self, sec: str):
        d = self.sec.get(sec.upper())
        if not d:
            return []
        items = sorted((int(k), v) for k, (_o, v) in d.items() if k.isdigit())
        return [v for _n, v in items]


def read_deep_all(archives, name: str, ini: Ini, overwrite: bool) -> int:
    """按挂载顺序，每个归档各找一次（C++ 的 Merge_From_Mix 就是逐归档调的）。"""
    got = 0
    for a in archives:
        blob = find_deep(a, name)
        if blob:
            ini.merge(blob, overwrite)
            got += 1
    return got


def dump_raw(gamedir: str, out_path: str) -> None:
    archives = open_archives(gamedir)
    rules, art = Ini(), Ini()
    for name, ow in (("RULES.INI", False), ("RULESMD.INI", True)):
        read_deep_all(archives, name, rules, ow)
    for name, ow in (("ART.INI", False), ("ARTMD.INI", True)):
        read_deep_all(archives, name, art, ow)

    units, seen = [], set()
    for t in TYPE_LISTS:
        for v in rules.numbered_list(t):
            u = v.strip().upper()
            if u and u not in seen:
                seen.add(u)
                units.append(u)

    def key_order(k):
        return (k.upper(), k)

    # 排序口径必须和 C++ 完全一致：按 (单位, 来源, 键) 全序，且**同一单位里
    # rules 一律排在 art 前面**（C++ 是"先扫 rules 再扫 art"再各自对键排序，
    # 不是把两个来源混在一起排）。第一版就是混排的，结果每一行都错位。
    rows = []
    for u in units:
        rs = rules.sec.get(u)
        if not rs:
            continue      # 列表里有名字、没有段 —— C++ 也是跳过
        for k in sorted(rs, key=key_order):
            rows.append((u, 0, key_order(k), "%s\trules\t%s\t%s\n"
                         % (u, rs[k][0], rs[k][1])))
        img = rs.get("IMAGE")
        image = img[1].strip().upper() if img else u
        if not image:
            image = u
        asec = art.sec.get(image)
        if asec:
            for k in sorted(asec, key=key_order):
                rows.append((u, 1, key_order(k), "%s\tart\t%s\t%s\n"
                             % (u, asec[k][0], asec[k][1])))
    rows.sort(key=lambda r: (r[0], r[1], r[2]))
    lines = [r[3] for r in rows]

    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.writelines(lines)
    print("[ok] %d 行 -> %s（%d 个单位，其中 %d 个无 rules 段）"
          % (len(lines), out_path, len(units),
             sum(1 for u in units if u not in rules.sec)))


def check(cpp_path: str, py_path: str) -> int:
    with open(cpp_path, encoding="utf-8") as f:
        a = f.readlines()
    with open(py_path, encoding="utf-8") as f:
        b = f.readlines()
    if a == b:
        print("[OK] 逐行一致：%d 行" % len(a))
        return 0
    print("[x] 不一致：C++ %d 行 / Python %d 行" % (len(a), len(b)))
    n = 0
    for i in range(max(len(a), len(b))):
        x = a[i] if i < len(a) else "<缺>"
        y = b[i] if i < len(b) else "<缺>"
        if x != y:
            print("  行 %d:\n    C++    %s    Python %s" % (i + 1, x.rstrip(), y.rstrip()))
            n += 1
            if n >= 20:
                print("  ...（只打前 20 处）")
                break
    return 1


def main() -> None:
    if len(sys.argv) < 2:
        print(__doc__)
        return
    gamedir = sys.argv[1]
    out = "build/type_raw_py.txt"
    do_check = None
    for i, a in enumerate(sys.argv):
        if a == "--out" and i + 1 < len(sys.argv):
            out = sys.argv[i + 1]
        if a == "--check" and i + 1 < len(sys.argv):
            do_check = sys.argv[i + 1]
    dump_raw(gamedir, out)
    if do_check:
        sys.exit(check(do_check, out))


if __name__ == "__main__":
    main()
