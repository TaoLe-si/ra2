"""
unitvxl.py -- 从 INI 自动推出单位的体素模型组成（P2 数据层的参考实现）。

为什么需要：P1 之前想看一辆坦克得手写三个 0xID（车体/炮塔/炮管），
而游戏自己从来不这么干 —— 它读 rules(md).ini 拿 Image=，读 art(md).ini 拿
Voxel=yes，然后按名字拼出 <名>.VXL / <名>TUR.VXL / <名>BARL.VXL 去 MIX 里查。
这条链路打通，"给单位名出图"才算真的连上。

INI 里到底怎么摆（实测，见 tools/iniprobe.py）：
  Voxel=yes        -> art(md).ini 的 [Image] 段        （rules 里一处都没有）
  Turret=yes       -> rules(md).ini 的 [单位] 段        （art 里一处都没有）
  Image=           -> rules(md).ini 的 [单位] 段        （缺省 = 单位名本身）
  PrimaryFireFLH=  -> art(md).ini 的 [Image] 段
  TurretOffset=    -> art(md).ini 的 [Image] 段

载入顺序（跟游戏一致）：rules.ini 打底，rulesmd.ini 覆盖；art 同理。

用法：
  python tools/unitvxl.py                      # 全量导出 db/unit-vxl.json + 报告
  python tools/unitvxl.py MTNK HTNK ZEP        # 只看这几个单位
"""

from __future__ import annotations

import json
import os
import sys

sys.path.insert(0, r"E:\ra2source\tools")
import inidump
import mixdump

BUILD = r"E:\ra2source\build"
MIXES = [r"D:\westwood\RA2YR\ra2.mix", r"D:\westwood\RA2YR\ra2md.mix"]

# 会当成"单位"去解析的类型列表段（顺序即优先级，先命中先用）
TYPE_LISTS = ("VehicleTypes", "AircraftTypes", "InfantryTypes", "BuildingTypes")


def load_ini(path):
    if not os.path.exists(path):
        return {}, 0
    raw = open(path, "rb").read()
    secs, mal = inidump.parse(raw.decode("latin-1"))
    idx = {}
    for name, ents in secs:
        d = dict((k.lower(), v) for k, v in ents)   # 后写覆盖先写
        prev = idx.get(name.lower())
        if prev:
            prev.update(d)
        else:
            idx[name.lower()] = d
    return idx, mal


def merge(base_path, md_path):
    """md 覆盖 base。段级合并：同名段的键逐条覆盖，不整段替换。"""
    idx = {}
    for p in (base_path, md_path):
        cur, _mal = load_ini(p)
        for k, v in cur.items():
            if k in idx:
                idx[k].update(v)
            else:
                idx[k] = dict(v)
    return idx


class Db:
    def __init__(self):
        self.rules = merge(os.path.join(BUILD, "rules.ini"),
                           os.path.join(BUILD, "rulesmd.ini"))
        self.art = merge(os.path.join(BUILD, "art.ini"),
                         os.path.join(BUILD, "artmd.ini"))
        self.ids = self._load_ids()
        self.units = self._collect_units()

    def _load_ids(self):
        ids = {}
        for p in MIXES:
            if not os.path.exists(p):
                continue
            m = mixdump.MixFile(p)
            for owner, h, off, size, depth in mixdump.iter_leaves(m):
                ids.setdefault(h, (os.path.basename(p), size))
            m.close()
        return ids

    def _collect_units(self):
        out = []
        seen = set()
        for lst in TYPE_LISTS:
            ents = self.rules.get(lst.lower(), {})
            for k in sorted(ents, key=lambda s: (not s.isdigit(), int(s) if s.isdigit() else s)):
                name = ents[k].strip()
                if not name or name.lower() in seen:
                    continue
                seen.add(name.lower())
                out.append((name, lst))
        return out

    def resolve(self, unit):
        """单位名 -> 模型组成。返回 dict。"""
        r = self.rules.get(unit.lower(), {})
        image = r.get("image", "").strip() or unit
        art = self.art.get(image.lower(), {})
        voxel = art.get("voxel", "").strip().lower() == "yes"
        turret = r.get("turret", "").strip().lower() == "yes"

        def crc(n):
            return mixdump.westwood_crc(n)

        def have(n):
            c = crc(n)
            return (c in self.ids), c

        body_name = "%s.VXL" % image
        ok_body, id_body = have(body_name)
        res = {
            "unit": unit,
            "image": image,
            "voxel": voxel,
            "turret": turret,
            "body": {"name": body_name, "id": "0x%08X" % id_body, "found": ok_body},
            "hva": None,
            "turret_part": None,
            "barrel_part": None,
        }
        ok_h, id_h = have("%s.HVA" % image)
        res["hva"] = {"name": "%s.HVA" % image, "id": "0x%08X" % id_h, "found": ok_h}

        if turret:
            tn = "%sTUR.VXL" % image
            ok_t, id_t = have(tn)
            res["turret_part"] = {"name": tn, "id": "0x%08X" % id_t, "found": ok_t}
            bn = "%sBARL.VXL" % image
            ok_b, id_b = have(bn)
            res["barrel_part"] = {"name": bn, "id": "0x%08X" % id_b, "found": ok_b}

        flh = art.get("primaryfireflh", "")
        if flh:
            res["flh"] = flh
        toff = art.get("turretoffset", "")
        if toff:
            res["turret_offset"] = toff
        return res


def main() -> None:
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    db = Db()
    print("rules 段 %d / art 段 %d / MIX 叶子 ID %d / 单位 %d"
          % (len(db.rules), len(db.art), len(db.ids), len(db.units)))

    names = args if args else [u for u, _l in db.units]
    rows = []
    n_vox = n_tur = n_barl = 0
    miss_body = []
    for u in names:
        r = db.resolve(u)
        rows.append(r)
        if not r["voxel"]:
            continue
        n_vox += 1
        if not r["body"]["found"]:
            miss_body.append((u, r["image"]))
        if r["turret_part"] and r["turret_part"]["found"]:
            n_tur += 1
        if r["barrel_part"] and r["barrel_part"]["found"]:
            n_barl += 1

    print("\n体素单位 %d 个：炮塔命中 %d，炮管命中 %d" % (n_vox, n_tur, n_barl))
    if miss_body:
        print("[!] Voxel=yes 但车体 VXL 找不到（%d 个）：" % len(miss_body))
        for u, im in miss_body[:20]:
            print("     %-12s Image=%s" % (u, im))

    if not args:
        # 全量：打印所有体素单位的三件套命中情况
        print("\n--- 体素单位清单 ---")
        for r in rows:
            if not r["voxel"]:
                continue
            t = r["turret_part"]
            b = r["barrel_part"]
            mark = lambda p: ("OK " if p and p["found"] else ("-- " if p else "   "))
            print("  %-12s %-12s %s%s%s  %s" % (
                r["unit"], r["image"],
                "车体OK " if r["body"]["found"] else "车体缺 ",
                mark(t) + ("炮塔" if t else ""),
                mark(b) + ("炮管" if b else ""),
                r["body"]["id"]))
        out = r"E:\ra2source\db\unit-vxl.json"
        with open(out, "w", encoding="utf-8") as f:
            json.dump(rows, f, ensure_ascii=False, indent=1)
        print("\n已写出 %s（%d 个单位）" % (out, len(rows)))
    else:
        for r in rows:
            print(json.dumps(r, ensure_ascii=False, indent=1))


if __name__ == "__main__":
    main()
