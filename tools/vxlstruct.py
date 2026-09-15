"""vxlstruct.py -- VXL 外层结构的解析与转储（Python 参考实现）。

【已确认的布局】经 184 个样本验证 `32 + 770*NP + 28*L + BodySize + 92*T == size` 100% 成立。

  +0   16B  "Voxel Animation"
  +16  u32  unknown / palette_count（恒 1）
  +20  u32  NumLimbs              L
  +24  u32  NumLimbFrames        恒等于 NumLimbs
  +28  u32  BodySize
  +32  u8   remapStart (16)
  +33  u8   remapEnd   (31)
  +34  768B RGB 调色板                       -> 头尾 = 802 = 32 + 770*NP
  +802 L×28 肢体头: name[16] + number u32 + unk1 u32 + unk2 u32
       BodySize 字节的 body（span 数组 + span 数据）
       T×92 肢体尾:
           +0  u32 spanStartOfs
           +4  u32 spanEndOfs
           +8  u32 spanDataOfs
           +12 f32 det(缩放)
           +16 f32[12] transform
           +64 f32[3]  minBounds
           +76 f32[3]  maxBounds
           +88 u8 XSize, +89 YSize, +90 ZSize, +91 NormalsType

body 内每肢体三段 u32 数组，各 X*Y 项：spanStart / spanEnd / spanData。
空列在三张表里都是 0xFFFFFFFF。
"""

from __future__ import annotations

import struct
import sys

HEADER_BASE = 32          # 魔数 16 + u32×4
PALETTE_BLOCK = 770       # 2 字节 remap 区间 + 768 字节调色板
LIMB_HEADER = 28
LIMB_TAILER = 92
NO_SPAN = 0xFFFFFFFF


class VxlError(Exception):
    pass


class LimbTailer:
    __slots__ = ("span_start_ofs", "span_end_ofs", "span_data_ofs", "det",
                 "transform", "min_bounds", "max_bounds",
                 "x", "y", "z", "normals_type")


class LimbHeader:
    __slots__ = ("name", "number", "unk1", "unk2")


class VxlFile:
    def __init__(self, data: bytes):
        self.raw = data
        self._parse()

    # ---------------------------------------------------------------- 头
    def _parse(self):
        d = self.raw
        if len(d) < HEADER_BASE:
            raise VxlError("短于 32 字节")
        if d[:15] != b"Voxel Animation":
            raise VxlError("魔数不是 'Voxel Animation'")
        (self.palette_count, self.num_limbs,
         self.num_limb_frames, self.body_size) = struct.unpack_from("<4I", d, 16)
        self.remap_start, self.remap_end = d[32], d[33]

        self.header_size = HEADER_BASE + PALETTE_BLOCK * self.palette_count
        self.palette = d[34:34 + 768]
        self.palette_count_eff = self.palette_count if self.palette_count else 1

        limb_hdr_end = self.header_size + LIMB_HEADER * self.num_limbs
        body_start = limb_hdr_end
        body_end = body_start + self.body_size
        tailer_start = body_end
        expect = tailer_start + LIMB_TAILER * self.num_limbs
        if expect != len(d):
            raise VxlError(
                "长度不符: 期望 %d 实际 %d (pal=%d limbs=%d body=%d)"
                % (expect, len(d), self.palette_count, self.num_limbs, self.body_size))
        self.body_start = body_start
        self.body = d[body_start:body_end]

        # 肢体头
        self.headers = []
        for i in range(self.num_limbs):
            o = self.header_size + i * LIMB_HEADER
            h = LimbHeader()
            h.name = d[o:o + 16].split(b"\x00")[0].decode("ascii", "replace")
            h.number, h.unk1, h.unk2 = struct.unpack_from("<3I", d, o + 16)
            self.headers.append(h)

        # 肢体尾
        self.tailers = []
        for i in range(self.num_limbs):
            o = tailer_start + i * LIMB_TAILER
            t = LimbTailer()
            (t.span_start_ofs, t.span_end_ofs, t.span_data_ofs,
             t.det) = struct.unpack_from("<3If", d, o)
            t.transform = list(struct.unpack_from("<12f", d, o + 16))
            t.min_bounds = list(struct.unpack_from("<3f", d, o + 64))
            t.max_bounds = list(struct.unpack_from("<3f", d, o + 76))
            t.x, t.y, t.z, t.normals_type = d[o + 88], d[o + 89], d[o + 90], d[o + 91]
            self.tailers.append(t)

    # ---------------------------------------------------------------- span 表
    def span_tables(self, i: int):
        """返回肢体 i 的两张表 start/end（各 X*Y 个 u32）。

        **只有两张表**。tailer 里第三个 u32（span_data_ofs）不是表，而是
        "数据块"在 body 里的起点 —— 硬证据：多数肢体上 span_data_ofs + X*Y*4
        会直接越出 body。表里的值是**相对数据块**的字节偏移，空列恒 0xFFFFFFFF。
        """
        t = self.tailers[i]
        n = t.x * t.y
        st = struct.unpack_from("<%dI" % n, self.body, t.span_start_ofs)
        en = struct.unpack_from("<%dI" % n, self.body, t.span_end_ofs)
        return st, en

    def column_bytes(self, i: int, col: int) -> bytes | None:
        """肢体 i 第 col 列的原始字节（已按 start/end 裁好）。空列返回 None。"""
        st, en = self.span_tables(i)
        if st[col] == NO_SPAN:
            return None
        base = self.tailers[i].span_data_ofs
        return self.body[base + st[col]: base + en[col] + 1]

    # ---------------------------------------------------------------- span 解码
    def decode_column(self, limb: int, col: int, strict: bool = True):
        """解出肢体 limb 第 col 列的体素，返回 [(z, colour, normal), ...]。

        【列内编码】2026-09-15 实测破解（156536 列零异常）：
            游标 z = 0，反复读：
                delta  u8   游程起点 z += delta
                n      u8   本游程体素数；n == 0 即终止符
                n × 2B      每个体素 (colour, normal)
                n      u8   计数重复一遍（渲染器要反向遍历 span）
                z += n
            列尾终止符 = [delta][0][0]，delta 是最后一个体素之上剩下的空格数；
            若最后一个游程刚好填满到 Z，终止符整个省略。
        """
        b = self.column_bytes(limb, col)
        if b is None:
            return []
        z_size = self.tailers[limb].z
        out = []
        i, z = 0, 0
        while i < len(b):
            if i + 2 > len(b):
                if strict:
                    raise VxlError("列 %d 在 %d 处只剩 %d 字节，不足一个 span 头"
                                   % (col, i, len(b) - i))
                break
            z += b[i]
            n = b[i + 1]
            i += 2
            if n == 0:                       # 终止符
                if i >= len(b):
                    raise VxlError("列 %d 终止符缺重复字节" % col)
                if strict and b[i] != 0:
                    raise VxlError("列 %d 终止符重复字节为 %d（应为 0）" % (col, b[i]))
                i += 1
                if strict and z != z_size:
                    raise VxlError("列 %d 终止符 delta 落在 z=%d，应为 Z=%d"
                                   % (col, z, z_size))
                break
            if z + n > z_size:
                raise VxlError("列 %d 游程 z=%d n=%d 越出 Z=%d" % (col, z, n, z_size))
            if i + n * 2 + 1 > len(b):
                raise VxlError("列 %d 游程数据不足" % col)
            for k in range(n):
                out.append((z + k, b[i], b[i + 1]))
                i += 2
            if strict and b[i] != n:
                raise VxlError("列 %d 计数重复字节 %d != n %d" % (col, b[i], n))
            i += 1
            z += n
        if strict and i != len(b):
            raise VxlError("列 %d 还剩 %d 字节未消费" % (col, len(b) - i))
        return out

    def voxels(self, limb: int):
        """产出 (x, y, z, colour, normal)。"""
        t = self.tailers[limb]
        for i in range(t.x * t.y):
            x, y = i % t.x, i // t.x
            for z, c, n in self.decode_column(limb, i):
                yield x, y, z, c, n

    def summary(self) -> str:
        return ("VXL limbs=%d frames=%d body=%d pal=%d remap=(%d,%d) size=%d"
                % (self.num_limbs, self.num_limb_frames, self.body_size,
                   self.palette_count, self.remap_start, self.remap_end, len(self.raw)))


def dump(v: VxlFile, limb: int = 0, max_cols: int = 40, verbose: bool = True):
    print(v.summary())
    print("header_size=%d body_start=%d" % (v.header_size, v.body_start))
    for i, (h, t) in enumerate(zip(v.headers, v.tailers)):
        print("  [%2d] %-12s num=%d unk1=%d unk2=%d  %dx%dx%d nt=%d det=%g"
              "  ofs=(%d,%d,%d) bounds=(%.3f,%.3f,%.3f)-(%.3f,%.3f,%.3f)"
              % (i, h.name, h.number, h.unk1, h.unk2, t.x, t.y, t.z, t.normals_type,
                 t.det, t.span_start_ofs, t.span_end_ofs, t.span_data_ofs,
                 t.min_bounds[0], t.min_bounds[1], t.min_bounds[2],
                 t.max_bounds[0], t.max_bounds[1], t.max_bounds[2]))
    if not verbose:
        return
    t = v.tailers[limb]
    st, en, da = v.span_tables(limb)
    print("\n-- limb %d span 表（前 %d/%d 列）--" % (limb, min(max_cols, len(st)), len(st)))
    for i in range(min(max_cols, len(st))):
        x, y = i % t.x, i // t.x
        if st[i] == NO_SPAN:
            print("  [%2d] (%2d,%2d) 空" % (i, x, y))
            continue
        print("  [%2d] (%2d,%2d) start=%-6d end=%-6d data=%-6d  d-s=%-5d e-d=%-5d"
              % (i, x, y, st[i], en[i], da[i], en[i] - st[i], da[i] - en[i]))


def main() -> None:
    if len(sys.argv) < 2:
        print(__doc__)
        print("用法: python tools/vxlstruct.py <file.vxl> [limb]")
        sys.exit(1)
    v = VxlFile(open(sys.argv[1], "rb").read())
    dump(v, int(sys.argv[2]) if len(sys.argv) > 2 else 0)


if __name__ == "__main__":
    main()
