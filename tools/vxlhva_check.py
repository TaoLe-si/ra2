"""vxlhva_check.py -- 用"肢体名唯一"的多肢配对，验证 VXL 肢体尾与 HVA 的关系。

假设：VXL 肢体尾的 12 个 float = [R | T·det]，HVA 的是 [R | T]（体素单位）。
即 `T_hva ≈ T_vxl / det`，而 3×3 部分两边相同。

单肢模型（绝大多数）名字都是 DUMMY01/BODY 之类，会互相撞车，不能用来验证；
只挑名字至少有 2 个不同、且帧数/肢数 >= 2 的配对。

用法: python tools/vxlhva_check.py [ra2.mix]
"""
from __future__ import annotations

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mixdump import MixFile, iter_leaves          # noqa: E402
from vxlstruct import VxlFile, VxlError           # noqa: E402
from vxlhva import hva_limb_names, hva_matrix     # noqa: E402


def main() -> None:
    path = sys.argv[1] if len(sys.argv) > 1 else "D:/westwood/RA2YR/ra2.mix"
    m = MixFile(path)
    vxls, hvas = [], {}
    for owner, h, off, size, depth in iter_leaves(m):
        d = owner.read(off, size)
        if size >= 15 and d[:15] == b"Voxel Animation":
            try:
                vxls.append((h, VxlFile(d)))
            except VxlError:
                pass
            continue
        names = hva_limb_names(d)
        if names:
            frames, limbs = struct.unpack_from("<2I", d, 16)
            hvas.setdefault(tuple(names), []).append((h, frames, limbs, d))

    checked = 0
    for vh, v in vxls:
        if v.num_limbs < 2:
            continue
        names = tuple(v.headers[i].name for i in range(v.num_limbs))
        cand = hvas.get(names)
        if not cand or len(set(names)) < 2:
            continue
        hh, frames, limbs, hd = cand[0]
        det = v.tailers[0].det
        print("\nVXL 0x%08X (%d 肢) <- HVA 0x%08X (%d 帧 x %d 肢)  det=%g  names=%s"
              % (vh, v.num_limbs, hh, frames, limbs, det, list(names)))
        worst_r = worst_t = 0.0
        for l in range(v.num_limbs):
            tv = v.tailers[l].transform
            th = hva_matrix(hd, 0, l, limbs)
            # 3×3 部分应当一致
            dr = max(abs(tv[i] - th[i]) for i in (0, 1, 2, 4, 5, 6, 8, 9, 10))
            # 平移应当差一个 det（T_hva = T_vxl / det）
            dt = max(abs(tv[3 + k] - th[3 + k] * det) for k in range(3))
            worst_r = max(worst_r, dr)
            worst_t = max(worst_t, dt)
            print("  limb%d R 最大差=%.5f  T(缩放后)最大差=%.5f   T_vxl=%s T_hva=%s"
                  % (l, dr, dt,
                     "(%7.3f,%7.3f,%7.3f)" % (tv[3], tv[7], tv[11]),
                     "(%8.3f,%8.3f,%8.3f)" % (th[3], th[7], th[11])))
        print("  => 3×3 最大差 %.6f，平移（除 det 后）最大差 %.6f" % (worst_r, worst_t))
        checked += 1
    print("\n检查了 %d 个唯一的 VXL/HVA 配对" % checked)


if __name__ == "__main__":
    main()
