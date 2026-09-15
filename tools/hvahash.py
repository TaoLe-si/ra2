"""
hvahash.py -- HVA 的 Python 参考实现，用于和 ra2core --hvahash 对账。

HVA **没有魔数**（游戏按 `模型名 + ".HVA"` 拼名去 MIX 里查），所以这里和 C++
一样只能对每个叶子条目硬试一次解析，靠长度算术判定：

    文件长度 == 24 + 16*LimbCount + 48*FrameCount*LimbCount

哈希喂进去的顺序（必须和 src/main.cpp 的 Hash_Hva 逐字节一致）：
    u32 FrameCount、u32 LimbCount（小端各 4 字节）
    每根肢体名的原始字节（已按第一个 NUL 截断，不含 NUL）
    每帧每肢体的 12 个 float（小端）

用法：
  python tools/hvahash.py D:\\westwood\\RA2YR\\ra2.mix > build/hva_py.txt
  ra2core --hvahash D:\\westwood\\RA2YR\\ra2.mix  > build/hva_cpp.txt
"""

from __future__ import annotations

import struct
import sys

sys.path.insert(0, r"E:\ra2source\tools")
import mixdump  # noqa: E402

HEADER = 24
LIMB_NAME = 16


class HvaError(Exception):
    pass


class Hva:
    def __init__(self, data: bytes):
        if len(data) < HEADER:
            raise HvaError("短于 24 字节")
        self.path = data[:16].split(b"\x00")[0].decode("ascii", "replace")
        self.frames, self.limbs = struct.unpack_from("<II", data, 16)
        if self.frames == 0 or self.frames > 4096 or self.limbs == 0 or self.limbs > 256:
            raise HvaError("计数离谱 F=%d L=%d" % (self.frames, self.limbs))
        need = HEADER + self.limbs * LIMB_NAME + self.frames * self.limbs * 48
        if need != len(data):
            raise HvaError("长度 %d != 应有 %d" % (len(data), need))
        self.names = []
        off = HEADER
        for _ in range(self.limbs):
            n = data[off:off + LIMB_NAME].split(b"\x00")[0]
            if not any(0x20 <= c < 0x7F for c in n):
                raise HvaError("肢体名不可打印：%r" % n)
            self.names.append(n.decode("ascii", "replace"))
            off += LIMB_NAME
        self.mats = []
        for _ in range(self.frames * self.limbs):
            self.mats.append(struct.unpack_from("<12f", data, off))
            off += 48


def run(path: str, max_depth: int = 4) -> int:
    m = mixdump.MixFile(path)
    print("# mix=%s" % path)
    seq = ok = 0
    for owner, h, off, size, depth in mixdump.iter_leaves(m, max_depth=max_depth):
        d = owner.read(off, size)
        try:
            v = Hva(d)
        except HvaError:
            continue
        acc = 2166136261
        for b in struct.pack("<II", v.frames, v.limbs):
            acc = ((acc ^ b) * 16777619) & 0xFFFFFFFF
        for n in v.names:
            for b in n.encode("ascii"):
                acc = ((acc ^ b) * 16777619) & 0xFFFFFFFF
        for mat in v.mats:
            for b in struct.pack("<12f", *mat):
                acc = ((acc ^ b) * 16777619) & 0xFFFFFFFF
        print("%d 0x%08X %dx%d %s 0x%08X" % (seq, h, v.frames, v.limbs, v.names[0], acc))
        seq += 1
        ok += 1
    print("# 共 %d 个 HVA" % ok)
    return 0


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    sys.exit(run(sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 4))
