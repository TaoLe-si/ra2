"""
shptest.py -- 用"输出长度必须恰好等于 宽*高"这条硬判据，判定 SHP 帧压缩算法。

为什么需要它：SHP(TS) 的头/帧表布局已经从 moddingwiki 确认了，但帧数据的压缩方式
在不同资料里说法不一（RLE-Zero / LCW / Format80）。与其猜，不如写几个候选解码器，
拿真实文件跑 —— 只有正确那个能让输出恰好铺满一帧。
"""

from __future__ import annotations

import struct
import sys

sys.path.insert(0, r"E:\ra2source\tools")
import mixdump  # noqa: E402


def le16(b, i):
    return b[i] | (b[i + 1] << 8)


def decode_rle_zero(src, w, h):
    """moddingwiki 的 Westwood RLE-Zero（TS 变体）：
       每行开头 u16 = 该行输入长度（含这 2 字节）；行内遇到 00 则后一字节是重复计数。"""
    out = bytearray()
    p = 0
    for _ in range(h):
        if p + 2 > len(src):
            return None
        line = le16(src, p)
        p += 2
        end = p + line - 2
        if end > len(src):
            return None
        while p < end:
            v = src[p]
            p += 1
            if v == 0:
                if p >= end:
                    return None
                out += bytes(src[p])
                p += 1
            else:
                out.append(v)
    return bytes(out) if len(out) == w * h else None


def decode_rle_zero_nolen(src, w, h):
    """同上但没有行长前缀，全帧一条流。"""
    out = bytearray()
    p = 0
    while p < len(src) and len(out) < w * h:
        v = src[p]
        p += 1
        if v == 0:
            if p >= len(src):
                return None
            out += bytes(src[p])
            p += 1
        else:
            out.append(v)
    return bytes(out) if len(out) == w * h else None


def decode_lcw(src, w, h):
    """Westwood LCW / Format40。
       0x00       -> u16 长度，原样拷贝
       0x01-0x7F  -> 原样拷贝这么多字节
       0x80-0xBF  -> 回抄：count = cmd & 0x3F，偏移由后续字节给出
       0xC0-0xFF  -> 回抄：count = cmd & 0x3F + 3? 
    """
    out = bytearray()
    p = 0
    while len(out) < w * h:
        if p >= len(src):
            return None
        cmd = src[p]
        p += 1
        if cmd == 0:
            if p + 2 > len(src):
                return None
            n = le16(src, p)
            p += 2
            out += src[p:p + n]
            p += n
        elif cmd < 0x80:
            out += src[p:p + cmd]
            p += cmd
        else:
            count = cmd & 0x3F
            if cmd & 0x40:
                if count == 0:            # 0xC0: 长回抄
                    if p + 2 > len(src):
                        return None
                    count = le16(src, p)
                    p += 2
                    if p + 2 > len(src):
                        return None
                    off = le16(src, p)
                    p += 2
                    if count != 0xFFFF:
                        count += 3
                else:
                    if p + 2 > len(src):
                        return None
                    off = le16(src, p)
                    p += 2
                    count += 3
            else:
                if count == 0:            # 0x80: ???
                    return None
                if p + 1 > len(src):
                    return None
                off = (count << 8) | src[p]
                p += 1
                count = ((cmd >> 3) & 7) + 3   # 猜
            for _ in range(count):
                out.append(out[len(out) - off] if off <= len(out) else 0)
    return bytes(out) if len(out) == w * h else None


DECODERS = [
    ("RLE-Zero + 行长", decode_rle_zero),
    ("RLE-Zero 无行长", decode_rle_zero_nolen),
    ("LCW/Format40", decode_lcw),
]


def main():
    m = mixdump.MixFile(r"D:\westwood\RA2YR\ra2md.mix")
    targets = {0x1BB29153: "JAPI.SHP", 0x0370167B: "OBSALLI.SHP",
               0x02A7BD34: "AutoLoginQuery.shp", 0xD77312D5: "(unknown 12946)"}
    for owner, hid, off, size, depth in mixdump.iter_leaves(m):
        if hid not in targets:
            continue
        d = owner.read(off, size)
        w, h, nf = struct.unpack_from("<HHH", d, 0)
        fx, fy, fw, fh, flags = struct.unpack_from("<HHHHI", d, 8)
        doff = struct.unpack_from("<I", d, 28)[0]
        print("\n%s  %dx%d frames=%d | frame %d,%d %dx%d flags=0x%X data@%d (len %d)" % (
            targets[hid], w, h, nf, fx, fy, fw, fh, flags, doff, len(d) - doff))
        for f in range(min(nf, 3)):
            base = 8 + f * 24
            fx, fy, fw, fh, fl = struct.unpack_from("<HHHHI", d, base)
            d0 = struct.unpack_from("<I", d, base + 20)[0]
            d1 = struct.unpack_from("<I", d, base + 28)[0] if base + 32 <= len(d) else len(d)
            raw = d[d0:d1] if d1 > d0 else d[d0:]
            print("  帧%d flags=0x%X 数据 %d 字节, 期望像素 %d" % (f, fl, len(raw), fw * fh))
            for name, fn in DECODERS:
                try:
                    r = fn(raw, fw, fh)
                except Exception as e:
                    r = None
                print("     %-16s %s" % (name, "OK!" if r else "不匹配"))


if __name__ == "__main__":
    main()
