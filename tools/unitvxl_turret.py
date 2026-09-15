# -*- coding: utf-8 -*-
"""探针：Turret=yes 但没有独立 <名>TUR.VXL 的单位，炮塔到底藏在哪？

假设：炮塔是车体 VXL 里的一个**肢体**（HVA 逐帧转它），此时 HVA 肢体数 > 1
且肢体名里带 TUR。验证这个假设，才能把 P2 的"炮塔来源"规则写对。
"""
import json
import os
import sys

sys.path.insert(0, r"E:\ra2source\tools")
import mixdump
import vxlstruct

MIXES = [r"D:\westwood\RA2YR\ra2.mix", r"D:\westwood\RA2YR\ra2md.mix"]


def load_ids(keep):
    ids = {}
    for p in MIXES:
        if not os.path.exists(p):
            continue
        m = mixdump.MixFile(p)
        keep.append(m)              # 必须活着，owner 是它的子归档
        for owner, h, off, size, depth in mixdump.iter_leaves(m):
            ids.setdefault(h, (m, owner, off, size))
    return ids


def main():
    rows = json.load(open(r"E:\ra2source\db\unit-vxl.json", encoding="utf-8"))
    keep = []
    ids = load_ids(keep)
    print("%-12s %-10s %-6s %-6s %s" % ("单位", "Image", "TUR", "BARL", "车体肢体"))
    for r in rows:
        if not r["voxel"] or not r["turret"]:
            continue
        t, b = r["turret_part"], r["barrel_part"]
        if t and t["found"]:
            continue                      # 有独立炮塔文件，不用查
        id_body = int(r["body"]["id"], 16)
        hit = ids.get(id_body)
        if not hit:
            print("%-12s %-10s ?" % (r["unit"], r["image"]))
            continue
        _m, owner, off, size = hit
        v = vxlstruct.VxlFile(owner.read(off, size))
        names = [h.name for h in v.headers]
        print("%-12s %-10s %-6s %-6s n=%d %s" % (
            r["unit"], r["image"],
            "缺" if not (t and t["found"]) else "OK",
            "缺" if not (b and b["found"]) else "OK",
            v.num_limbs, names))


if __name__ == "__main__":
    main()
