"""
normalscan2.py -- 精确定界 gamemd.exe 里的单位向量数组。

normalscan.py 的问题是只报"窗口命中"，一个 256 项的表会报出几十个错位窗口。
这一版改成：先给每个 4 字节对齐的位置打"是不是单位向量"的标记，
再找连续 True 段（长度 ≥ 64），一段就是一个候选数组。
真表的段长应该正好是 256（或 256 的倍数附近）。

用法：
  python tools/normalscan2.py [exe路径] [--min 64]
"""

from __future__ import annotations

import math
import struct
import sys

import peimage


def is_unit(data, o):
    x, y, z = struct.unpack_from("<3f", data, o)
    if not (abs(x) <= 1.0001 and abs(y) <= 1.0001 and abs(z) <= 1.0001):
        return False, None
    L = math.sqrt(x * x + y * y + z * z)
    if L < 0.98 or L > 1.02:
        return False, None
    return True, (x, y, z)


def main() -> None:
    rest = sys.argv[1:]
    min_len = 64
    if "--min" in rest:
        i = rest.index("--min")
        min_len = int(rest[i + 1])
        rest = rest[:i] + rest[i + 2:]
    path = rest[0] if rest else peimage.DEFAULT_IMAGE

    img = peimage.PEImage(path)
    data = img.data
    print("PE %s：%d 字节" % (path, len(data)))

    found = []
    for s in img.sections:
        if s["raw_size"] < 4096:
            continue
        base, size = s["raw_ptr"], s["raw_size"]
        end = base + size - 12
        # 只从"段起点"出发按 stride=12 向前量长度。
        # 段起点 = 自己是单位向量、且往前 12 字节不是（或已越界）。
        # （不能逐 4 扫着判连续：数组内部每 12 字节才有一个有效起点，
        #   中间的错位读取必然不是单位向量，会把一段切成碎片。）
        runs = []
        off = base
        while off <= end:
            ok, _v = is_unit(data, off)
            if not ok:
                off += 4
                continue
            prev_ok = (off - 12 >= base) and is_unit(data, off - 12)[0]
            if prev_ok:
                off += 4
                continue
            n = 0
            p = off
            while p <= end and is_unit(data, p)[0]:
                n += 1
                p += 12
            if n >= min_len:
                runs.append((off, n))
            off += 4
        for st, n in runs:
            found.append((s["name"], st, img.off_to_rva(st), n))

    if not found:
        print("[x] 没有长度 >= %d 的连续单位向量段" % min_len)
        return

    found.sort(key=lambda f: -f[3])
    print("\n连续单位向量段（按长度降序）：")
    for sec, off, rva, n in found[:25]:
        mark = ""
        if n == 256:
            mark = "   <== 正好 256 项！"
        elif 250 <= n <= 260:
            mark = "   <== 接近 256"
        print("  %-7s off=0x%08X rva=0x%08X  %5d 项%s" % (sec, off, rva, n, mark))

    out = r"E:\ra2source\build\normals_runs.txt"
    with open(out, "w", encoding="utf-8") as f:
        for sec, off, rva, n in found:
            f.write("%s off=0x%08X rva=0x%08X n=%d\n" % (sec, off, rva, n))
    print("\n全部 %d 段 -> %s" % (len(found), out))


if __name__ == "__main__":
    sys.path.insert(0, r"E:\ra2source\tools")
    main()
