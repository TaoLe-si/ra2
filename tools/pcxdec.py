"""
pcxdec.py -- RA2 PCX 的 Python 参考解码器 + 全量自校验。

格式就是标准 ZSoft PCX（RA2 没有改过，实测 161 个样本全部自洽）：
  128 字节头
  | 0  制造商 0x0A
  | 1  版本 5
  | 2  编码 1 = RLE
  | 3  每平面位数（RA2 里是 8）
  | 4  u16 Xmin / 6 u16 Ymin / 8 u16 Xmax / 10 u16 Ymax
  | 65 平面数（1 = 8 位带调色板，3 = 24 位 RGB）
  | 66 u16 每行字节数（= 宽度，按偶对齐）
  然后是 RLE 数据，逐行、逐平面（planes=3 时是 R,G,B 三条带依次存）
  最后是调色板：只有 planes==1 && bpp==8 时有，= 1 字节 0x0C + 768 字节

RLE：读一字节 b。若 (b & 0xC0) == 0xC0 则是游程，长度 = b & 0x3F，
下一个字节是要重复的值；否则 b 本身就是像素值。每行独立计数。

自校验（三条硬判据，任何一条不过就是解码错了）：
  1. RLE 解出来的字节数 == 每行字节数 × 行数 × 平面数（不多不少）
  2. 8 位模式下文件尾必须有 0x0C 魔数 + 768 字节调色板
  3. 128 + 数据长度 + 调色板长度 == 文件长度

用法：
  python tools/pcxdec.py            # 跑全量 161 个
  python tools/pcxdec.py --dump 3   # 顺带把前 3 个写成 BMP
"""

from __future__ import annotations

import os
import struct
import sys

sys.path.insert(0, r"E:\ra2source\tools")
import mixdump  # noqa: E402

HDR = 128
PAL_MAGIC = 0x0C


class PcxError(Exception):
    pass


class Pcx:
    def __init__(self, data: bytes):
        self.raw = data
        d = data
        if len(d) < HDR:
            raise PcxError("短于 128 字节头")
        (self.manu, self.ver, self.enc, self.bpp,
         self.xmin, self.ymin, self.xmax, self.ymax) = struct.unpack_from("<BBBBHHHH", d, 0)
        self.nplanes = d[65]
        self.bpl = struct.unpack_from("<H", d, 66)[0]
        self.palinfo = struct.unpack_from("<H", d, 68)[0]
        self.w = self.xmax - self.xmin + 1
        self.h = self.ymax - self.ymin + 1
        if self.manu != 0x0A:
            raise PcxError("制造商字节 0x%02X 不是 0x0A" % self.manu)
        if self.enc != 1:
            raise PcxError("不支持的编码 %d（只做 RLE）" % self.enc)
        if self.bpp != 8 or self.nplanes not in (1, 3):
            raise PcxError("不支持 %d 位 × %d 平面" % (self.bpp, self.nplanes))
        if self.w <= 0 or self.h <= 0 or self.bpl < self.w or self.bpl > 4096:
            raise PcxError("尺寸离谱 %dx%d bpl=%d" % (self.w, self.h, self.bpl))
        self.has_pal = (self.nplanes == 1)
        self._decode()

    # ---------------------------------------------------------------- RLE
    def _decode(self) -> None:
        row = self.bpl
        want = row * self.h * self.nplanes
        src = self.raw
        n = len(src)
        out = bytearray()
        i = HDR
        # 8 位模式：调色板占掉最后 769 字节，数据区到那里为止
        end = n
        if self.has_pal:
            if n < HDR + 769:
                raise PcxError("8 位模式装不下调色板")
            if src[n - 769] != PAL_MAGIC:
                raise PcxError("调色板魔数是 0x%02X，不是 0x0C" % src[n - 769])
            self.palette = src[n - 768:]
            end = n - 769
        else:
            self.palette = b""
        while i < end and len(out) < want:
            b = src[i]
            i += 1
            if (b & 0xC0) == 0xC0:
                if i >= end:
                    raise PcxError("游程头在结尾断掉")
                out += bytes([src[i]]) * (b & 0x3F)
                i += 1
            else:
                out.append(b)
        if len(out) != want:
            raise PcxError("解出 %d 字节，应为 %d" % (len(out), want))
        if i != end:
            raise PcxError("数据区还剩 %d 字节没吃掉（end=%d i=%d）" % (end - i, end, i))
        self.pixels = bytes(out)

    # ---------------------------------------------------------------- 取像素
    def to_indexed(self) -> bytes:
        """8 位模式：返回 w*h 的调色板索引。"""
        if not self.has_pal:
            raise PcxError("24 位没有索引")
        out = bytearray(self.w * self.h)
        for y in range(self.h):
            src = self.pixels[y * self.bpl:y * self.bpl + self.w]
            out[y * self.w:(y + 1) * self.w] = src
        return bytes(out)

    def to_rgb(self) -> bytes:
        """24 位模式：返回 w*h*3 的 RGB。"""
        if self.has_pal:
            raise PcxError("8 位模式请走调色板")
        out = bytearray(self.w * self.h * 3)
        for y in range(self.h):
            base = y * self.bpl * 3
            for x in range(self.w):
                r = self.pixels[base + x]
                g = self.pixels[base + self.bpl + x]
                b = self.pixels[base + 2 * self.bpl + x]
                o = (y * self.w + x) * 3
                out[o] = r
                out[o + 1] = g
                out[o + 2] = b
        return bytes(out)

    def to_rgba(self) -> bytes:
        """统一成 RGBA，8 位走调色板，24 位直接取。"""
        if self.has_pal:
            idx = self.to_indexed()
            out = bytearray(self.w * self.h * 4)
            for i, v in enumerate(idx):
                out[i * 4 + 0] = self.palette[v * 3 + 0]
                out[i * 4 + 1] = self.palette[v * 3 + 1]
                out[i * 4 + 2] = self.palette[v * 3 + 2]
                out[i * 4 + 3] = 255
            return bytes(out)
        rgb = self.to_rgb()
        out = bytearray(self.w * self.h * 4)
        for i in range(self.w * self.h):
            out[i * 4 + 0] = rgb[i * 3 + 0]
            out[i * 4 + 1] = rgb[i * 3 + 1]
            out[i * 4 + 2] = rgb[i * 3 + 2]
            out[i * 4 + 3] = 255
        return bytes(out)


def write_png(path, w, h, rgba):
    """最小 PNG 写出（只靠 zlib，不引第三方库）。"""
    import zlib
    row = bytearray()
    for y in range(h):
        row.append(0)                       # filter type 0 = None
        for x in range(w):
            s = (y * w + x) * 4
            row += bytes((rgba[s], rgba[s + 1], rgba[s + 2]))

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)   # 8位 RGB
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
           + chunk(b"IDAT", zlib.compress(bytes(row), 6)) + chunk(b"IEND", b""))
    open(path, "wb").write(png)


def write_bmp(path, w, h, rgba):
    """24 位 BMP，自上而下存（height 取负）。"""
    row = ((w * 3 + 3) // 4) * 4
    body = bytearray(row * h)
    for y in range(h):
        for x in range(w):
            s = (y * w + x) * 4
            d = y * row + x * 3
            body[d + 0] = rgba[s + 2]   # B
            body[d + 1] = rgba[s + 1]   # G
            body[d + 2] = rgba[s + 0]   # R
    hdr = struct.pack("<2sIHHI", b"BM", 14 + 40 + len(body), 0, 0, 14 + 40)
    dib = struct.pack("<IiiHHIIiiII", 40, w, -h, 1, 24, 0, len(body), 0, 0, 0, 0)
    open(path, "wb").write(hdr + dib + bytes(body))


# ---------------------------------------------------------------- 全量扫描

def scan(path, want_dump=0, out_dir="build/pcx"):
    m = mixdump.MixFile(path)
    leaves = []
    for owner, h, off, size, depth in mixdump.iter_leaves(m):
        leaves.append((owner, h, off, size))
    ok = bad = skipped = 0
    dumped = 0
    os.makedirs(out_dir, exist_ok=True)
    errs = {}
    for owner, h, off, size in leaves:
        d = owner.head(off, 2)
        if d[:2] != b"\x0a\x05":
            continue
        data = owner.read(off, size)
        try:
            p = Pcx(data)
        except PcxError as e:
            bad += 1
            errs.setdefault(str(e), []).append("0x%08X" % h)
            continue
        ok += 1
        if dumped < want_dump and p.w * p.h <= 4000:
            write_bmp(os.path.join(out_dir, "0x%08X_%dx%d.bmp" % (h, p.w, p.h)),
                      p.w, p.h, p.to_rgba())
            dumped += 1
    print("[i] %s  叶子 %d 个，其中 PCX 魔数 %d 个" % (path, len(leaves), ok + bad))
    print("[%s] 解码成功 %d，失败 %d" % ("OK" if bad == 0 else "x", ok, bad))
    for e, ids in sorted(errs.items(), key=lambda kv: -len(kv[1])):
        print("     %-40s x%d  例: %s" % (e, len(ids), " ".join(ids[:5])))
    return ok, bad


def dump_ids(path, ids, out_dir="build/pcx"):
    """按 id 导出指定 PCX 的 BMP（用来肉眼验证平面顺序 / 调色板）。"""
    m = mixdump.MixFile(path)
    os.makedirs(out_dir, exist_ok=True)
    want = set(int(x, 16) for x in ids)
    for owner, h, off, size, depth in mixdump.iter_leaves(m):
        if h not in want:
            continue
        data = owner.read(off, size)
        try:
            p = Pcx(data)
        except PcxError as e:
            print("[x] 0x%08X %s" % (h, e))
            continue
        fn = os.path.join(out_dir, "0x%08X_%dx%d_p%d.png" % (h, p.w, p.h, p.nplanes))
        write_png(fn, p.w, p.h, p.to_rgba())
        print("[OK] 0x%08X %dx%d planes=%d bpl=%d -> %s" % (h, p.w, p.h, p.nplanes, p.bpl, fn))


def main() -> None:
    args = sys.argv[1:]
    want = 0
    if "--dump" in args:
        want = int(args[args.index("--dump") + 1])
    if "--id" in args:
        k = args.index("--id")
        dump_ids(args[0], [a for a in args[k + 1:] if a.startswith("0x")])
        return
    paths = [a for a in args if not a.startswith("-") and not a.isdigit()]
    if not paths:
        paths = [r"D:\westwood\RA2YR\ra2.mix"]
    tot_ok = tot_bad = 0
    for p in paths:
        o, b = scan(p, want)
        tot_ok += o
        tot_bad += b
    print("[=] 合计 成功 %d / 失败 %d" % (tot_ok, tot_bad))
    sys.exit(1 if tot_bad else 0)


if __name__ == "__main__":
    main()
