"""
remapprobe.py -- 找 RA2 的阵营色 remap 区。

思路（不猜，靠数据）：
  1) 在 MIX 里扫所有 768 字节的条目当 .PAL，且要求全部分量 <= 63（6 bit 值域）。
  2) 把 256 色按 16 个一行打印，人工/程序找"一段同一色相的等距渐变"——
     那就是 remap 基色区（原版会把这块整段换成阵营色）。
  3) 拿单位 SHP 的像素索引直方图去对：单位若集中用某段索引，
     而那段在调色板里恰好是渐变，即可交叉确认。

用法：
  python tools/remapprobe.py pal   D:\\westwood\\RA2YR\\ra2.mix
  python tools/remapprobe.py hist  D:\\westwood\\RA2YR\\ra2.mix E1.SHP
"""

from __future__ import annotations

import sys

sys.path.insert(0, r"E:\ra2source\tools")
import mixdump  # noqa: E402


def crc_name(name: str) -> int:
    return mixdump.westwood_crc(name)


def looks_like_pal(d: bytes) -> bool:
    if len(d) != 768:
        return False
    # 6 bit 值域：每个分量 <= 63
    if any(b > 63 for b in d):
        return False
    # 全 0 的不要
    return any(d)


def hexdump_pal(d: bytes, label: str) -> None:
    print("\n=== %s ===" % label)
    for row in range(16):
        cells = []
        for c in range(16):
            i = row * 16 + c
            r, g, b = d[i * 3], d[i * 3 + 1], d[i * 3 + 2]
            cells.append("%02x%02x%02x" % (r * 4, g * 4, b * 4))
        print("  %02X: %s" % (row * 16, " ".join(cells)))


def ramp_score(d: bytes) -> float:
    """0..1，衡量"前 256 色里是否存在明显的等距渐变段"。

    逐行（16 色）看相邻色的亮度差是否单调且幅度接近。
    """
    best = 0.0
    for row in range(16):
        lum = []
        for c in range(16):
            i = row * 16 + c
            r, g, b = d[i * 3], d[i * 3 + 1], d[i * 3 + 2]
            lum.append(0.299 * r + 0.587 * g + 0.114 * b)
        diffs = [lum[k + 1] - lum[k] for k in range(15)]
        if not diffs:
            continue
        pos = sum(1 for x in diffs if x > 0.5)
        neg = sum(1 for x in diffs if x < -0.5)
        mono = max(pos, neg)
        span = max(lum) - min(lum)
        if span < 8:
            continue
        best = max(best, (mono / 15.0) * min(1.0, span / 40.0))
    return best


def main() -> None:
    args = sys.argv[1:]
    if len(args) < 2:
        print(__doc__)
        sys.exit(1)
    mode = args[0]
    path = args[1]

    m = mixdump.MixFile(path)

    if mode == "pal":
        pals = []
        leaves = 0
        for owner, h, off, size, depth in mixdump.iter_leaves(m):
            leaves += 1
            if size != 768:
                continue
            d = owner.read(off, size)
            if looks_like_pal(d):
                pals.append((h, d))
        print("[i] %d 个叶子条目，其中 %d 个像 .PAL（768 字节 + 6bit 值域）"
              % (leaves, len(pals)))
        ranked = sorted(pals, key=lambda t: -ramp_score(t[1]))
        for h, d in ranked:
            sc = ramp_score(d)
            if sc < 0.55:
                continue
            hexdump_pal(d, "0x%08X  ramp=%.2f" % (h, sc))
        return

    if mode == "pal1":
        # 按名字 dump 一张调色板的前 64 色
        want = mixdump.westwood_crc(args[2])
        for owner, h, off, size, _d in mixdump.iter_leaves(m):
            if h == want:
                hexdump_pal(owner.read(off, size), args[2])
                return
        print("[x] 没找到 %s" % args[2])
        return

    if mode == "colors":
        # 找含 [Colors] 的 INI（rules.ini 里的玩家阵营色表）
        for owner, h, off, size, depth in mixdump.iter_leaves(m):
            if size > 2 << 20 or size < 512:
                continue
            d = owner.read(off, size)
            if b"[Colors]" not in d:
                continue
            txt = d.decode("latin-1")
            i = txt.index("[Colors]")
            seg = txt[i:i + 900]
            end = seg.find("[", 5)
            print("\n=== 0x%08X  size=%d ===" % (h, size))
            print(seg[:end if end > 0 else 900])
            return
        print("[x] 没有找到含 [Colors] 的条目")
        return

    if mode == "hist":
        name = args[2]
        want = mixdump.westwood_crc(name)
        hit = None
        for owner, h, off, size, _depth in mixdump.iter_leaves(m):
            if h == want:
                hit = (owner, h, off, size)
                break
        if not hit:
            print("[x] 没找到 %s" % name)
            sys.exit(1)
        owner, h, off, size = hit
        d = owner.read(off, size)
        import struct
        _z, w, hh, n = struct.unpack_from("<HHHH", d, 0)
        print("[i] %s  %dx%d  %d 帧" % (name, w, hh, n))
        hist = [0] * 256
        for f in range(n):
            fo = 8 + f * 24
            fw, fh = struct.unpack_from("<HH", d, fo + 4)
            flags, = struct.unpack_from("<I", d, fo + 8)
            doff, = struct.unpack_from("<I", d, fo + 20)
            if flags & 2:
                px = decode_rle(d[doff:], fw, fh)
            else:
                px = list(d[doff:doff + fw * fh])
            for p in px:
                hist[p] += 1
        total = sum(hist)
        print("[i] 总计 %d 像素，索引直方图（只列 >0 的）：" % total)
        for row in range(16):
            parts = []
            for c in range(16):
                i = row * 16 + c
                if hist[i]:
                    parts.append("%02X:%d" % (i, hist[i]))
            if parts:
                print("  %02X: %s" % (row * 16, " ".join(parts)))
        return

    print(__doc__)


def decode_rle(src: bytes, w: int, h: int) -> list:
    """Westwood RLE-Zero：每行 u16 行字节数，行内 0x00 后跟透明游程计数。"""
    out = []
    p = 0
    for _y in range(h):
        if p + 2 > len(src):
            break
        row_len = int.from_bytes(src[p:p + 2], "little")
        body = src[p + 2:p + row_len]
        p += row_len
        line = []
        i = 0
        while i < len(body):
            v = body[i]
            if v == 0:
                i += 1
                if i >= len(body):
                    break
                line.extend([0] * body[i])
                i += 1
            else:
                line.append(v)
                i += 1
        line = line[:w]
        line += [0] * (w - len(line))
        out.extend(line)
    return out


if __name__ == "__main__":
    main()
