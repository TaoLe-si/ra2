"""
shpdump3.py -- 按名字从 MIX 里取条目 + 打 SHP 帧头 / 帧数据原始字节 / ASCII 预览。

定位：**诊断工具**。正经的解码和渲染请用 shpdump.ShpFile / shppng.py。
这里保留 dec_rle（不做行尾裁剪的"裸解码"）是为了对比"裁剪前"是什么样，
破解 flags=0x03 时正是靠它看出"每行多 1 个像素"的。

名字表来自 db/mix-names.txt（由 mixnames.py 生成）。

用法:
  python shpdump3.py FULLFNT3.SHP [起始帧] [帧数]
"""

from __future__ import annotations

import struct
import sys

sys.path.insert(0, r"E:\ra2source\tools")
import mixdump  # noqa: E402

MIXES = [r"D:\westwood\RA2YR\ra2.mix", r"D:\westwood\RA2YR\ra2md.mix"]


def u16(b, p):
    return b[p] | (b[p + 1] << 8)


def load_name_table():
    """db/mix-names.txt -> {id: name}"""
    tab = {}
    for line in open(r"E:\ra2source\db\mix-names.txt", encoding="utf-8", errors="replace"):
        parts = line.split()
        if len(parts) < 4 or parts[-1] == "?":
            continue
        try:
            tab[int(parts[1], 16)] = parts[2]
        except ValueError:
            pass
    return tab


_CACHE = {}


def _resolve(want):
    """在 MIX（含嵌套）里找到 ID==want 的 (bytes, 来源路径)。"""
    for path in MIXES:
        m = mixdump.MixFile(path)
        try:
            for owner, hid, off, size, d in mixdump.iter_leaves(m):
                if hid == want:
                    return owner.read(off, size), path
        finally:
            m.close()
    return None, None


def read_named(name):
    """按名字取条目原始字节；找不到返回 None。

    注意别把 MixFile 对象缓存出去：那会一直占着整份归档的内存和文件句柄，
    而且 iter_leaves 的 owner 只在归档还活着时有效。这里一次性读成 bytes。
    """
    if name in _CACHE:
        return _CACHE[name]
    tab = load_name_table()
    want = None
    for k, v in tab.items():
        if v.upper() == name.upper():
            want = k
            break
    data = None
    if want is not None:
        data, _src = _resolve(want)
    _CACHE[name] = data
    return data


def find_id(name):
    """按名字反查 MIX 里的 32 位 ID；查不到返回 None。"""
    tab = load_name_table()
    for k, v in tab.items():
        if v.upper() == name.upper():
            return k
    return None


# ------------------------------------------------------------------ 解码器
def dec_rle(src, w, h, cnt_off=0, strict=False):
    """**不裁剪**的裸 RLE-Zero，只用于诊断。

    真正的解码规则必须逐行裁到行宽（行尾游程计数常多 1），见 shpdump.ShpFile。
    这里故意不裁，就是为了把"多出来的那 1 个像素"暴露出来。
    """
    out = bytearray()
    p = 0
    for _ in range(h):
        if p + 2 > len(src):
            return None
        n = u16(src, p)
        p += 2
        end = p + n - 2
        if n < 2 or end > len(src):
            return None
        cnt = 0
        while p < end:
            v = src[p]
            p += 1
            if v == 0:
                if p >= end:
                    return None
                c = src[p] + cnt_off
                p += 1
                out += bytes(c)
                cnt += c
            else:
                out.append(v)
                cnt += 1
        if strict and cnt != w:
            return None
    return bytes(out) if len(out) == w * h else None


def frame_iter(full, nf):
    for i in range(nf):
        base = 8 + i * 24
        fx, fy, fw, fh, fl = struct.unpack_from("<HHHHI", full, base)
        color = full[base + 12:base + 16]
        d0 = struct.unpack_from("<I", full, base + 20)[0]
        d1 = struct.unpack_from("<I", full, base + 24 + 20)[0] if i + 1 < nf else len(full)
        if not (d0 < d1 <= len(full)):
            d1 = len(full)
        yield i, fx, fy, fw, fh, fl, color, d0, d1


def main() -> None:
    name = sys.argv[1] if len(sys.argv) > 1 else "FULLFNT3.SHP"
    start = int(sys.argv[2]) if len(sys.argv) > 2 else 0
    n = int(sys.argv[3]) if len(sys.argv) > 3 else 6

    full = read_named(name)
    if not full:
        print("找不到", name)
        return
    size = len(full)
    w, h, nf = struct.unpack_from("<HHH", full, 2)
    print("%s size=%d 画布 %dx%d 帧数=%d" % (name, size, w, h, nf))

    for i, fx, fy, fw, fh, fl, color, d0, d1 in frame_iter(full, nf):
        if i < start:
            continue
        if i >= start + n:
            break
        blob = full[d0:d1]
        print("\n[帧%3d] %dx%d @(%d,%d) flags=0x%02X color=%s data[%d,%d) len=%d"
              % (i, fw, fh, fx, fy, fl, color.hex(), d0, d1, len(blob)))
        print("  hex :", blob[:48].hex(" "))
        # 行长序列
        p = 0
        lens = []
        for _ in range(fh):
            if p + 2 > len(blob):
                lens.append(-1)
                break
            ln = u16(blob, p)
            lens.append(ln)
            p += ln
        print("  行长:", lens, "sum=%d len=%d" % (sum(x for x in lens if x > 0), len(blob)))

        ok = dec_rle(blob, fw, fh)
        if ok is None:
            print("  RLE-Zero: 失败")
        else:
            print("  RLE-Zero: 成功 %d px" % len(ok))
        draw(blob, fw, fh)


def draw(blob, fw, fh, cnt_off=0):
    """逐行解（不截断），画 #/. 方便肉眼验证字形。"""
    p = 0
    for r in range(fh):
        if p + 2 > len(blob):
            print("    <行 %d 越界>" % r)
            return
        ln = u16(blob, p)
        p += 2
        end = min(p + max(0, ln - 2), len(blob))
        row = bytearray()
        while p < end:
            v = blob[p]
            p += 1
            if v == 0:
                if p >= end:
                    break
                c = blob[p] + cnt_off
                p += 1
                row += bytes(c)
            else:
                row.append(v)
        print("    |%-*s| %d" % (fw, "".join("#" if x else "." for x in row[:fw]), len(row)))


if __name__ == "__main__":
    main()
