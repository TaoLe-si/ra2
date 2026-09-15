# -*- coding: utf-8 -*-
"""一次性探针：打印某个单位在 rules/art 里的相关段。"""
import io
import os
import sys

sys.path.insert(0, r"E:\ra2source\tools")
import inidump

KEYS = ("image", "voxel", "turret", "turretoffset", "primaryfireflh",
        "voxelturret", "strength", "techlevel")


def main():
    unit = sys.argv[1].upper() if len(sys.argv) > 1 else "GTGCAN"
    for p in (r"E:\ra2source\build\rules.ini", r"E:\ra2source\build\rulesmd.ini",
              r"E:\ra2source\build\art.ini", r"E:\ra2source\build\artmd.ini"):
        raw = open(p, "rb").read()
        secs, _mal = inidump.parse(raw.decode("latin-1"))
        for name, ents in secs:
            if name.upper() != unit:
                continue
            d = {}
            for k, v in ents:
                d.setdefault(k.lower(), v)
            hit = {k: d[k] for k in KEYS if k in d}
            print("%-14s [%-8s] %s" % (os.path.basename(p), name, hit))


if __name__ == "__main__":
    main()
