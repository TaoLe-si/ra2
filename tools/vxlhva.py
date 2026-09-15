"""vxlhva.py -- 按肢体名把 VXL 和 HVA 配对，弄清 HVA 矩阵该怎么用。

要回答的问题：VXL 肢体尾里已经有一个 3×4 变换，HVA 又给每帧每肢一个 3×4。
两者是"同一槽位的静态值 / 逐帧覆盖"，还是"要串起来乘"？

MIX 里没有文件名，所以配对只能靠内容：HVA 的肢体名数组和 VXL 的肢体头名字
必须逐一相同。这个约束足够强（"FOOT R REAR" 之类不会撞车）。

用法: python tools/vxlhva.py [ra2.mix]
"""
from __future__ import annotations

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mixdump import MixFile, iter_leaves          # noqa: E402
from vxlstruct import VxlFile, VxlError           # noqa: E402


def hva_limb_names(d: bytes) -> list:
    """尽力解出 HVA 的肢体名。判据同 HvaFile.cpp：长度算术 + 名字可打印。"""
    if len(d) < 24:
        return []
    frames, limbs = struct.unpack_from("<2I", d, 16)
    if not (1 <= frames <= 4096 and 1 <= limbs <= 256):
        return []
    if 24 + limbs * 16 + frames * limbs * 48 != len(d):
        return []
    names = []
    for i in range(limbs):
        raw = d[24 + i * 16:24 + i * 16 + 16].split(b"\x00")[0]
        try:
            s = raw.decode("ascii")
        except UnicodeDecodeError:
            return []
        # 判据和 src/gfx/HvaFile.cpp 对齐：只要可打印 ASCII 且非空即可。
        # 别加 "首尾无空格" 这种额外条件 —— 实测有 "MAIN BODY " 这类带尾空格的
        # 名字，加了就把 182 个 HVA 全滤掉了（踩过）。
        if not s or any(not (0x20 <= ord(c) <= 0x7E) for c in s):
            return []
        names.append(s.strip())
    return names


def mat12(d: bytes, off: int) -> tuple:
    return struct.unpack_from("<12f", d, off)


def hva_matrix(d: bytes, frame: int, limb: int, limbs: int) -> tuple:
    return mat12(d, 24 + limbs * 16 + (frame * limbs + limb) * 48)


def fmt(m: tuple) -> str:
    return "[" + " ".join("%7.3f" % v for v in m[:4]) + " | " + \
           " ".join("%7.3f" % v for v in m[4:8]) + " | " + \
           " ".join("%7.3f" % v for v in m[8:12]) + "]"


def main() -> None:
    path = sys.argv[1] if len(sys.argv) > 1 else "D:/westwood/RA2YR/ra2.mix"
    m = MixFile(path)
    vxls, hvas = [], []
    for owner, h, off, size, depth in iter_leaves(m):
        # 注意别加 size 下限：1 帧 1 肢的 HVA 只有 24+16+48 = 88 字节，
        # 之前写 size < 1024 直接跳过，183 个 HVA 只剩 1 个（踩过）。
        d = owner.read(off, size)
        if size >= 15 and d[:15] == b"Voxel Animation":
            try:
                vxls.append((h, VxlFile(d), d))
            except VxlError:
                pass
            continue
        names = hva_limb_names(d)
        if names:
            frames, limbs = struct.unpack_from("<2I", d, 16)
            hvas.append((h, names, frames, limbs, d))
    print("VXL %d 个，HVA %d 个" % (len(vxls), len(hvas)))

    # 按肢体名做索引
    by_names = {}
    for h, names, frames, limbs, d in hvas:
        by_names.setdefault(tuple(names), []).append((h, frames, limbs, d))

    paired = 0
    shown = 0
    multi = []
    for vh, v, d in vxls:
        names = tuple(v.headers[i].name for i in range(v.num_limbs))
        cand = by_names.get(names)
        if not cand:
            continue
        paired += 1
        hh, frames, limbs, hd = cand[0]
        if frames > 1 or v.num_limbs > 1:
            multi.append((vh, v, hh, hd, frames, limbs))
        if shown < 4:
            shown += 1
            print("\n配对 VXL 0x%08X  <- HVA 0x%08X  %d 帧 x %d 肢  名=%s"
                  % (vh, hh, frames, limbs, list(names)))
            for l in range(min(v.num_limbs, 3)):
                t = v.tailers[l]
                print("  limb%d VXL 尾 : %s" % (l, fmt(tuple(t.transform))))
                print("  limb%d HVA f0 : %s" % (l, fmt(hva_matrix(hd, 0, l, limbs))))
                if frames > 1:
                    print("  limb%d HVA f1 : %s" % (l, fmt(hva_matrix(hd, 1, l, limbs))))
    print("\n按名字配对成功 %d / %d 个 VXL" % (paired, len(vxls)))
    print("多肢或多帧的配对 %d 个，前几个：" % len(multi))
    for vh, v, hh, hd, frames, limbs in multi[:8]:
        names = [v.headers[i].name for i in range(v.num_limbs)]
        print("  VXL 0x%08X (%d 肢) <- HVA 0x%08X (%d 帧) %s" % (vh, v.num_limbs, hh,
                                                              frames, names))


if __name__ == "__main__":
    main()
