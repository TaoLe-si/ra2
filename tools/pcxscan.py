"""
pcxscan.py -- 扫全部 MIX（含嵌套）找出 PCX / VXL / HVA 条目，打印头部。

MIX 不存文件名，只能靠内容魔数认。这一步是为了确定：
  * RA2 里的 PCX 到底是标准 PCX 还是 Westwood 改过的
  * VXL/HVA 的实际布局（尤其 limb 数量、是不是带 palette 尾巴）

用法：
  python tools/pcxscan.py D:\\westwood\\RA2YR\\ra2.mix pcx
  python tools/pcxscan.py D:\\westwood\\RA2YR\\ra2.mix vxl --dump build/vxl
"""

from __future__ import annotations

import os
import struct
import sys

sys.path.insert(0, r"E:\ra2source\tools")
import mixdump  # noqa: E402


def walk(m, out, kind_filter, depth=0, max_depth=4, path=""):
    for h, off, size in m.entries:
        head = m.head(off, 32)
        sub = mixdump.open_nested(m, off, size) if depth < max_depth else None
        if sub is not None:
            walk(sub, out, kind_filter, depth + 1, max_depth,
                 "%s/%08X" % (path, h))
            continue
        kind = "?"
        if head[:2] == b"\x0a\x05":
            kind = "PCX"
        elif head[:4] == b"Voxel":
            kind = "VXL"
        elif head[:4] == b"HVA!":
            kind = "HVA"
        if kind_filter and kind != kind_filter:
            continue
        if kind == "?" and kind_filter:
            continue
        out.append((kind, h, off, size, path, m))


def dump_pcx_head(m, off, size):
    d = m.read(off, 128)
    (manu, ver, enc, bpp, xmin, ymin, xmax, ymax, hdpi, vdpi) = struct.unpack_from(
        "<BBBBHHHHHH", d, 0)
    nplanes = d[65]
    bpl = struct.unpack_from("<H", d, 66)[0]
    palinfo = struct.unpack_from("<H", d, 68)[0]
    w = xmax - xmin + 1
    h = ymax - ymin + 1
    return dict(manu=manu, ver=ver, enc=enc, bpp=bpp, nplanes=nplanes,
                bpl=bpl, palinfo=palinfo, w=w, h=h, size=size,
                expect=bpl * nplanes * h)


def main() -> None:
    args = sys.argv[1:]
    if len(args) < 2:
        print(__doc__)
        sys.exit(1)
    path, kind = args[0], args[1].upper()
    out_dir = None
    if "--dump" in args:
        out_dir = args[args.index("--dump") + 1]
        os.makedirs(out_dir, exist_ok=True)

    m = mixdump.MixFile(path)
    found = []
    walk(m, found, kind)
    print("[i] %s -> %d 个 %s" % (path, len(found), kind))

    for kind, h, off, size, p, owner in found[:200]:
        if kind == "PCX":
            info = dump_pcx_head(owner, off, size)
            print("  id=0x%08X %-5s %4dx%-4d planes=%d bpl=%-4d enc=%d ver=%d "
                  "size=%-8d 行数据应占=%d %s"
                  % (h, kind, info["w"], info["h"], info["nplanes"],
                     info["bpl"], info["enc"], info["ver"], size,
                     info["expect"], p))
        else:
            d = owner.read(off, 32)
            print("  id=0x%08X %-5s size=%-8d head=%s %s"
                  % (h, kind, size, d[:24].hex(" "), p))
        if out_dir:
            data = owner.read(off, size)
            fn = os.path.join(out_dir, "%s_0x%08X.bin" % (kind.lower(), h))
            open(fn, "wb").write(data)
    if out_dir:
        print("[OK] 已导出到 %s" % out_dir)


if __name__ == "__main__":
    main()
