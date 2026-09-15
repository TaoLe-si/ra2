"""
theaterdump.py -- 打开剧场包（urban.mix / temperat.mix ...），列内容并抽出剧场 INI。

ra2.mix 里：
  isourb.mix / isotemp.mix ...   = 该剧场全部 TMP 瓦片（1257 个级别）
  urban.mix / temperat.mix ...   = 剧场定义：INI + 调色板 + 少量 SHP

用法：
  python tools/theaterdump.py D:\\westwood\\RA2YR\\ra2.mix urban.mix E:\\ra2source\\build\\theater
"""

from __future__ import annotations

import os
import sys

sys.path.insert(0, r"E:\ra2source\tools")
import mixdump  # noqa: E402


def main() -> None:
    if len(sys.argv) < 4:
        print(__doc__)
        sys.exit(1)
    root_path, sub_name, outdir = sys.argv[1], sys.argv[2], sys.argv[3]
    os.makedirs(outdir, exist_ok=True)

    root = mixdump.MixFile(root_path)
    hit = root.find(sub_name)
    if hit is None:
        print("[x] %s 里没有 %s" % (root_path, sub_name))
        sys.exit(1)
    off, size = hit
    print("[OK] %s  off=%d size=%d" % (sub_name, off, size))
    sub = mixdump.open_nested(root, off, size)
    if sub is None:
        print("[x] %s 不是可嵌套的 MIX" % sub_name)
        sys.exit(1)
    print("  子归档 flags=0x%08X 条目=%d 数据区=%d" % (sub.flags, sub.count, sub.data_size))

    # 候选名：剧场名 + 常见扩展名
    stem = os.path.splitext(sub_name)[0].upper()
    exts = ("INI", "ini", "PAL", "pal", "PCX", "pcx", "SHP", "shp", "MIX", "TEM", "URB", "SNO")
    table = {}
    ids = {h: (o, s) for h, o, s in sub.entries}
    for e in exts:
        for n in (("%s.%s" % (stem, e)), ("%sMD.%s" % (stem, e))):
            c = mixdump.westwood_crc(n)
            if c in ids:
                table[c] = n

    for h, o, s in sub.entries:
        if table.get(h) or s < 200000:
            print("   id=0x%08X  %-16s size=%-9d head=%s" % (
                h, table.get(h, "?"), s, sub.head(o, 16).hex(" ")))
        if s < 200000:
            data = sub.read(o, s)
            try:
                t = data.decode("ascii", "ignore")
            except Exception:
                continue
            printable = sum(1 for ch in t if ch.isprintable() or ch in "\r\n")
            if printable > len(t) * 0.9 and len(t) > 0:
                name = table.get(h, "%08X.ini" % h)
                out = os.path.join(outdir, name)
                open(out, "wb").write(data)
                print("      -> 文本，已写出 %s (%d 字节)" % (out, s))
    root.close()


if __name__ == "__main__":
    main()
