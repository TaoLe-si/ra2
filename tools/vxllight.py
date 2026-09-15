"""
vxllight.py -- 给 VXL 加上 Lambert 光照，验证从 gamemd.exe 抠出来的法线表对不对。

背景：VXL 每个体素带一个 normal 索引（0..255），法线表不在文件里、在 gamemd.exe
的 .data 里（实测 off=0x00446A08，361 项单位向量，见 tools/normalscan2.py）。
而 VXL 头里的 NormalsType（实测只有 2 和 4）决定用这张表的前多少项：
    type=2 -> normal 取值 0..35   （JEEP 实测）
    type=4 -> normal 取值 0..244  （GTNK / ZEP 实测）

要不要转世界空间：法线存在**模型空间**里，严格说要乘肢体的旋转矩阵。
但单肢模型的肢体矩阵是恒等（实测 GTNK 的 transform 就是单位阵 + 平移），
所以先用原值；多肢模型（13 肢机甲）再考虑逐肢变换。

用法：
  python tools/vxllight.py <mix> <0xID> out.png [放大] [--light x,y,z] [--amb 0.35]
  python tools/vxllight.py D:/westwood/RA2YR/ra2.mix 0xAE458B95 build/lit.png 10
"""

from __future__ import annotations

import math
import os
import sys

sys.path.insert(0, r"E:\ra2source\tools")
from mixdump import MixFile, iter_leaves          # noqa: E402
from vxlstruct import VxlFile                     # noqa: E402
from pcxdec import write_png                      # noqa: E402

NORMALS_TXT = r"E:\ra2source\db\voxel-normals.txt"
MIXES = [r"D:\westwood\RA2YR\ra2.mix", r"D:\westwood\RA2YR\ra2md.mix"]


def load_normals():
    out = []
    for line in open(NORMALS_TXT, encoding="utf-8"):
        if line.startswith("#"):
            continue
        p = line.split()
        out.append((float(p[1]), float(p[2]), float(p[3])))
    return out


def palette_rgb(v):
    p = v.palette
    return [(p[i * 3], p[i * 3 + 1], p[i * 3 + 2]) for i in range(256)]


def find(keep, hid):
    for p in MIXES:
        m = MixFile(p)
        keep.append(m)
        for owner, h, off, size, depth in iter_leaves(m):
            if h == hid:
                return owner.read(off, size)
    return None


def norm(v):
    L = math.sqrt(sum(c * c for c in v))
    return tuple(c / L for c in v)


def render(v, normals, scale=10.0, light=(0.0, 0.0, 0.0), ambient=0.35):
    pal = palette_rgb(v)
    L = norm(light)
    pts = []
    for li in range(v.num_limbs):
        t = v.tailers[li]
        m = t.transform
        ox, oy, oz = t.min_bounds
        for x, y, z, c, n in v.voxels(li):
            lx, ly, lz = x + ox, y + oy, z + oz
            px = m[0] * lx + m[1] * ly + m[2] * lz + m[3]
            py = m[4] * lx + m[5] * ly + m[6] * lz + m[7]
            pz = m[8] * lx + m[9] * ly + m[10] * lz + m[11]
            # 法线：模型空间的，先不做肢体旋转（见文件头说明）
            ni = n if n < len(normals) else 0
            nx, ny, nz = normals[ni]
            sh = ambient + (1.0 - ambient) * max(0.0, nx * L[0] + ny * L[1] + nz * L[2])
            pts.append((px, py, pz, c, sh))

    k = 1.0 / math.sqrt(2.0)
    proj = []
    for px, py, pz, c, sh in pts:
        proj.append(((px - py) * k, (px + py) * k * 0.5 - pz, px + py + pz, c, sh))
    xs = [p[0] for p in proj]
    ys = [p[1] for p in proj]
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
    w = int((x1 - x0) * scale) + 2
    h = int((y1 - y0) * scale) + 2
    buf = bytearray(b"\x00" * (w * h * 4))   # write_png 要 RGBA
    proj.sort(key=lambda p: p[2])            # 画家算法：由远及近
    step = max(1, int(scale))
    for sx, sy, _d, c, sh in proj:
        cx = int((sx - x0) * scale)
        cy = int((sy - y0) * scale)
        r, g, b = pal[c]
        # 一个体素要画成 scale×scale 的方块 —— 只画 1 个像素的话，
        # scale=12 时 99% 的画布是空的，整张图会几乎全黑（踩过）。
        rr = min(255, int(r * sh))
        gg = min(255, int(g * sh))
        bb = min(255, int(b * sh))
        for dy in range(step):
            py2 = cy + dy
            if not (0 <= py2 < h):
                continue
            for dx in range(step):
                px2 = cx + dx
                if not (0 <= px2 < w):
                    continue
                o = (py2 * w + px2) * 4
                buf[o] = rr
                buf[o + 1] = gg
                buf[o + 2] = bb
                buf[o + 3] = 255
    return w, h, bytes(buf)


def main() -> None:
    args = sys.argv[1:]
    light = None
    amb = 0.35
    if "--light" in args:
        i = args.index("--light")
        light = tuple(float(x) for x in args[i + 1].split(","))
        args = args[:i] + args[i + 2:]
    if "--amb" in args:
        i = args.index("--amb")
        amb = float(args[i + 1])
        args = args[:i] + args[i + 2:]
    if len(args) < 3:
        print(__doc__)
        return
    mix, hid, out = args[0], int(args[1], 16), args[2]
    scale = float(args[3]) if len(args) > 3 else 10.0
    # 缺省光：等距视角下从"左上前方"打过来。世界空间 X 向前、Y 横向、Z 向上，
    # 屏幕左上 = -X+Y 方向，抬高取 +Z。
    if light is None:
        light = (-0.55, -0.45, 0.70)

    normals = load_normals()
    keep = []
    d = find(keep, hid)
    v = VxlFile(d)
    print("VXL limbs=%d  法线表 %d 项  光=(%.2f,%.2f,%.2f)  ambient=%.2f"
          % (v.num_limbs, len(normals), *light, amb))
    nt = set(t.normals_type for t in v.tailers)
    print("   normals_type=%s" % sorted(nt))

    w, h, rgb = render(v, normals, scale, light, amb)
    write_png(out, w, h, rgb)
    print("[OK] %dx%d -> %s" % (w, h, out))


if __name__ == "__main__":
    main()
