"""
normalscan.py -- 在 gamemd.exe 里找 VXL 的 256 项法线表。

为什么需要：VXL 每个体素带一个 normal 索引（0..255），但**法线表不在文件里**。
VXL 格式里只有 +91 的一个字节 NormalsType（实测只有 2 和 4），
256 个法线向量是引擎内置的（VoxLib）。没有它，体素就只能渲成一片平色，
RA2 那种"侧面暗、顶面亮"的立体感全靠这张表。

搜索判据（多重，互相印证）：
  * 256 个连续三元组，分量 ∈ [-1, 1]，模长 ≈ 1
  * 三种编码都试：float32×3、float32×4（padding）、int8/127×3
  * 真表的模长应该很集中（要么恒 1，要么量化到很窄的区间）

用法：
  python tools/normalscan.py [exe路径]
"""

from __future__ import annotations

import math
import struct
import sys

import peimage

N = 256


def probe(data, base, size, kind):
    """在 [base, base+size) 里按 kind 编码找连续的 256 个单位向量。"""
    hits = []
    stride, is_float = {
        "f32x3": (12, True), "f32x4": (16, True), "i8x3": (3, False),
    }[kind]
    step = 4 if is_float else 1
    end = base + size - N * stride
    off = base
    while off < end:
        ok = 0
        zpos = 0
        lo, hi = 9.0, 0.0
        for i in range(N):
            o = off + i * stride
            if is_float:
                x, y, z = struct.unpack_from("<3f", data, o)
            else:
                a, b, c = struct.unpack_from("<3b", data, o)
                x, y, z = a / 127.0, b / 127.0, c / 127.0
            if not (abs(x) <= 1.0001 and abs(y) <= 1.0001 and abs(z) <= 1.0001):
                break
            L = math.sqrt(x * x + y * y + z * z)
            if L < 1e-6:
                break
            lo, hi = min(lo, L), max(hi, L)
            if z > 0:
                zpos += 1
            ok += 1
        if ok == N and hi <= 1.35 and lo >= 0.60:
            hits.append((off, lo, hi, zpos))
        off += step
    return hits


def main() -> None:
    path = sys.argv[1] if len(sys.argv) > 1 else peimage.DEFAULT_IMAGE
    img = peimage.PEImage(path)
    data = img.data
    print("PE %s: %d 字节，%d 节" % (path, len(data), len(img.sections)))

    all_hits = []
    for s in img.sections:
        if s["raw_size"] < 4096:
            continue
        print("  扫 %-8s %8d 字节  rva=0x%08X" % (s["name"], s["raw_size"], s["rva"]))
        for kind in ("f32x3", "f32x4", "i8x3"):
            for off, lo, hi, zpos in probe(data, s["raw_ptr"], s["raw_size"], kind):
                rva = img.off_to_rva(off)
                all_hits.append((kind, s["name"], off, rva, lo, hi, zpos))

    if not all_hits:
        print("\n[x] 没找到。判据可能太严，或法线表不是裸数组。")
        return

    # 真表的模长区间应该很窄，按 (hi-lo) 升序排
    all_hits.sort(key=lambda h: (h[5] - h[4]))
    print("\n候选 %d 处（按模长离散度升序，前 30）：" % len(all_hits))
    for kind, sec, off, rva, lo, hi, zpos in all_hits[:30]:
        print("  %-6s %-7s off=0x%08X rva=0x%08X  模长[%.4f,%.4f]  z>0:%3d"
              % (kind, sec, off, rva, lo, hi, zpos))

    out = r"E:\ra2source\build\normals_candidates.txt"
    with open(out, "w", encoding="utf-8") as f:
        for kind, sec, off, rva, lo, hi, zpos in all_hits:
            f.write("%s %s off=0x%08X rva=0x%08X len[%.4f,%.4f] zpos=%d\n"
                    % (kind, sec, off, rva, lo, hi, zpos))
    print("全部候选 -> %s" % out)


if __name__ == "__main__":
    sys.path.insert(0, r"E:\ra2source\tools")
    main()
