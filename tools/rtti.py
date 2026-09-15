"""
rtti.py -- 从 gamemd.exe 抽取 MSVC RTTI 信息，重建 C++ 类层次与虚函数表。

背景：gamemd.exe 是 2001 年的 MSVC x86 构建，保留了完整的 RTTI（约 960 个类型描述符）。
利用它可以零猜测地拿到：
  * 真实类名（.rdata 中的 ".?AVxxx@@" 类型描述符）
  * 每张虚表的地址、槽位数、每个槽位对应的函数入口（即类的方法）
  * 继承关系（RTTIClassHierarchyDescriptor -> 基类列表）

输出：
  db/rtti.json      机器可读的完整结果
  db/classes.md     人类可读的类清单（供 C++ 还原时对照）
"""

from __future__ import annotations

import argparse
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from peimage import PEImage, DEFAULT_IMAGE  # noqa: E402

# TypeDescriptor::name 前缀：.?AV = class, .?AU = struct
_TD_PREFIXES = (b".?AV", b".?AU")

DATA_SECTIONS = (".rdata", ".data", ".rsrc")


def _demangle_type(name: str) -> str:
    """.?AVFoo@@ -> Foo"""
    n = name
    for p in (".?AV", ".?AU", ".?AW", ".?AY", ".?A"):
        if n.startswith(p):
            n = n[len(p):]
            break
    n = n.rstrip("@")
    # 命名空间/模板嵌套：`Foo@Bar@@` -> Bar::Foo
    parts = [x for x in n.split("@") if x]
    if len(parts) > 1:
        parts = parts[::-1]
    return "::".join(parts)


class RTTIExtractor:
    def __init__(self, img: PEImage):
        self.img = img
        self.types: dict[int, str] = {}          # TypeDescriptor RVA -> 原始名 ".?AVFoo@@"
        self.name_rvas: dict[int, int] = {}      # name 字符串 RVA -> TypeDescriptor RVA
        self.coll_rvas: list[int] = []           # RTTICompleteObjectLocator RVA
        self.refs: dict[int, list[int]] = {}     # 被指向的 VA -> 引用它的 RVA 列表
        self.vtables: dict[int, dict] = {}       # vftable RVA -> 信息
        self.classes: dict[str, dict] = {}

    # ---------- 1. 类型描述符 ----------
    def scan_type_descriptors(self) -> None:
        data = self.img.data
        for sec in self.img.sections:
            if sec["name"] not in DATA_SECTIONS:
                continue
            base = sec["raw_ptr"]
            size = min(sec["raw_size"], sec["vsize"] or sec["raw_size"])
            blob = data[base:base + size]
            for pref in _TD_PREFIXES:
                start = 0
                while True:
                    i = blob.find(pref, start)
                    if i < 0:
                        break
                    start = i + 1
                    end = blob.find(b"\0", i)
                    if end < 0:
                        continue
                    raw = blob[i:end]
                    # 只接受纯 ASCII 且结尾是 @@ 的类型名，过滤误报
                    if not raw.endswith(b"@@") or len(raw) > 300:
                        continue
                    try:
                        s = raw.decode("ascii")
                    except UnicodeDecodeError:
                        continue
                    # 关键：MSVC 的 TypeDescriptor 布局是
                    #   struct TypeDescriptor { const void* pVFTable; void* spare; char name[]; };
                    # 也就是说 name[] 位于描述符起始 +8 处。
                    # 而 RTTICompleteObjectLocator.pTypeDescriptor 指向的是描述符起始，不是字符串，
                    # 所以这里必须以 "字符串 RVA - 8" 作为键，否则后续匹配必然全部落空。
                    name_rva = sec["rva"] + i
                    td_rva = name_rva - 8
                    # 校验描述符头：pVFTable 非空且可读，spare 必须为 0
                    if td_rva < 0:
                        continue
                    pvft = self.img.u32(td_rva)
                    spare = self.img.u32(td_rva + 4)
                    if not pvft or spare != 0:
                        continue
                    self.types[td_rva] = s
                    self.name_rvas[name_rva] = td_rva

    # ---------- 2. 建立 "被指向 VA -> 引用位置" 索引 ----------
    def build_ref_index(self) -> None:
        for sec in self.img.sections:
            if sec["name"] not in (".rdata", ".data"):
                continue
            off = sec["raw_ptr"]
            size = min(sec["raw_size"], sec["vsize"] or sec["raw_size"])
            for k in range(0, size - 3, 4):
                (v,) = struct.unpack_from("<I", self.img.data, off + k)
                self.refs.setdefault(v, []).append(sec["rva"] + k)

    # ---------- 3. CompleteObjectLocator ----------
    def scan_coll(self) -> None:
        ib = self.img.image_base
        for sec in self.img.sections:
            if sec["name"] not in (".rdata", ".data"):
                continue
            off = sec["raw_ptr"]
            size = min(sec["raw_size"], sec["vsize"] or sec["raw_size"])
            for k in range(0, size - 20, 4):
                p = off + k
                sig = struct.unpack_from("<I", self.img.data, p)[0]
                if sig not in (0, 1):
                    continue
                # 注意：RTTI 里的 pTypeDescriptor / pClassDescriptor 存的是 VA，不是 RVA
                ptd_va = struct.unpack_from("<I", self.img.data, p + 12)[0]
                ptd = ptd_va - ib
                if ptd not in self.types:
                    continue
                pcd = struct.unpack_from("<I", self.img.data, p + 16)[0]
                rva = sec["rva"] + k
                # pClassDescriptor 必须可读且合理
                if self.img.u32(pcd - ib) is None:
                    continue
                sig2 = self.img.u32(pcd - ib)
                nbase = self.img.u32(pcd - ib + 8)
                if sig2 not in (0, 1) or not (0 < (nbase or 0) < 128):
                    continue
                self.coll_rvas.append(rva)

    def parse_coll(self, coll_rva: int):
        ib = self.img.image_base
        sig = self.img.u32(coll_rva)
        offset = self.img.u32(coll_rva + 4)
        cd_offset = self.img.u32(coll_rva + 8)
        # 注意：COL 里的 pTypeDescriptor / pClassDescriptor 存的是 VA，必须减去 image_base
        # 才能当作 RVA 用（本二进制 relocations 已剥离，VA - image_base 即 RVA）。
        ptd = self.img.u32(coll_rva + 12) - ib
        pcd = self.img.u32(coll_rva + 16) - ib
        return {
            "rva": coll_rva,
            "va": ib + coll_rva,
            "signature": sig,
            "offset": offset,
            "cd_offset": cd_offset,
            "type_rva": ptd,
            "type_name": self.types.get(ptd, "?"),
            "type_demangled": _demangle_type(self.types.get(ptd, "?")),
            "hierarchy_rva": pcd,
        }

    def parse_bases(self, pcd_rva: int) -> list[dict]:
        ib = self.img.image_base
        out = []
        nbase = self.img.u32(pcd_rva + 8)
        pba = self.img.u32(pcd_rva + 12)
        if not nbase or not pba:
            return out
        arr = self.img.dwords(pba - ib, nbase)
        for a in arr:
            r = a - ib
            btd_va = self.img.u32(r)
            if btd_va is None:
                continue
            btd = btd_va - ib
            out.append({
                "type_name": self.types.get(btd, "?"),
                "demangled": _demangle_type(self.types.get(btd, "?")),
                "num_contained_bases": self.img.u32(r + 4),
                "mdisp": self.img.i32(r + 8),
                "pdisp": self.img.i32(r + 12),
                "vdisp": self.img.i32(r + 16),
                "attributes": self.img.u32(r + 20),
            })
        return out

    # ---------- 4. 虚表 ----------
    def build_vtables(self) -> None:
        ib = self.img.image_base
        lo, hi = self.img.text_range()

        # 第一遍：先用 COL 引用点确定所有虚表的确切起点
        starts: dict[int, dict] = {}
        for coll in self.coll_rvas:
            info = self.parse_coll(coll)
            for slot_rva in self.refs.get(ib + coll, []):
                vt_rva = slot_rva + 4
                if vt_rva not in starts:
                    starts[vt_rva] = info
        sorted_starts = sorted(starts)
        start_set = set(sorted_starts)

        # 第二遍：定界。延伸条件 = 值落在 .text 且 4 字节对齐；
        # 额外用「下一张已知虚表起点」截断，避免相邻虚表粘连成一张超长表。
        for idx, vt_rva in enumerate(sorted_starts):
            nxt = sorted_starts[idx + 1] if idx + 1 < len(sorted_starts) else None
            entries = []
            cur = vt_rva
            while True:
                if nxt is not None and cur >= nxt:
                    break
                if cur in start_set and cur != vt_rva:
                    break
                v = self.img.u32(cur)
                if v is None:
                    break
                if lo <= v - ib < hi and ((v - ib) & 3) == 0:
                    entries.append(v)
                    cur += 4
                    if len(entries) >= 1024:
                        break
                else:
                    break
            if not entries:
                continue
            info = starts[vt_rva]
            self.vtables[vt_rva] = {
                    "rva": vt_rva,
                    "va": ib + vt_rva,
                    "coll": info,
                    "type_name": info["type_name"],
                    "class": info["type_demangled"],
                    "offset_in_class": info["offset"],
                    "bases": self.parse_bases(info["hierarchy_rva"]),
                    "entries": entries,
                    "count": len(entries),
                }

    # ---------- 汇总 ----------
    def run(self) -> dict:
        self.scan_type_descriptors()
        self.build_ref_index()
        self.scan_coll()
        self.build_vtables()

        by_class: dict[str, dict] = {}
        for vt in self.vtables.values():
            c = by_class.setdefault(vt["class"], {
                "name": vt["class"],
                "type_name": vt["type_name"],
                "vtables": [],
                "bases": [],
            })
            c["vtables"].append(vt)
            if not c["bases"] and vt["bases"]:
                c["bases"] = [b["demangled"] for b in vt["bases"] if b["demangled"] != vt["class"]]
        for c in by_class.values():
            c["vtables"].sort(key=lambda x: (x["offset_in_class"], x["rva"]))
            c["base"] = c["bases"][0] if c["bases"] else None

        return {
            "image": self.img.path,
            "image_base": self.img.image_base,
            "type_descriptor_count": len(self.types),
            "coll_count": len(self.coll_rvas),
            "vtable_count": len(self.vtables),
            "types": {hex(k): v for k, v in self.types.items()},
            "vtables": self.vtables,
            "classes": by_class,
        }


def write_markdown(res: dict, out_path: str) -> None:
    """有虚表时输出类-虚表对照；没有时退化为「类型名词表」，两者都很有用。"""
    lines = [
        "# gamemd.exe 类型清单（RTTI 自动提取）",
        "",
        "由 `tools/rtti.py` 从二进制的 MSVC RTTI 中自动抽取，非人工猜测。",
        "",
        "- 类型描述符（TypeDescriptor）：%d" % res["type_descriptor_count"],
        "- CompleteObjectLocator：%d" % res["coll_count"],
        "- 可定位虚函数表：%d" % res["vtable_count"],
        "",
    ]
    if res["vtable_count"]:
        lines += [
            "| 类名 | 基类 | 虚表数 | 最大槽位数 | 虚表 VA |",
            "|---|---|---:|---:|---|",
        ]
        for name in sorted(res["classes"], key=lambda x: x.lower()):
            c = res["classes"][name]
            mx = max(v["count"] for v in c["vtables"])
            va = ", ".join("0x%06X" % v["va"] for v in c["vtables"][:3])
            lines.append("| `%s` | %s | %d | %d | %s |" % (
                name, "`%s`" % c["base"] if c["base"] else "—",
                len(c["vtables"]), mx, va))
    else:
        names = sorted({_demangle_type(v) for v in res["types"].values()})
        # 拆出模板，正文只列具名类/结构体
        plain = [n for n in names if "<" not in n]
        templ = [n for n in names if "<" in n]
        lines += [
            "> 本二进制保留了 TypeDescriptor，但没有类层次数据（CompleteObjectLocator 为 0），",
            "> 说明编译时未生成可用于 `dynamic_cast` 的 RTTI 图。类名本身仍然完全可信——",
            "> 它们就是还原 C++ 源码时的命名依据。虚表需用 `tools/analyze.py` 另行定位。",
            "",
            "## 具名类型（%d）" % len(plain),
            "",
        ]
        for n in plain:
            lines.append("- `%s`" % n)
        lines += ["", "## 模板实例（%d）" % len(templ), ""]
        for n in templ:
            lines.append("- `%s`" % n)
    open(out_path, "w", encoding="utf-8").write("\n".join(lines) + "\n")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", default=DEFAULT_IMAGE)
    ap.add_argument("--outdir", default=os.path.join(os.path.dirname(__file__), "..", "db"))
    a = ap.parse_args()

    img = PEImage(a.image)
    res = RTTIExtractor(img).run()
    outdir = os.path.abspath(a.outdir)
    os.makedirs(outdir, exist_ok=True)
    with open(os.path.join(outdir, "rtti.json"), "w", encoding="utf-8") as f:
        json.dump(res, f, ensure_ascii=False, indent=1)
    write_markdown(res, os.path.join(outdir, "classes.md"))
    print("types=%d coll=%d vtables=%d classes=%d" % (
        res["type_descriptor_count"], res["coll_count"],
        res["vtable_count"], len(res["classes"])))


if __name__ == "__main__":
    main()
