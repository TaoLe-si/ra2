"""
shpcrack.py -- 穷举 SHP(TS) 帧数据编码变体，用大量真实帧做判据。

判据是硬的：每行必须恰好产出 w 个像素、共 h 行。

各变体的差别集中在两点：
  1. 行首 u16 行长是否含这 2 个字节
  2. `00 NN`（透明游程）的计数语义，尤其是"行尾那个游程"

已确认（用 FULLFNT3.SHP 手工核对）：
  - flags 的 bit0(0x01)=HasTransparency、bit1(0x02)=UsesRle，0x2 和 0x3 共用同一套 RLE-Zero
  - 行长 u16 含自身
  - 行内游程 `00 NN` = NN 个透明像素（精确）
  - **行尾游程 `00 NN` 实际只有 NN-1 个** —— 编码器写多了 1

用法：
  python shpcrack.py              # 全量跑变体统计
  python shpcrack.py --glyph NAME # 画某个字体 SHP 的字形做肉眼验证
"""

from __future__ import annotations

import struct
import sys
from collections import Counter, defaultdict

sys.path.insert(0, r"E:\ra2source\tools")
import mixdump  # noqa: E402

MIXES = [r"D:\westwood\RA2YR\ra2.mix", r"D:\westwood\RA2YR\ra2md.mix"]
MAX_FRAMES = 200000


def u16(b, p):
    return b[p] | (b[p + 1] << 8)


# ---------------------------------------------------------------- 解码器
def dec_rle(src, w, h, incl=True, tail_minus1=False, clip=False):
    """RLE-Zero。

    incl        行长 u16 是否含这 2 字节
    tail_minus1 行尾游程的计数减 1
    clip        允许行尾溢出后截断到 w
    """
    out = bytearray()
    p = 0
    for _ in range(h):
        if p + 2 > len(src):
            return None
        n = u16(src, p)
        p += 2
        end = p + (n - 2 if incl else n)
        if n < 2 or end > len(src):
            return None
        cnt = 0
        row_start = len(out)
        while p < end:
            v = src[p]
            p += 1
            if v == 0:
                if p >= end:
                    return None
                c = src[p]
                p += 1
                if tail_minus1 and p >= end:
                    c -= 1
                    if c < 0:
                        return None
                if clip and cnt + c >= w:
                    c = w - cnt
                    if c < 0:
                        c = 0
                out += bytes(c)
                cnt += c
            else:
                out.append(v)
                cnt += 1
        if cnt != w:
            return None
        del row_start
    return bytes(out) if len(out) == w * h else None


def dec_raw(src, w, h):
    return src[:w * h] if len(src) >= w * h else None


VARIANTS = {
    "raw(未压缩)": lambda s, w, h: dec_raw(s, w, h),
    "RLE 行长含 严格": lambda s, w, h: dec_rle(s, w, h, True, False, False),
    "RLE 行长含 行尾-1": lambda s, w, h: dec_rle(s, w, h, True, True, False),
    "RLE 行长含 裁剪": lambda s, w, h: dec_rle(s, w, h, True, False, True),
    "RLE 行长不含 严格": lambda s, w, h: dec_rle(s, w, h, False, False, False),
    "RLE 行长不含 行尾-1": lambda s, w, h: dec_rle(s, w, h, False, True, False),
}


# ---------------------------------------------------------------- 采样
def collect_frames():
    frames = defaultdict(list)
    for path in MIXES:
        m = mixdump.MixFile(path)
        for owner, hid, off, size, d in mixdump.iter_leaves(m):
            if size < 64:
                continue
            head = owner.read(off, 8)
            if len(head) < 8 or head[0] != 0 or head[1] != 0:
                continue
            w, h, nf = struct.unpack_from("<HHH", head, 2)
            if not (1 <= w <= 2000 and 1 <= h <= 2000 and 1 <= nf <= 64):
                continue
            full = owner.read(off, size)
            if len(full) < 8 + nf * 24:
                continue
            for i in range(nf):
                base = 8 + i * 24
                fx, fy, fw, fh, fl = struct.unpack_from("<HHHHI", full, base)
                d0 = struct.unpack_from("<I", full, base + 20)[0]
                if fw == 0 or fh == 0 or d0 >= len(full):
                    continue
                d1 = struct.unpack_from("<I", full, base + 24 + 20)[0] if i + 1 < nf else len(full)
                if not (d0 < d1 <= len(full)):
                    d1 = len(full)
                frames[(fl & 0xFF) & 0x03].append((full[d0:d1], fw, fh, hid, i))
        m.close()
    return frames


def main() -> None:
    if len(sys.argv) > 1 and sys.argv[1] == "--glyph":
        glyph(sys.argv[2] if len(sys.argv) > 2 else "FULLFNT3.SHP",
              int(sys.argv[3]) if len(sys.argv) > 3 else 0,
              int(sys.argv[4]) if len(sys.argv) > 4 else 8)
        return

    frames = collect_frames()
    total = sum(len(v) for v in frames.values())
    print("收集到 %d 帧，flags&3 分布：%s\n"
          % (total, dict(Counter({hex(k): len(v) for k, v in frames.items()}).most_common())))

    for flags, items in sorted(frames.items(), key=lambda kv: -len(kv[1])):
        print("=== flags&3 = 0x%X  (%d 帧) ===" % (flags, len(items)))
        wins = Counter()
        for name, fn in VARIANTS.items():
            ok = 0
            for src, w, h, hid, i in items:
                try:
                    r = fn(src, w, h)
                except Exception:
                    r = None
                if r is not None and len(r) == w * h:
                    ok += 1
            if ok:
                wins[name] = ok
        if not wins:
            print("   没有候选能解出任何一帧")
        for name, ok in wins.most_common():
            print("   %-20s %5d/%5d  %6.2f%%" % (name, ok, len(items), 100.0 * ok / len(items)))
        print()


# ---------------------------------------------------------------- 字形验证
def glyph(name, start, n):
    sys.path.insert(0, r"E:\ra2source\tools")
    import shpdump3
    full = shpdump3.read_named(name)
    if not full:
        print("找不到", name)
        return
    w, h, nf = struct.unpack_from("<HHH", full, 2)
    print("%s  画布 %dx%d  帧数 %d  显示 %d..%d" % (name, w, h, nf, start, start + n - 1))
    for i, fx, fy, fw, fh, fl, color, d0, d1 in shpdump3.frame_iter(full, nf):
        if i < start or i >= start + n:
            continue
        blob = full[d0:d1]
        px = dec_rle(blob, fw, fh, True, True, False)
        print("\n[帧%3d] %dx%d flags=0x%02X  %s" % (i, fw, fh, fl,
                                                    "RLE-行尾-1 解出" if px else "解不出"))
        if not px:
            continue
        for r in range(fh):
            print("    |" + "".join("#" if px[r * fw + c] else "." for c in range(fw)) + "|")


if __name__ == "__main__":
    main()
