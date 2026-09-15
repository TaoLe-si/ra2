"""vxlhva_aabb.py -- 诊断：逐肢比较 [静态尾姿态] 与 [HVA 第N帧] 的世界 AABB。

用途：判断 HVA 接进渲染链路后肢体是否仍然自洽（四脚分居四角、z 分层正确）。

用法: python tools/vxlhva_aabb.py <mix> <vxl_id_hex> [hva_id_hex|-] [frame]
"""
from __future__ import annotations

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mixdump import MixFile, iter_leaves      # noqa: E402
from vxlstruct import VxlFile                 # noqa: E402


def hva_frames(data: bytes):
    """返回 (frame_count, limb_count, [frame][limb][12])，结构不符则 raise。

    【布局】+0 16B 编译期残留路径 / +16 FrameCount / +20 LimbCount /
    +24 16B×L 肢体名 / 之后 48B×F×L 矩阵。
    矩阵块起点是 24 + 16*L —— 早期误当成 16，导致漏算肢体名区，配对全灭。
    """
    if len(data) < 24:
        raise ValueError("太短")
    n, l = struct.unpack_from("<2I", data, 16)
    if n == 0 or l == 0 or n > 4096 or l > 256:
        raise ValueError("计数离谱 n=%d l=%d" % (n, l))
    m_off = 24 + 16 * l
    if m_off + n * l * 48 != len(data):
        raise ValueError("长度不符 %d != %d" % (m_off + n * l * 48, len(data)))
    off = m_off
    frames = []
    for _ in range(n):
        fr = []
        for _ in range(l):
            fr.append(list(struct.unpack_from("<12f", data, off)))
            off += 48
        frames.append(fr)
    return n, l, frames


def collect(mix_path: str):
    """返回 (vxl 列表, hva 列表)：元素为 (id, bytes)。"""
    m = MixFile(mix_path)
    vxls, hvas = [], []
    for owner, h, off, size, _d in iter_leaves(m):
        head = owner.read(off, min(size, 16))
        if size >= 1024 and head[:15] == b"Voxel Animation":
            vxls.append((h, owner.read(off, size)))
            continue
        if size >= 16:
            blob = owner.read(off, size)
            try:
                hva_frames(blob)
            except Exception:
                continue
            hvas.append((h, blob))
    return vxls, hvas


# 3×4 行主序里，旋转分量下标是 0,1,2 / 4,5,6 / 8,9,10；3,7,11 是平移。
# 【踩坑】早期写成 range(9)，把 3、7 两个平移分量当旋转比，差值恒为 11×T，
# 于是 |ΔR| 动辄上百、配对直接全灭。
ROT_IX = (0, 1, 2, 4, 5, 6, 8, 9, 10)
TRA_IX = (3, 7, 11)


def apply(m, x, y, z):
    return (m[0] * x + m[1] * y + m[2] * z + m[3],
            m[4] * x + m[5] * y + m[6] * z + m[7],
            m[8] * x + m[9] * y + m[10] * z + m[11])


def limb_aabb(vxl: VxlFile, li: int, m):
    """肢体体素的世界 AABB。

    局部坐标 = 索引 + min_bounds（min/max_bounds 是该肢体在局部系下的 AABB，
    局部原点在 AABB 中心）。直接把索引喂变换在单肢模型上看不出来，
    多肢模型会立刻散架。
    """
    t = vxl.tailers[li]
    ox, oy, oz = t.min_bounds
    lo = [1e30] * 3
    hi = [-1e30] * 3
    cnt = 0
    for (x, y, z, _c, _n) in vxl.voxels(li):
        px, py, pz = apply(m, x + ox, y + oy, z + oz)
        for k, p in enumerate((px, py, pz)):
            if p < lo[k]:
                lo[k] = p
            if p > hi[k]:
                hi[k] = p
        cnt += 1
    if cnt == 0:
        return None
    return lo, hi, cnt


def main() -> None:
    mix = sys.argv[1]
    vid = int(sys.argv[2], 16)
    want_hva = None
    if len(sys.argv) > 3 and sys.argv[3] not in ("-", "auto"):
        want_hva = int(sys.argv[3], 16)
    frame = int(sys.argv[4]) if len(sys.argv) > 4 else 0

    vxls, hvas = collect(mix)
    vxl_blob = None
    for h, d in vxls:
        if h == vid:
            vxl_blob = d
            break
    if vxl_blob is None:
        raise SystemExit("VXL 0x%08X 不在 %s（共 %d 个）" % (vid, mix, len(vxls)))
    vxl = VxlFile(vxl_blob)
    L = vxl.num_limbs
    print("VXL 0x%08X limbs=%d body=%d vox=%d"
          % (vid, L, vxl.body_size, sum(1 for li in range(L) for _ in vxl.voxels(li))))

    # 配对：优先用静态尾姿态的 R + T/det 指纹
    hits = []
    for h, d in hvas:
        try:
            n, l, frames = hva_frames(d)
        except Exception:
            continue
        if l != L:
            continue
        ok = True
        for k in range(L):
            t = vxl.tailers[k]
            det = t.det if t.det != 0.0 else 1.0
            for i in ROT_IX:
                if abs(frames[0][k][i] - t.transform[i]) > 1e-3:
                    ok = False
                    break
            if ok:
                for j in TRA_IX:
                    if abs(frames[0][k][j] * det - t.transform[j]) > 2e-2:
                        ok = False
                        break
            if not ok:
                break
        if ok:
            hits.append((h, n, frames))

    if want_hva is not None:
        hits = [x for x in hits if x[0] == want_hva] or hits
    if len(hits) != 1:
        print("候选 HVA（肢数相同者中指纹全等）：%s"
              % ", ".join("0x%08X(%d帧)" % (h, n) for h, n, _ in hits))
    if not hits:
        raise SystemExit("没配到 HVA")

    hid, nf, frames = hits[0]
    print("HVA 0x%08X frames=%d limbs=%d 用帧 %d" % (hid, nf, L, frame % nf))

    print("\n%-4s %-16s %-8s %-30s %-30s %s"
          % ("limb", "name", "vox", "static lo/hi", "hva lo/hi", "max|Δ|"))
    g_lo = [1e30] * 3
    g_hi = [-1e30] * 3
    for k in range(L):
        t = vxl.tailers[k]
        st = list(t.transform)
        hv = list(frames[frame % nf][k])
        det = t.det if t.det != 0.0 else 1.0
        hv[3] *= det
        hv[7] *= det
        hv[11] *= det
        a1 = limb_aabb(vxl, k, st)
        a2 = limb_aabb(vxl, k, hv)
        if a2:
            for i in range(3):
                g_lo[i] = min(g_lo[i], a2[0][i])
                g_hi[i] = max(g_hi[i], a2[1][i])
        if not a1:
            continue
        dd = max(max(abs(a1[0][i] - a2[0][i]) for i in range(3)),
                 max(abs(a1[1][i] - a2[1][i]) for i in range(3)))
        print("%-4d %-16s %-8d (%.1f,%.1f,%.1f)-(%.1f,%.1f,%.1f)   "
              "(%.1f,%.1f,%.1f)-(%.1f,%.1f,%.1f)   %.3f"
              % (k, vxl.headers[k].name, a1[2],
                 *a1[0], *a1[1], *a2[0], *a2[1], dd))
    print("\n全模型 HVA 帧 %d 世界 AABB: lo=(%.2f,%.2f,%.2f) hi=(%.2f,%.2f,%.2f) size=(%.2f,%.2f,%.2f)"
          % (frame % nf, *g_lo, *g_hi, g_hi[0] - g_lo[0], g_hi[1] - g_lo[1], g_hi[2] - g_lo[2]))
    print("静态尾姿态世界 AABB（同式算）:")
    lo = [1e30] * 3
    hi = [-1e30] * 3
    for k in range(L):
        a = limb_aabb(vxl, k, list(vxl.tailers[k].transform))
        if not a:
            continue
        for i in range(3):
            lo[i] = min(lo[i], a[0][i])
            hi[i] = max(hi[i], a[1][i])
    print("    lo=(%.2f,%.2f,%.2f) hi=(%.2f,%.2f,%.2f) size=(%.2f,%.2f,%.2f)"
          % (*lo, *hi, hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]))


if __name__ == "__main__":
    main()
