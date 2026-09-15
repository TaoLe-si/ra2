"""
gennormals.py -- 从 db/voxel-normals.txt 生成 src/gfx/VxlNormals.cpp。

为什么要生成而不是手写：361 项 × 3 个 float = 1083 个数，手抄必错。
而这张表的正确性是有硬判据的（外表面法线朝外 94%），
所以生成过程必须无损、且可复算。

用法：
  python tools/gennormals.py
"""

from __future__ import annotations

import io
import os

SRC = r"E:\ra2source\db\voxel-normals.txt"
DST = r"E:\ra2source\src\gfx\VxlNormals.cpp"


def main() -> None:
    slots = {}
    for line in io.open(SRC, encoding="utf-8"):
        s = line.strip()
        if not s or s.startswith("#"):
            continue
        p = s.split()
        slots.setdefault(int(p[0]), []).append(
            (float(p[2]), float(p[3]), float(p[4])))

    order = sorted(slots)
    total = sum(len(slots[k]) for k in order)
    print("槽位 %s，共 %d 项" % (order, total))

    out = io.StringIO()
    out.write("// VxlNormals.cpp -- RA2/YR 体素法线表（VoxLib 内置，VXL 文件里没有）\n")
    out.write("//\n")
    out.write("// **本文件由 tools/gennormals.py 生成，不要手改。**\n")
    out.write("// 源数据：db/voxel-normals.txt（由 tools/normaldump.py 从 exe 里导出的）。\n")
    out.write("//\n")
    out.write("// 【为什么需要】VXL 每个体素带一个 normal 索引（0..255），但法线向量\n")
    out.write("// 不在 VXL 文件里 —— VXL 头只有 +91 的一个 NormalsType。法线表是引擎\n")
    out.write("// 内置的 4 张 LOD 表，精度递增，由一个 VA 指针数组索引：\n")
    out.write("//\n")
    out.write("//     槽位[0]  16 项\n")
    out.write("//     槽位[1]  36 项\n")
    out.write("//     槽位[2]  64 项\n")
    out.write("//     槽位[3] 245 项\n")
    out.write("//\n")
    out.write("// **槽位 = NormalsType - 1**。硬证据（tools/normalverify.py）：\n")
    out.write("//     type=2 的模型（JEEP）normal 取值 0..35   -> 36 项，槽位[1]\n")
    out.write("//     type=4 的模型（GTNK）normal 取值 0..244  -> 245 项，槽位[3]\n")
    out.write("// 用\"外表面法线必须朝外\"验证：用对槽位 94.0%，用错（把 361 项连起来\n")
    out.write("// 从 [0] 查）只有 50.9% —— 跟乱猜一样。\n")
    out.write("//\n")
    out.write("// 【两个版本共用】game.exe 与 gamemd.exe 的表逐字节相同（361 项 0 处不同），\n")
    out.write("// 只是 rva 不同，所以这里只有一份。\n")
    out.write("\n")
    out.write('#include "gfx/VxlNormals.h"\n')
    out.write("\n")
    out.write("namespace ra2 {\n")
    for si in order:
        tab = slots[si]
        out.write("\n/// 槽位[%d]：%d 项（NormalsType=%d 用）。\n" % (si, len(tab), si + 1))
        # 行数就是项数。写错成 len(tab)*3 会让数组尾部被零填充成 3 倍大 ——
        # 查表结果不变（count 是对的），但白白多占 2 倍静态存储。
        out.write("static const float kNormals%d[%d][3] = {\n" % (si, len(tab)))
        for i, (x, y, z) in enumerate(tab):
            if i % 3 == 0:
                out.write("    ")
            out.write("%.6ff, %.6ff, %.6ff," % (x, y, z))
            if i % 3 == 2 or i == len(tab) - 1:
                out.write("\n")
            else:
                out.write(" ")
        out.write("};\n")
    out.write("\nconst float* Voxel_Normal_Table(int slot, int* out_count) {\n")
    out.write("    if (slot < 0) slot = 0;\n")
    out.write("    if (slot >= kNormalSlotCount) slot = kNormalSlotCount - 1;\n")
    out.write("    static const int kCounts[kNormalSlotCount] = {%s};\n"
              % ", ".join(str(len(slots[k])) for k in order))
    out.write("    if (out_count) *out_count = kCounts[slot];\n")
    out.write("    switch (slot) {\n")
    for si in order:
        out.write("        case %d: return &kNormals%d[0][0];\n" % (si, si))
    out.write("    }\n")
    out.write("    return nullptr;\n")
    out.write("}\n\n")
    out.write("}  // namespace ra2\n")

    os.makedirs(os.path.dirname(DST), exist_ok=True)
    io.open(DST, "w", encoding="utf-8", newline="\n").write(out.getvalue())
    print("已写出 %s（%d 字节）" % (DST, os.path.getsize(DST)))


if __name__ == "__main__":
    main()
