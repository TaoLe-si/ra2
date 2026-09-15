# -*- coding: utf-8 -*-
"""一次性探针：看 rules/art 里体素相关键到底落在哪个文件、哪个段。"""
import sys

sys.path.insert(0, r"E:\ra2source\tools")
import inidump

FILES = [
    r"E:\ra2source\build\rules.ini",
    r"E:\ra2source\build\art.ini",
    r"E:\ra2source\build\rulesmd.ini",
    r"E:\ra2source\build\artmd.ini",
]

KEYS = ("voxel", "turret", "image", "primaryfireflh", "secondaryfireflh",
        "turretoffset", "barrel", "walkrate", "voxelanim")


def main():
    for p in FILES:
        raw = open(p, "rb").read()
        secs, mal = inidump.parse(raw.decode("latin-1"))
        idx = {}
        for name, ents in secs:
            idx.setdefault(name.lower(), []).append((name, ents))
        print("=== %s  %d 段  畸形 %d" % (p.split("\\")[-1], len(secs), mal))
        for k in KEYS:
            hits = []
            for name, ents in secs:
                for kk, vv in ents:
                    if kk.lower() == k:
                        hits.append((name, vv))
            if hits:
                vals = {}
                for _n, vv in hits:
                    vals[vv] = vals.get(vv, 0) + 1
                top = sorted(vals.items(), key=lambda kv: -kv[1])[:6]
                print("  %-18s %5d 处  样例: %s" % (k, len(hits), top))
            else:
                print("  %-18s 0 处" % k)
        # 类型列表段
        for lst in ("VehicleTypes", "AircraftTypes", "InfantryTypes", "BuildingTypes"):
            if lst.lower() in idx:
                n = len(idx[lst.lower()][-1][1])
                print("  [%-14s] %d 条" % (lst, n))


if __name__ == "__main__":
    main()
