"""
unitvxl_check.py -- C++ 与 Python 两份"单位 -> 体素模型"解析结果逐行对账。

跟项目里其它对账脚本一个道理：格式理解对不对，唯一可信的验收是
两边各算一遍、结果一致。这里对的是 P2 数据层的解析规则：
  Image= 的取值、Voxel/Turret 的布尔、三段 CRC 是否拼对、文件在不在包里。

用法：
  python tools/unitvxl_check.py build/unitdb_cpp.txt [db/unit-vxl.json]
"""

from __future__ import annotations

import json
import sys


def load_cpp(path):
    out = {}
    for line in open(path, encoding="utf-8"):
        s = line.strip()
        if not s or s.startswith("#"):
            continue
        f = s.split()
        if len(f) != 10:
            print("[x] 列数不对（应为 10）：%r" % s)
            sys.exit(1)
        out[f[0]] = {
            "image": f[1], "voxel": f[2] == "1", "turret": f[3] == "1",
            "body": f[4], "body_ok": f[5] == "1",
            "tur": f[6], "tur_ok": f[7] == "1",
            "barl": f[8], "barl_ok": f[9] == "1",
        }
    return out


def load_py(path):
    out = {}
    for r in json.load(open(path, encoding="utf-8")):
        p = lambda k: (r.get(k) or {})
        out[r["unit"].upper()] = {
            "image": r["image"].upper(),
            "voxel": bool(r["voxel"]), "turret": bool(r["turret"]),
            # 注意：ID 字符串不要 .upper() —— 会把 "0x" 变成 "0X"。
            "body": r["body"]["id"], "body_ok": bool(r["body"]["found"]),
            "tur": p("turret_part").get("id", "0x00000000"),
            "tur_ok": bool(p("turret_part").get("found", False)),
            "barl": p("barrel_part").get("id", "0x00000000"),
            "barl_ok": bool(p("barrel_part").get("found", False)),
        }
    return out


def main() -> None:
    cpp_path = sys.argv[1] if len(sys.argv) > 1 else r"E:\ra2source\build\unitdb_cpp.txt"
    py_path = sys.argv[2] if len(sys.argv) > 2 else r"E:\ra2source\db\unit-vxl.json"

    a = load_cpp(cpp_path)
    b = load_py(py_path)
    print("C++ %d 个单位 / Python %d 个单位" % (len(a), len(b)))

    keys = sorted(set(a) | set(b))
    bad = 0
    for k in keys:
        if k not in a:
            print("  只在 Python: %s" % k)
            bad += 1
            continue
        if k not in b:
            print("  只在 C++   : %s" % k)
            bad += 1
            continue
        ra, rb = a[k], b[k]
        for f in ("image", "voxel", "turret", "body", "body_ok",
                  "tur", "tur_ok", "barl", "barl_ok"):
            if ra[f] != rb[f]:
                print("  差异 %-12s %-10s  C++=%r  Python=%r" % (k, f, ra[f], rb[f]))
                bad += 1

    nvox = sum(1 for k in a if a[k]["voxel"])
    nbody = sum(1 for k in a if a[k]["voxel"] and a[k]["body_ok"])
    ntur = sum(1 for k in a if a[k]["tur_ok"])
    nbarl = sum(1 for k in a if a[k]["barl_ok"])
    print("体素单位 %d，车体命中 %d，炮塔 %d，炮管 %d" % (nvox, nbody, ntur, nbarl))

    if bad == 0:
        print("逐行对账通过：%d 个单位 × 9 个字段完全一致" % len(keys))
    else:
        print("共 %d 处不一致" % bad)
        sys.exit(1)


if __name__ == "__main__":
    main()
