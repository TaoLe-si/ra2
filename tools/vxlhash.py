"""vxlhash.py -- VXL 哈希对账（Python 参考实现）。

喂给 FNV-1a 的字节序列必须和 src/main.cpp 的 Hash_One_Vxl **逐字节一致**：

    u32 palette_count, num_limbs, num_limb_frames, body_size
    u8  remap_start, remap_end
    768B 调色板
    每根肢体：
      u32 名字长度 + 名字字节 + i32 number + u32 unk1 + u32 unk2
      u32 span_start_ofs, span_end_ofs, span_data_ofs, det(float 原样 4 字节)
      f32×12 变换 + f32×3 min + f32×3 max
      u8 x_size, y_size, z_size, normals_type
      u32 体素数 + 每体素 5 字节 (x, y, z, colour, normal)

用法: python tools/vxlhash.py D:\\westwood\\RA2YR\\ra2.mix > build/vxl_py.txt
"""
from __future__ import annotations

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mixdump import MixFile, iter_leaves          # noqa: E402
from vxlstruct import VxlFile, VxlError           # noqa: E402

FNV_OFFSET = 2166136261
FNV_PRIME = 16777619


def fnv(data: bytes, h: int = FNV_OFFSET) -> int:
    for b in data:
        h = ((h ^ b) * FNV_PRIME) & 0xFFFFFFFF
    return h


def hash_one(v: VxlFile) -> int:
    h = FNV_OFFSET
    h = fnv(struct.pack("<4I", v.palette_count, v.num_limbs, v.num_limb_frames,
                        v.body_size), h)
    h = fnv(bytes((v.remap_start, v.remap_end)), h)
    h = fnv(v.palette, h)
    for i in range(v.num_limbs):
        hd, t = v.headers[i], v.tailers[i]
        nb = hd.name.encode("ascii")
        h = fnv(struct.pack("<I", len(nb)) + nb, h)
        h = fnv(struct.pack("<iII", hd.number, hd.unk1, hd.unk2), h)
        h = fnv(struct.pack("<3I", t.span_start_ofs, t.span_end_ofs, t.span_data_ofs), h)
        h = fnv(struct.pack("<f", t.det), h)
        h = fnv(struct.pack("<12f", *t.transform), h)
        h = fnv(struct.pack("<3f", *t.min_bounds), h)
        h = fnv(struct.pack("<3f", *t.max_bounds), h)
        h = fnv(bytes((t.x, t.y, t.z, t.normals_type)), h)
        # 体素必须按"列序 → 列内 z 升序"打包，和 C++ Decode_Limb 一致。
        packed = bytearray()
        for c in range(t.x * t.y):
            x, y = c % t.x, c // t.x
            for z, col, nrm in v.decode_column(i, c):
                packed += bytes((x, y, z, col, nrm))
        h = fnv(struct.pack("<I", len(packed) // 5), h)
        h = fnv(bytes(packed), h)
    return h


def main() -> None:
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    path = sys.argv[1]
    m = MixFile(path)
    print("# mix=%s" % path)
    seq = ok = 0
    for owner, h, off, size, depth in iter_leaves(m):
        if size < 802 or owner.read(off, 15) != b"Voxel Animation":
            continue
        data = owner.read(off, size)
        try:
            v = VxlFile(data)
        except VxlError as e:
            print("BAD %d 0x%08X %s" % (seq, h, e))
            seq += 1
            continue
        nvox = 0
        for i in range(v.num_limbs):
            t = v.tailers[i]
            for c in range(t.x * t.y):
                nvox += len(v.decode_column(i, c, strict=False))
        print("%d 0x%08X limbs=%d body=%d vox=%d 0x%08X"
              % (seq, h, v.num_limbs, v.body_size, nvox, hash_one(v)))
        seq += 1
        ok += 1
    print("# 共 %d 个 VXL" % ok)


if __name__ == "__main__":
    main()
