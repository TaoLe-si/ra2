"""vxlrender.py -- 把 VXL 体素模型渲成 PNG，肉眼验收解码结果。

用法: python tools/vxlrender.py <mix> <0xID> out.png [放大倍数]
"""
from __future__ import annotations

import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mixdump import MixFile, iter_leaves          # noqa: E402
from vxlstruct import VxlFile                     # noqa: E402
from pcxdec import write_png                      # noqa: E402


def palette_rgb(v: VxlFile):
    """取 256 色 RGB。

    实测 184 个样本的 768 个分量**全部**满足 raw ≡ 3 (mod 4)，即文件里存的
    已经是 `(v6<<2)|(v6>>4)` 展开后的 8 位值 —— 直接取用，不要再 <<2。
    """
    p = v.palette
    return [(p[i * 3], p[i * 3 + 1], p[i * 3 + 2]) for i in range(256)]


def render(v: VxlFile, scale: float = 6.0, shading: bool = True):
    pal = palette_rgb(v)
    pts = []
    for li in range(v.num_limbs):
        t = v.tailers[li]
        m = t.transform
        # 体素索引不是局部坐标：局部坐标 = 索引 + min_bounds
        # （min/max_bounds 是该肢体体素在局部系下的 AABB，局部原点在 AABB 中心）。
        ox, oy, oz = t.min_bounds
        for x, y, z, c, n in v.voxels(li):
            lx, ly, lz = x + ox, y + oy, z + oz
            px = m[0] * lx + m[1] * ly + m[2] * lz + m[3]
            py = m[4] * lx + m[5] * ly + m[6] * lz + m[7]
            pz = m[8] * lx + m[9] * ly + m[10] * lz + m[11]
            pts.append((px, py, pz, c, n))

    # 等距投影：绕 Z 转 45°，再压扁
    k = 1.0 / math.sqrt(2.0)
    proj = []
    for px, py, pz, c, n in pts:
        sx = (px - py) * k
        sy = (px + py) * k * 0.5 - pz
        depth = px + py + pz
        proj.append((sx, sy, depth, c, n))
    if not proj:
        raise SystemExit("没有体素")

    xs = [p[0] for p in proj]
    ys = [p[1] for p in proj]
    x0, x1 = min(xs), max(xs)
    y0, y1 = min(ys), max(ys)
    w = int((x1 - x0) * scale) + 2
    h = int((y1 - y0) * scale) + 2
    buf = bytearray(w * h * 4)

    # 画家算法：depth 大的先画
    proj.sort(key=lambda p: p[2])
    for sx, sy, _d, c, n in proj:
        cx = int((sx - x0) * scale)
        cy = int((sy - y0) * scale)
        r, g, b = pal[c]
        if shading:
            # 法线索引里 32 附近是"朝内/默认"，这里简单按法线调亮度
            f = 0.65 + 0.35 * (n / 255.0)
            r, g, b = min(255, int(r * f)), min(255, int(g * f)), min(255, int(b * f))
        for dy in range(int(scale)):
            for dx in range(int(scale)):
                px2, py2 = cx + dx, cy + dy
                if 0 <= px2 < w and 0 <= py2 < h:
                    s = (py2 * w + px2) * 4
                    buf[s] = r
                    buf[s + 1] = g
                    buf[s + 2] = b
                    buf[s + 3] = 255
    return w, h, bytes(buf)


def main() -> None:
    if len(sys.argv) < 4:
        print(__doc__)
        sys.exit(1)
    path, want, out = sys.argv[1], int(sys.argv[2], 16), sys.argv[3]
    scale = float(sys.argv[4]) if len(sys.argv) > 4 else 6.0
    m = MixFile(path)
    data = None
    for owner, h, off, size, depth in iter_leaves(m):
        if h == want and size >= 1024 and owner.read(off, 15) == b"Voxel Animation":
            data = owner.read(off, size)
            break
    if data is None:
        print("[x] 没有 id=0x%08X" % want)
        sys.exit(1)
    v = VxlFile(data)
    print(v.summary())
    for li in range(v.num_limbs):
        t = v.tailers[li]
        print("  limb %d %-12s %dx%dx%d nt=%d" % (li, v.headers[li].name, t.x, t.y, t.z,
                                                  t.normals_type))
    w, h, rgba = render(v, scale)
    write_png(out, w, h, rgba)
    print("[OK] %dx%d -> %s" % (w, h, out))


if __name__ == "__main__":
    main()
