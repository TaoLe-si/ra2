"""
mixnames.py -- 把 MIX 里的 32 位 ID 反查成文件名。

MIX 不存文件名，只有 Westwood CRC。好在游戏自己必须按名字去查，
所以 gamemd.exe 里一定留着这些名字的字符串。做法：

  1. 从 PE 里扫出所有看起来像文件名的 ASCII 字符串
  2. 逐个算 CRC，撞进 MIX 的 ID 集合
  3. 命中不了的，用规则生成候选（编号族、国家名 + 后缀等）再撞一轮

用法：
  python tools/mixnames.py D:\\westwood\\RA2YR\\ra2md.mix [更多.mix...]
  python tools/mixnames.py --exe D:\\westwood\\RA2YR\\gamemd.exe --ids 8C8CCA19,FBE0D09D
"""

from __future__ import annotations

import re
import struct
import sys

import mixdump

# 可能的资源扩展名。RA2/YR 的素材类型就这么些，全部试一遍。
EXTS = """mix shp ini pal vxl pcx wav aud mp3 cps tem map sno urb hva txt dll
bik w3d csc pkt dat bin xml csv log fnt fpa mnu bud cur tga png jpg""".split()

_EXT_RE = re.compile(
    r"[A-Za-z0-9_~\-]{2,}\.(?:" + "|".join(EXTS) + r")\b",
    re.IGNORECASE,
)

_ASCII_RUN = re.compile(rb"[\x20-\x7e]{4,}")


def pe_strings(data: bytes):
    """从 PE 里扒可打印字符串。不解析节表 —— 全文扫就够了，
    只读的 .rdata 和代码里的立即数字符串都一样能扫到。"""
    for m in _ASCII_RUN.finditer(data):
        yield m.group().decode("ascii")


def candidate_names_from_exe(exe_path: str):
    """从可执行文件里提取候选文件名。

    除了直接的字符串，还会展开 "%02d/%d" 这类格式串 ——
    二进制里存的是 "MOVMD%02d.MIX"，实际要查的是 MOVMD01.MIX ...
    """
    with open(exe_path, "rb") as f:
        data = f.read()

    seen = set()

    def add(name: str):
        name = name.strip().strip('"').strip()
        if not name.isascii():
            return False          # 中文/其它代码页的文件名，CRC 算法按字节算，先不碰
        if 4 <= len(name) <= 64 and name not in seen:
            seen.add(name)
            return True
        return False

    for s in pe_strings(data):
        if "%" in s:
            # 格式串：MOVMD%02d.MIX / MOVIES%02d.MIX / SS%02d.TEM
            for i in range(0, 100):
                try:
                    add(s % i)
                except (TypeError, ValueError):
                    break
        for m in _EXT_RE.finditer(s):
            add(m.group())
    return seen


def candidate_names_from_dir(root: str):
    """游戏目录里散落的文件（未打包的补丁/汉化残留）本身就是很好的名字源。"""
    import os

    out = set()
    for dirpath, _, files in os.walk(root):
        for fn in files:
            if 4 <= len(fn) <= 64:
                out.add(fn)
    return out


def candidate_names_from_mix_text(mix_paths, max_bytes=4 << 20):
    """从 MIX 里的 INI/文本文件抽名字。

    这是命中率最高的一路：art.ini 用 Image= / Animation= 指向每个 SHP，
    rules.ini 里也是一堆素材名 —— 这些名字 gamemd.exe 里根本没有。
    """
    out = set()
    key_re = re.compile(
        r"(?:Image|Animation|Anim|Voxel|Palette|Sound|Voice|SHP|File|Name|"
        r"Occupy|Overlay|Terrain|Smudge|Building|Weapon|Projectile|Warhead)"
        r"\s*=\s*([A-Za-z0-9_~\-\.]{3,64})", re.IGNORECASE)
    for p in mix_paths:
        m = mixdump.MixFile(p)
        for owner, hid, off, size, depth in mixdump.iter_leaves(m):
            if size > max_bytes:
                continue
            data = owner.read(off, min(size, max_bytes))
            if data.count(0) * 20 > len(data):
                continue                       # 二进制，不是文本
            try:
                text = data.decode("ascii", "ignore")
            except Exception:
                continue
            for mm in _EXT_RE.finditer(text):
                out.add(mm.group())
            for mm in key_re.finditer(text):
                v = mm.group(1)
                if "." in v:
                    out.add(v)
                else:
                    for ext in ("shp", "SHP", "pcx", "vxl", "hva", "wav", "aud"):
                        out.add("%s.%s" % (v, ext))
        m.close()
    return out


def candidate_names_generated():
    """规则生成：单靠二进制字符串覆盖不到的，按 RA2 的命名习惯造。

    这类名字数量会很大，所以只用在"前两轮没撞上"的 ID 上，
    并且按需要再扩 —— 现在放的是覆盖范围/命中比最划算的几族。
    """
    out = set()
    # 各国剧场/campaign 素材：GAnnn / CAnnn / TTnnn ...
    for fam in ("GA", "CA", "TT", "MOVMD", "MOVIES", "AUDIO", "AUDIOMD",
                "SS", "SC", "MAPS", "EXPAND", "ECACHE", "CACHE", "LOCAL",
                "NEWMAP", "MULTIMD", "THEME", "SIDES", "GEN"):
        for i in range(0, 100):
            out.add("%s%02d.MIX" % (fam, i))
            out.add("%s%02d.mix" % (fam, i))
    # 简单编号族
    for i in range(0, 1000):
        out.add("%03d.MIX" % i)
        out.add("%04d.MIX" % i)
    return out


def load_ids(mix_paths):
    """读所有 MIX（含嵌套）收集待反查的 ID。"""
    ids = {}
    for p in mix_paths:
        m = mixdump.MixFile(p)
        for owner, h, off, size, depth in mixdump.iter_leaves(m):
            ids.setdefault(h, (p, depth, size))
        m.close()
    return ids


def main() -> None:
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        sys.exit(1)

    exe = r"D:\westwood\RA2YR\gamemd.exe"
    gamedir = r"D:\westwood\RA2YR"
    ids_arg = None
    paths = []
    i = 0
    while i < len(args):
        if args[i] == "--exe":
            exe = args[i + 1]
            i += 2
        elif args[i] == "--ids":
            ids_arg = [int(x, 16) for x in args[i + 1].split(",")]
            i += 2
        elif args[i] == "--gamedir":
            gamedir = args[i + 1]
            i += 2
        else:
            paths.append(args[i])
            i += 1

    if ids_arg is not None:
        ids = {h: ("<命令行>", 0, 0) for h in ids_arg}
    elif paths:
        ids = load_ids(paths)
    else:
        print("至少要给一个 .mix 或 --ids")
        sys.exit(1)

    print("待反查 ID: %d 个" % len(ids))

    cands = set()
    try:
        cands |= candidate_names_from_exe(exe)
        print("  从 %s 抽出候选名 %d 个" % (exe, len(cands)))
    except OSError as e:
        print("  [!] 读 %s 失败: %s" % (exe, e))
    if gamedir:
        n0 = len(cands)
        cands |= candidate_names_from_dir(gamedir)
        print("  从游戏目录补 %d 个（累计 %d）" % (len(cands) - n0, len(cands)))

    table = {}
    per_round = []

    def try_names(names):
        hit = 0
        for n in names:
            if not n.isascii():
                continue
            c = mixdump.westwood_crc(n)
            if c in ids and c not in table:
                table[c] = n
                hit += 1
        return hit

    per_round.append(("二进制字符串 + 目录", try_names(cands)))
    if len(table) < len(ids) and paths:
        per_round.append(("MIX 内 INI 文本", try_names(candidate_names_from_mix_text(paths))))
    if len(table) < len(ids):
        per_round.append(("规则生成", try_names(candidate_names_generated())))

    for i, (label, n) in enumerate(per_round, 1):
        print("  第%d轮 %s：本轮新增命中 %d（累计 %d）" % (i, label, n, sum(x[1] for x in per_round[:i])))

    print("\n反查出 %d / %d = %.1f%%" % (len(table), len(ids), 100.0 * len(table) / max(1, len(ids))))

    out = []
    for h, (src, depth, size) in sorted(ids.items(), key=lambda kv: (kv[1][0], kv[0])):
        out.append("%-10s 0x%08X  %-24s %s" % (src.split("\\")[-1], h,
                                               table.get(h, "?"), size))
    open(r"E:\ra2source\db\mix-names.txt", "w", encoding="utf-8").write(
        "\n".join(out) + "\n")
    print("已写出 db/mix-names.txt")


if __name__ == "__main__":
    sys.path.insert(0, r"E:\ra2source\tools")
    main()
