"""
mixdump.py -- 解开 RA2/YR 加密 MIX 归档（Python 参考实现，已实测通过）。

为什么先写 Python 版：
  加密 MIX 是"看到任何游戏素材"的唯一关卡 —— ra2.mix / ra2md.mix / expandmd01.mix
  全部带 0x00020000（加密）。在搬进 C++ 之前需要快速试参数组合并立刻判断对错。
  验证通过后照抄成 src/io/MixCrypto.cpp。

【已验证的算法】2026-09-15 在 ra2md.mix 上跑通：
  偏移 0    4 字节   flags（明文）
  偏移 4    80 字节  RSA 加密的密钥源 = 两个 40 字节**小端**大整数
  偏移 84            Blowfish 加密的索引：
                       u16 file_count + u32 data_size + file_count×12 字节条目
                     尾部补零到 8 字节对齐；若 flags 带 0x00010000，
                     文件末尾还有 20 字节 SHA1（data_size 不含它）

  RSA：m = c^65537 mod n，n = base64 "AihRvNoIbTn85FZRYNZRcT+i6KpU+maCsEqr3Q5
       q+LDB5tH7Tz2qQ38V" 解出的 42 字节里去掉 DER 头 "02 28" 后的 40 字节大端整数。
       每个分组的结果取 **39 字节小端**（= (bitlen(n)-1)/8），两个拼起来取前 56 字节。

  Blowfish：56 字节密钥，**标准大端**（两个 32 位半字按大端读写），ECB。

  自洽校验（ra2md.mix）：file_count=25、data_size=204527280，
  而 204527696(文件) - 396(4+80+索引) - 20(SHA1) = 204527280。

用法：
  python tools/mixdump.py list  D:\\westwood\\RA2YR\\ra2md.mix
  python tools/mixdump.py find  D:\\westwood\\RA2YR\\ra2md.mix  RULES.INI
  python tools/mixdump.py get   D:\\westwood\\RA2YR\\ra2md.mix  <文件名>  out.bin
"""

from __future__ import annotations

import base64
import struct
import sys

# ---------------------------------------------------------------- 公钥

PUBKEY_B64 = "AihRvNoIbTn85FZRYNZRcT+i6KpU+maCsEqr3Q5q+LDB5tH7Tz2qQ38V"
PUBLIC_EXPONENT = 65537

_DER = base64.b64decode(PUBKEY_B64)
assert _DER[:2] == b"\x02\x28", "DER 头应为 02 28（INTEGER, 40 字节）"
PUBLIC_MODULUS = int.from_bytes(_DER[2:], "big")

KEY_SOURCE_LEN = 80        # 头部里 RSA 加密的密钥源长度
RSA_CHUNK = 40             # 每个大整数的字节数（小端）
RSA_OUT = 39               # 每个结果保留的字节数 = (bitlen(n)-1)/8
BLOWFISH_KEY_LEN = 56


# ---------------------------------------------------------------- Blowfish
#
# P 盒与 S 盒是圆周率小数部分的十六进制展开。这里用 Machin 公式
#   pi = 16*atan(1/5) - 4*atan(1/239)
# 在整数定点上算出来，既不依赖第三方库，也避免手抄 1042 个常数抄错。

def _atan_inv(x: int, one: int) -> int:
    """atan(1/x) * one，整数定点。"""
    total = 0
    term = one // x
    x2 = x * x
    k = 0
    while term:
        if k % 2 == 0:
            total += term // (2 * k + 1)
        else:
            total -= term // (2 * k + 1)
        term //= x2
        k += 1
    return total


def _pi_hex_digits(n_digits: int) -> str:
    bits = n_digits * 4 + 64
    one = 1 << bits
    pi = 16 * _atan_inv(5, one) - 4 * _atan_inv(239, one)
    frac = pi - 3 * one
    out = []
    for _ in range(n_digits):
        out.append("%x" % (frac >> (bits - 4)))
        frac = (frac & ((1 << (bits - 4)) - 1)) << 4
    return "".join(out)


def _init_boxes():
    hexd = _pi_hex_digits(18 * 8 + 1024 * 8)
    words = [int(hexd[i:i + 8], 16) for i in range(0, len(hexd), 8)]
    assert words[0] == 0x243F6A88, "pi 展开错：P[0]=0x%08X" % words[0]
    assert words[17] == 0x8979FB1B, "pi 展开错：P[17]=0x%08X" % words[17]
    assert words[18] == 0xD1310BA6, "pi 展开错：S[0][0]=0x%08X" % words[18]
    return words[:18], [words[18 + i * 256:18 + (i + 1) * 256] for i in range(4)]


_P_INIT, _S_INIT = _init_boxes()


class Blowfish:
    """标准 Blowfish（大端）。ECB 模式，只用到解密。"""

    def __init__(self, key: bytes):
        if not key:
            raise ValueError("key 不能为空")
        self.p = list(_P_INIT)
        self.s = [list(x) for x in _S_INIT]
        kl = len(key)
        for i in range(18):
            k = 0
            for j in range(4):
                k = ((k << 8) | key[(i * 4 + j) % kl]) & 0xFFFFFFFF
            self.p[i] ^= k
        l = r = 0
        for i in range(0, 18, 2):
            l, r = self._enc(l, r)
            self.p[i], self.p[i + 1] = l, r
        for i in range(4):
            for j in range(0, 256, 2):
                l, r = self._enc(l, r)
                self.s[i][j], self.s[i][j + 1] = l, r

    def _f(self, x: int) -> int:
        s0, s1, s2, s3 = self.s
        return (((s0[(x >> 24) & 0xFF] + s1[(x >> 16) & 0xFF]) & 0xFFFFFFFF)
                ^ s2[(x >> 8) & 0xFF]) + s3[x & 0xFF] & 0xFFFFFFFF

    def _enc(self, l: int, r: int):
        p = self.p
        for i in range(16):
            l ^= p[i]
            r ^= self._f(l)
            l, r = r, l
        l, r = r, l
        return (l ^ p[17]) & 0xFFFFFFFF, (r ^ p[16]) & 0xFFFFFFFF

    def _dec(self, l: int, r: int):
        p = self.p
        for i in range(17, 1, -1):
            l ^= p[i]
            r ^= self._f(l)
            l, r = r, l
        l, r = r, l
        return (l ^ p[0]) & 0xFFFFFFFF, (r ^ p[1]) & 0xFFFFFFFF

    def decrypt(self, data: bytes) -> bytes:
        if len(data) % 8:
            raise ValueError("Blowfish 输入必须是 8 字节的倍数")
        out = bytearray()
        for i in range(0, len(data), 8):
            l = int.from_bytes(data[i:i + 4], "big")
            r = int.from_bytes(data[i + 4:i + 8], "big")
            l, r = self._dec(l, r)
            out += l.to_bytes(4, "big") + r.to_bytes(4, "big")
        return bytes(out)


# ---------------------------------------------------------------- MIX

def derive_blowfish_key(blob: bytes) -> bytes:
    """从头部 80 字节密钥源推出 56 字节 Blowfish 密钥。"""
    src = blob[4:4 + KEY_SOURCE_LEN]
    out = b""
    for i in range(0, KEY_SOURCE_LEN, RSA_CHUNK):
        c = int.from_bytes(src[i:i + RSA_CHUNK], "little")
        if c >= PUBLIC_MODULUS:
            raise ValueError("分组 %d 不小于模数，不可逆" % (i // RSA_CHUNK))
        m = pow(c, PUBLIC_EXPONENT, PUBLIC_MODULUS)
        out += m.to_bytes(RSA_OUT, "little")
    return out[:BLOWFISH_KEY_LEN]


def westwood_crc(name: str) -> int:
    """MIX 文件名哈希：大写 + 特殊填充 + CRC32。"""
    s = name.upper().encode("ascii")
    rem = len(s) % 4
    if rem:
        pad = s[(len(s) // 4) * 4]
        s += bytes([rem]) + bytes([pad]) * (3 - rem)
    crc = 0xFFFFFFFF
    for ch in s:
        crc ^= ch
        for _ in range(8):
            crc = (crc >> 1) ^ (0xEDB88320 if crc & 1 else 0)
    return (~crc) & 0xFFFFFFFF


class MixFile:
    """用 seek 读，不全量载入 —— ra2.mix 有 281MB，整个读进内存不现实。"""

    def __init__(self, path: str = "", blob: bytes = b"",
                 parent: "MixFile | None" = None, parent_off: int = 0):
        # 支持嵌套：ra2md.mix 里装的子归档本身也是加密 MIX，
        # 所以既能从文件开，也能从内存块开。
        #
        # 但只把"索引那几百字节"读进内存是不够的 —— 真正的文件数据在
        # 父归档的数据区里。所以嵌套时还要记住 parent + 本归档在父里的偏移，
        # 读数据时回源到父，避免为了索引就把几十 MB 的子归档整个吞进来。
        self.path = path
        self.parent = parent
        self.parent_off = parent_off
        if blob:
            self._mem = blob
            self.fh = None
            self.file_size = len(blob)
        else:
            self._mem = b""
            self.fh = open(path, "rb")
            self.fh.seek(0, 2)
            self.file_size = self.fh.tell()
        self.blob = self._at(0, 4 + KEY_SOURCE_LEN + 8)
        self.flags = struct.unpack_from("<I", self.blob, 0)[0]
        self.encrypted = bool(self.flags & 0x00020000)
        self.has_checksum = bool(self.flags & 0x00010000)
        if not self.encrypted:
            raise NotImplementedError("未加密 MIX 走 src/io/FileSystem.cpp 那条路")
        self.body = 4 + KEY_SOURCE_LEN
        bf = Blowfish(derive_blowfish_key(self.blob))
        head = bf.decrypt(self.blob[self.body:self.body + 8])
        self.count, self.data_size = struct.unpack_from("<HI", head, 0)
        need = 6 + self.count * 12
        enc_len = (need + 7) // 8 * 8
        raw = bf.decrypt(self._at(self.body, enc_len))[:need]
        self.entries = [struct.unpack_from("<III", raw, 6 + i * 12)
                        for i in range(self.count)]
        self.data_start = self.body + enc_len

    def _at(self, off: int, n: int) -> bytes:
        if self.fh is not None:
            self.fh.seek(off)
            return self.fh.read(n)
        if self.parent is not None:
            # 本归档是嵌套的：偏移换算回父归档的数据区坐标。
            return self.parent.read(self.parent_off + off, n)
        return self._mem[off:off + n]

    def head_bytes(self) -> bytes:
        """本归档的头部（4+80+加密索引）原始字节，用于判断嵌套内容。"""
        return self._at(0, self.data_start)

    def close(self):
        if self.fh is not None:
            self.fh.close()

    def validate(self) -> int:
        bad = 0
        for h, off, size in self.entries:
            if off + size > self.data_size:
                bad += 1
        return bad

    def find(self, name: str):
        want = westwood_crc(name)
        for h, off, size in self.entries:
            if h == want:
                return off, size
        return None

    def read(self, off: int, size: int) -> bytes:
        return self._at(self.data_start + off, size)

    def head(self, off: int, n: int = 16) -> bytes:
        return self._at(self.data_start + off, n)


def open_nested(m: MixFile, off: int, size: int, limit: int = 4 << 20) -> MixFile | None:
    """尝试把 m 里 off 处的条目当成嵌套 MIX 打开。不是 MIX 就返回 None。"""
    blob = nested_blob(m, off, size, limit)
    if blob is None:
        return None
    return MixFile(blob=blob, parent=m, parent_off=off)


def nested_blob(m: MixFile, off: int, size: int, limit: int = 4 << 20) -> bytes | None:
    """把 m 里 off 处的条目当成可能的嵌套 MIX，取"足够建索引"的头部字节。

    子归档可能有几十 MB，没必要整个读进来 —— 索引只占前几百字节。
    但如果头部解出来根本不像 MIX（文件数离谱 / 数据长度对不上），返回 None。
    """
    first = m.read(off, min(size, 4 + KEY_SOURCE_LEN + 8))
    if len(first) < 4 + KEY_SOURCE_LEN + 8:
        return None
    flags = struct.unpack_from("<I", first, 0)[0]
    if not flags & 0x00020000:
        return None                      # 未加密，不归这里管
    try:
        bf = Blowfish(derive_blowfish_key(first))
        cnt, dsz = struct.unpack_from("<HI", bf.decrypt(first[84:92]), 0)
    except Exception:
        return None                      # RSA 分组 >= 模数，或解出来是噪声
    if cnt == 0 or cnt > 20000 or dsz > size:
        return None
    enc_len = (6 + cnt * 12 + 7) // 8 * 8
    if enc_len > limit or off + 92 + enc_len > m.data_size:
        return None
    rest = m.read(off + 92, enc_len - 8)
    if len(rest) < enc_len - 8:
        return None
    return first + rest


# ---------------------------------------------------------------- 内容识别
#
# MIX 不存文件名，只有 CRC。与其穷举文件名，不如直接看内容魔数 —— 对我们
# 来说"这是个 SHP"比"它叫什么"更有用。

def classify(head: bytes, size: int) -> str:
    if size == 768:
        return "PAL"          # 调色板
    if head[:2] == b"\x0a\x05" or head[:1] == b"\x0a":
        return "PCX?"
    if head[:4] == b"Voxel" or head[:5] == b"Voxel":
        return "VXL"
    if head[:2] == b"\x00\x00" and size > 16:
        # RA2 SHP: u16 0, u16 width, u16 height, u16 frames
        return "SHP?"
    try:
        t = head[:64].decode("ascii")
    except UnicodeDecodeError:
        return "?"
    if t.lstrip().startswith("[") or "=" in t[:64]:
        return "INI/TXT"
    return "?"


def iter_leaves(m: MixFile, depth: int = 0, max_depth: int = 4):
    """递归产出 (所在归档, off, size, 深度)。嵌套 MIX 会被继续剥开。"""
    for h, off, size in m.entries:
        sub = open_nested(m, off, size) if depth < max_depth else None
        if sub is None:
            yield m, h, off, size, depth
        else:
            yield from iter_leaves(sub, depth + 1, max_depth)


def main() -> None:
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    cmd, path = sys.argv[1], sys.argv[2]
    m = MixFile(path)
    print("%s  flags=0x%08X 条目=%d 数据区=%d 起点=%d 越界条目=%d" % (
        path, m.flags, m.count, m.data_size, m.data_start, m.validate()))
    print("文件 %d 字节；尾部 SHA1 %s" % (
        len(m.blob), "有" if m.has_checksum else "无"))

    if cmd == "list":
        for h, off, size in m.entries:
            print("  id=0x%08X  off=%-10d size=%-10d" % (h, off, size))
    elif cmd == "tree":
        # 递归：子归档本身也是加密 MIX，逐层剥开并打印内容类型。
        total = 0
        for h, off, size in m.entries:
            sub = open_nested(m, off, size)
            if sub is None:
                kind = classify(m.head(off, 16), size)
                print("  id=0x%08X  %-12s size=%-10d" % (h, kind, size))
                total += 1
                continue
            print("  id=0x%08X  MIX          size=%-10d flags=0x%08X 条目=%d 越界=%d" % (
                h, size, sub.flags, sub.count, sub.validate()))
            for sh, soff, ssize in sub.entries:
                kind = classify(sub.head(soff, 16), ssize)
                print("      id=0x%08X  %-12s size=%d" % (sh, kind, ssize))
                total += 1
        print("  合计叶子条目 %d" % total)
    elif cmd == "dump" and len(sys.argv) >= 5:
        # dump <mix> <0xID> <out> —— 按 CRC 直接取，不知道文件名也能拿内容。
        want = int(sys.argv[3], 16)
        for owner, h, off, size, depth in iter_leaves(m):
            if h != want:
                continue
            data = owner.read(off, size)
            open(sys.argv[4], "wb").write(data)
            print("[OK] id=0x%08X depth=%d size=%d -> %s" % (h, depth, size, sys.argv[4]))
            print("     前 16 字节: %s" % data[:16].hex(" "))
            break
        else:
            print("[x] 没有 id=0x%08X" % want)
            sys.exit(1)
    elif cmd in ("find", "get") and len(sys.argv) >= 4:
        r = m.find(sys.argv[3])
        if not r:
            print("[x] 归档里没有 %s (crc=0x%08X)" % (sys.argv[3], westwood_crc(sys.argv[3])))
            sys.exit(1)
        off, size = r
        print("[OK] %s  off=%d size=%d" % (sys.argv[3], off, size))
        if cmd == "get":
            open(sys.argv[4], "wb").write(m.read(off, size))
            print("     已写出 %s" % sys.argv[4])


if __name__ == "__main__":
    main()
