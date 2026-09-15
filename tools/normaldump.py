"""
normaldump.py -- 导出 gamemd.exe / game.exe 里的体素法线表（4 张 LOD 表）。

【表不在 VXL 文件里】VXL 每个体素带一个 normal 索引（0..255），但 256 个法线
向量是引擎（VoxLib）内置的。VXL 头里只有 +91 的 NormalsType（实测只有 2 和 4）。

【不是一张表，是 4 张 LOD 表】由一个 VA 指针数组索引，精度递增：

    gamemd.exe 指针数组 off=0x004469E4
    game.exe   指针数组 off=(见下)

    槽位[0] rva=0x00446A08   16 项
    槽位[1] rva=0x00446AC8   36 项
    槽位[2] rva=0x00446C78   64 项
    槽位[3] rva=0x00446F78  245 项

**槽位 = NormalsType - 1**。硬证据（tools/normalverify.py，"外表面法线必须朝外"）：
    type=2 的模型（JEEP）normal 取值 0..35   -> 36 项表，槽位[1]
    type=4 的模型（GTNK）normal 取值 0..244  -> 245 项表，槽位[3]
用错槽位（比如把 361 项连在一起从 [0] 查起）命中率只有 50.9%（等于乱猜），
用对槽位是 94.0%（顶面 97% / 底面 91% / ±X 99%,95% / ±Y 95%,89%）。

【两个版本共用】game.exe 与 gamemd.exe 的表**逐字节相同**（361 项 0 处不同），
只是 rva 不同。

用法：
  python tools/normaldump.py
"""

from __future__ import annotations

import struct
import sys

import peimage

# (exe, 指针数组的文件偏移)
EXES = [
    ("RA2", r"D:\westwood\RA2YR\game.exe", None),
    ("YR ", r"D:\westwood\RA2YR\gamemd.exe", 0x004469E4),
]
NSLOT = 4
OUT = r"E:\ra2source\db\voxel-normals.txt"


def slots_from_ptr_array(data, ptr_off):
    """从 VA 指针数组读出 4 个槽位的 rva；返回 [(rva, None), ...]（长度待定）。"""
    out = []
    for i in range(NSLOT):
        va = struct.unpack_from("<I", data, ptr_off + i * 4)[0]
        rva = va - 0x00400000
        if not (0x1000 < rva < len(data)):
            return None
        out.append(rva)
    return out


def find_ptr_array(data, first_rva):
    """找不到指针数组时，反查"谁指向 first_rva"。"""
    va = 0x00400000 + first_rva
    i = data.find(struct.pack("<I", va))
    if i < 0:
        return None
    # 往前退到本槽位是第 0 个：连续 4 个有效指针且前一个不是
    return i


def main() -> None:
    tables_by_ver = {}
    for tag, path, ptr_off in EXES:
        data = open(path, "rb").read()
        img = peimage.PEImage(path)
        # 两个 exe 的 rva 不一样，所以不硬编码地址：
        #   1) 先在 .data 里找"连续单位向量段"的起点，它就是槽位[0]；
        #   2) 再反查"哪个 VA 指针指向它"，那个位置就是指针数组，
        #      顺着读出 4 个槽位的 rva。
        first, first_n = longest_unit_run(data, img)
        if first is None:
            print("[x] %s 找不到单位向量段" % tag)
            continue
        if ptr_off is None:
            ptr_off = find_ptr_array(data, first)
        slots = slots_from_ptr_array(data, ptr_off) if ptr_off else None
        if slots is None or slots[0] != first:
            print("[x] %s 指针数组定位失败（first=0x%08X slots=%s）"
                  % (tag, first, ["0x%08X" % s for s in slots] if slots else None))
            continue

        # 项数 = 下一槽位起点 - 本槽位起点；最后一张用"连续单位向量段"的尾巴定
        counts = []
        for i in range(NSLOT - 1):
            counts.append((slots[i + 1] - slots[i]) // 12)
        # 末槽位：从起点一直量到不再是单位向量
        n = 0
        p = slots[-1]
        while p + 12 <= len(data):
            x, y, z = struct.unpack_from("<3f", data, p)
            L = (x * x + y * y + z * z) ** 0.5
            if not (0.98 <= L <= 1.02):
                break
            n += 1
            p += 12
        counts.append(n)

        tabs = []
        for rva, c in zip(slots, counts):
            off = img.rva_to_off(rva)
            tabs.append([struct.unpack_from("<3f", data, off + i * 12)
                         for i in range(c)])
        tables_by_ver[tag] = tabs
        print("%s %s" % (tag, path))
        for i, (rva, c) in enumerate(zip(slots, counts)):
            print("   槽位[%d] rva=0x%08X  %3d 项   (NormalsType=%d 用)"
                  % (i, rva, c, i + 1))

    # 交叉验证
    if len(tables_by_ver) == 2:
        a, b = tables_by_ver["RA2"], tables_by_ver["YR "]
        diff = 0
        tot = 0
        for ta, tb in zip(a, b):
            for x, y in zip(ta, tb):
                tot += 1
                if max(abs(p - q) for p, q in zip(x, y)) > 1e-6:
                    diff += 1
        print("\n两版本不一致的项：%d / %d" % (diff, tot))

    yr = tables_by_ver.get("YR ") or tables_by_ver.get("RA2")
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("# RA2 / YR 体素法线表（VoxLib 内置，VXL 文件里没有）\n")
        f.write("# 4 张 LOD 表，槽位 = VXL 头的 NormalsType - 1。\n")
        f.write("# 验证：tools/normalverify.py，外表面法线朝外命中率 94.0%"
                "（用错槽位只有 50.9%）。\n")
        f.write("#\n")
        for tag, path, ptr_off in EXES:
            f.write("#   %s %s\n" % (tag, path))
        f.write("#\n# 格式：槽位 索引 x y z\n")
        for si, tab in enumerate(yr):
            f.write("# 槽位[%d] %d 项  (NormalsType=%d)\n" % (si, len(tab), si + 1))
            for i, (x, y, z) in enumerate(tab):
                f.write("%d %3d %.6f %.6f %.6f\n" % (si, i, x, y, z))
    print("\n已写出 %s（%d 张表，共 %d 项）"
          % (OUT, len(yr), sum(len(t) for t in yr)))


def longest_unit_run(data, img):
    """找**最长**的连续单位向量段，返回它的 rva。

    为什么取最长而不是第一个命中：.rdata 里散落着单个单位向量（比如 0x3E2AC8），
    扫到就返回会拿到假阳性。法线表是 361 项的大块，取最长一定命中它。
    （踩过：先命中 .rdata 的零散向量，导致指针数组对不上。）
    """
    best_rva, best_n = None, 0
    for s in img.sections:
        if s["raw_size"] < 4096 or s["name"] not in (".data", ".rdata"):
            continue
        base, size = s["raw_ptr"], s["raw_size"]
        end = base + size - 12
        off = base
        while off <= end:
            x, y, z = struct.unpack_from("<3f", data, off)
            L = (x * x + y * y + z * z) ** 0.5
            if not (0.98 <= L <= 1.02):
                off += 4
                continue
            # 是段起点吗（往前 12 字节不是单位向量）
            if off - 12 >= base:
                px, py, pz = struct.unpack_from("<3f", data, off - 12)
                pl = (px * px + py * py + pz * pz) ** 0.5
                if 0.98 <= pl <= 1.02:
                    off += 4
                    continue
            n = 0
            p = off
            while p <= end:
                a, b, c = struct.unpack_from("<3f", data, p)
                ll = (a * a + b * b + c * c) ** 0.5
                if not (0.98 <= ll <= 1.02):
                    break
                n += 1
                p += 12
            if n > best_n:
                best_n, best_rva = n, img.off_to_rva(off)
            off += 4
    return best_rva, best_n


if __name__ == "__main__":
    sys.path.insert(0, r"E:\ra2source\tools")
    main()
