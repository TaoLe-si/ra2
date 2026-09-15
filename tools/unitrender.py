# -*- coding: utf-8 -*-
"""端到端：给单位名 -> 从 INI 推出模型 -> 渲染成 PNG。

链路（每一跳都真实执行，不做假）：
  MIX 解密 -> RULES/ART 解析 -> 单位名解析出 <名>.VXL + TUR + BARL
  -> VXL 解码 -> HVA 姿态 -> 等距光栅化 -> R8 索引图 -> 调色板查表
  -> DX12 离屏回读 -> PNG

用法：
  python tools/unitrender.py MTNK HTNK ZEP
  python tools/unitrender.py YTNK --yaw 40 --pitch 25
"""
from __future__ import annotations

import os
import subprocess
import sys

ROOT = r"E:\ra2source"
VIEW = os.path.join(ROOT, "build", "ra2view.exe")
RAW = os.path.join(ROOT, "build", "frame.raw")
MIX1 = r"D:\westwood\RA2YR\ra2.mix"
MIX2 = r"D:\westwood\RA2YR\ra2md.mix"


def render(unit, yaw=None, pitch=None, out=None):
    out = out or os.path.join(ROOT, "build", "u_%s.png" % unit.lower())
    cmd = [VIEW, MIX1, "--addmix", MIX2, "--unit", unit, "--offscreen"]
    if yaw is not None:
        cmd += ["--turretyaw", str(yaw)]
    if pitch is not None:
        cmd += ["--barrelpitch", str(pitch)]
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True,
                       encoding="utf-8", errors="replace")
    sys.stdout.write(r.stdout)
    if r.returncode != 0:
        sys.stderr.write(r.stderr)
        return None
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "raw2png.py"),
                    RAW, out], cwd=ROOT, check=True)
    print("--> %s\n" % out)
    return out


def main() -> None:
    args = sys.argv[1:]
    yaw = pitch = None
    units = []
    i = 0
    while i < len(args):
        if args[i] == "--yaw":
            yaw = args[i + 1]
            i += 2
        elif args[i] == "--pitch":
            pitch = args[i + 1]
            i += 2
        else:
            units.append(args[i])
            i += 1
    if not units:
        units = ["MTNK", "HTNK", "YTNK", "ZEP", "APOC", "LTNK"]
    for u in units:
        print("=" * 60)
        print("单位 %s" % u)
        render(u, yaw, pitch)


if __name__ == "__main__":
    main()
