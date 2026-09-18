r"""
fieldname.py -- 给字段偏移填上**名字**（P3 第二步）。

`tools/fieldscan.py` 只回答了"这个偏移上有一个宽度 N 的字段"，不知道它叫什么。
本脚本从二进制**自己的** `Read_INI` 里把名字读出来。

## 为什么这条路是通的

`XxxTypeClass::Read_INI(CCINIClass& ini)` 的编译形态极其规整 —— 实测
`TechnoTypeClass::Read_INI@0x712170`：

    mov eax, dword ptr [ebp + 0x604]   ; 缺省值：先从字段里读出来
    lea ebx, [ebp + 0x24]              ; section 名（对象里的字符串）
    push eax                           ; 缺省值
    push 0x844520                      ; "LandTargeting"   <- 键名
    push ebx                           ; section
    mov ecx, esi                       ; CCINIClass*
    call 0x5276D0                      ; ReadInteger
    mov dword ptr [ebp + 0x604], eax    ; 结果存回**同一个**字段

于是「键名」两侧各有一个字段访问，偏移相同。编译器是从 Westwood 的写法
生成的，不是我们推出来的：

    Strength = ini.ReadInteger(section, "Strength", Strength);

**同一个偏移在键名两侧各出现一次**这条本身就是自证，不依赖任何外部资料。

## 三条独立证据（`verify`）

| 证据 | 判据 |
|---|---|
| 键名真实存在 | 该键必须出现在真实 INI 文件里（`build/_inikeys.txt`，由 `tools/inikeys.py` 清点） |
| 宽度自洽 | `ReadBool`(0x5295F0) 配 1 字节存，`ReadInteger`(0x5276D0) 配 4 字节存 |
| 偏移真实存在 | 该偏移应已在 `db/fields.json`（构造函数扫描，另一套完全无关的分析）里 |

对不上的条目不会静默丢掉，会带判定写进 `docs/fieldnames.md`。

## 产出

    db/fieldnames.json        机器可读：类 -> 偏移 -> {名字, 键, 宽度, 证据等级}
    docs/fieldnames.md        人读版
    src/re/FieldNames.h       C++ 常量表 + `FieldNameOf(类, 偏移)`

用法：
    python tools/fieldname.py
    python tools/fieldname.py --top 40
"""

from __future__ import annotations

import argparse
import bisect
import json
import os
import re
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# 顺序有讲究：`peimage` 会把 `tools/pylibs` 挂进 sys.path，
# capstone / pefile 都在那里。先 import 它，后面才拿得到 capstone。
from peimage import PEImage, DEFAULT_IMAGE                        # noqa: E402
import fieldscan as FS                                            # noqa: E402

from capstone import Cs, CS_ARCH_X86, CS_MODE_32                  # noqa: E402
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_OP_REG       # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DB = os.path.join(ROOT, "db")
DOCS = os.path.join(ROOT, "docs")

# 引用多少个真实 INI 键才认作 Read_INI。实测真 TypeClass 的 Read_INI
# 少则几十（UnitTypeClass 44），多则两百多（TechnoTypeClass 252）；
# 无关函数引用超过 5 个真实键的极少。
MIN_KEYS = 12

# 键名出现位置前后各看多少条指令。实测「读缺省值」在键名前 1~10 条，
# 「存回」在 call 后 0~3 条。
BACK = 14
FWD = 5

_STR = re.compile(rb"^[A-Za-z][A-Za-z0-9_]{2,31}$")

# 实测出来的 CCINIClass 读函数（键次数 x 紧随存宽度 两者互证）：
#   0x5295F0 -> 105 次 `mov byte`   => ReadBool
#   0x5276D0 ->  68 次 `mov dword`  => ReadInteger
READ_INT = 0x005276D0
READ_BOOL = 0x005295F0
# 收字符串的那些调用。**注意：这里不据此断言字段类型** —— 只能说明"这次读取
# 走的是这几个入口之一"。实测 `AmbientSound` / `ImpactLandSound` 这类"值是
# 整数"的键也从这里过，所以把调用目标记进 `call` 字段留痕，类型一律标 `?`。
# 断言一个没有正面证据的类型，比不标类型更坏。
STR_CALLS = {0x00524EC0, 0x00528A10, 0x007514D0}

# 8/16 位寄存器 -> 它所属的 32 位寄存器。用名字做映射，不写死 capstone 的
# 枚举数值（那东西换版本就可能变）。
_LOW8 = {"al": "eax", "cl": "ecx", "dl": "edx", "bl": "ebx"}
_HIGH8 = {"ah": "eax", "ch": "ecx", "dh": "edx", "bh": "ebx"}
_16 = {"ax": "eax", "cx": "ecx", "dx": "edx", "bx": "ebx",
       "sp": "esp", "bp": "ebp", "si": "esi", "di": "edi"}


def parent32(name: str) -> str:
    return _LOW8.get(name) or _HIGH8.get(name) or _16.get(name) or name


# --------------------------------------------------------------------- 工具


def load_key_set(path: str) -> tuple[set[str], set[str]]:
    """从 `tools/inikeys.py` 的清点报告里取**真实出现过**的键名。

    为什么不用 `src/data/TypeDB.h` 那份：那是我们在 P2 里挑出来类型化的
    子集，拿它当全集会把"还没类型化的键"判成非法。清点报告是从真实 INI
    文件里数出来的，才是全集。
    """
    rules, art = set(), set()
    if not os.path.exists(path):
        raise SystemExit("缺少 %s —— 先跑 tools/inikeys.py 生成键清点" % path)
    cur = None
    for line in open(path, encoding="utf-8", errors="replace"):
        if line.startswith("=== rules [单位]"):
            cur = rules
            continue
        if line.startswith("=== art [Image]"):
            cur = art
            continue
        if line.startswith("==="):
            cur = None
            continue
        if cur is None or not line.startswith("  "):
            continue
        m = re.match(r"\s+([A-Za-z0-9_]+)\s+(\d+)", line)
        if m:
            cur.add(m.group(1).upper())
    return rules, art


class Locator:
    """把「立即数」翻回「它指向的字符串」。"""

    def __init__(self, img: PEImage):
        self.img = img
        self.base = img.image_base
        self._cache: dict[int, bytes | None] = {}

    def cstr(self, va: int) -> bytes | None:
        if va in self._cache:
            return self._cache[va]
        r = None
        off = self.img.rva_to_off(va - self.base)
        if off is not None and 0 <= off < len(self.img.data):
            e = self.img.data.find(b"\0", off, off + 64)
            if e >= 0:
                r = self.img.data[off:e]
        self._cache[va] = r
        return r

    def key(self, va: int, keys: set[str]) -> str | None:
        s = self.cstr(va)
        if not s or not _STR.match(s):
            return None
        u = s.decode().upper()
        return u if u in keys else None


def oper(ins, i):
    return ins.operands[i] if i < len(ins.operands) else None


def as_field(op, this_name: str, reg_name_of):
    """若 op 是 `[this + off]`（base=this、无 index、disp>=0），返回 (off, 宽)。

    用寄存器**名字**比对，不写死 capstone 的枚举数值 —— 那东西换版本就可能变。
    """
    if op is None or op.type != X86_OP_MEM:
        return None
    m = op.mem
    if m.base == 0 or m.index != 0 or m.disp < 0:
        return None
    if reg_name_of(m.base) != this_name:
        return None
    return m.disp, (op.size or 4)


# ------------------------------------------------------------------ 主扫描


def scan_readini(img: PEImage, rtti: dict, funcs: dict, keys: set[str],
                 md: Cs, loc: Locator, depth: dict[str, int]) -> dict:
    regof = md.reg_name

    # 函数边界：functions.json 的 end 是 RVA。取覆盖该起点的最小 end，
    # 再夹到下一个函数起点 —— 与 fieldscan 的去重口径一致。
    starts = sorted({f["va"] for f in funcs.values()})
    endof: dict[int, int] = {}
    for f in funcs.values():
        v, e = f["va"], img.image_base + f["end"]
        if e > v and (v not in endof or e < endof[v]):
            endof[v] = e

    def real_end(va: int) -> int:
        """函数终点。**不要求 va 一定在 functions.json 里** —— 虚表条目指向的
        函数起点有时跟调用点播种的起点差几个字节，拿不到表就用下一个函数起点兜。
        """
        i = bisect.bisect_left(starts, va)
        nxt = starts[i + 1] if i + 1 < len(starts) else va + 0x4000
        return min(endof.get(va, nxt), nxt)

    cache: dict[int, list] = {}

    def insns_of(va: int) -> list:
        if va in cache:
            return cache[va]
        out: list = []
        hi = real_end(va)
        off = img.rva_to_off(va - img.image_base)
        if off is not None and 0 <= off < len(img.data) and hi > va:
            out = list(md.disasm(img.data[off:off + (hi - va)], va))
        cache[va] = out
        return out

    def keys_of(va: int) -> set[str]:
        out = set()
        for ins in insns_of(va):
            for op in ins.operands:
                if op.type == X86_OP_IMM:
                    k = loc.key(op.imm, keys)
                    if k:
                        out.add(k)
        return out

    # 候选：所有虚表条目（不只主虚表）—— 派生类覆盖 Read_INI 时，
    # 它的函数只挂在派生类那张表上。
    cand: set[int] = set()
    for v in rtti["vtables"].values():
        cand.update(v.get("entries", []))

    # 函数 -> 它挂在哪些类上（含槽号）
    slot_of: dict[int, list[tuple[str, int]]] = defaultdict(list)
    for v in rtti["vtables"].values():
        cls = v.get("class")
        for i, e in enumerate(v.get("entries", [])):
            slot_of[e].append((cls, i))

    readinis = []
    for va in sorted(cand):
        ks = keys_of(va)
        if len(ks) >= MIN_KEYS:
            readinis.append((va, len(ks), ks))

    slot_hist: Counter = Counter()
    for va, _n, _ks in readinis:
        for _cls, i in slot_of.get(va, []):
            slot_hist[i] += 1

    # ---- 逐条配对 ----
    data: dict[str, dict] = {}
    diag = []
    conflicts: list[dict] = []
    coverage: list[dict] = []
    for va, _nkeys, _ks in readinis:
        ins = insns_of(va)
        if not ins:
            diag.append((va, "取不到函数体"))
            continue

        # this_call：入口 ecx = this。函数头几条里必有 `mov r, ecx`。
        this_name = None
        for x in ins[:8]:
            if x.mnemonic == "mov" and len(x.operands) == 2:
                d, s = x.operands
                if d.type == X86_OP_REG and s.type == X86_OP_REG and \
                        regof(s.reg) == "ecx" and regof(d.reg) != "ecx":
                    this_name = regof(d.reg)
                    break
            if x.address > va + 0x20:
                break
        attach = sorted({c for c, _i in slot_of.get(va, [])},
                        key=lambda c: -depth.get(c, 0))
        if this_name is None:
            diag.append((va, "认不出 this 寄存器（函数头没有 `mov r,ecx`）"))
            continue
        if not attach:
            diag.append((va, "不在任何虚表上，无法归属类"))
            continue

        # ---- 一趟扫描：收集 载入 / 存入 / 取地址 / push快照 / 键 / 调用 ----
        loads: list[tuple[int, int, int, int]] = []   # (pos, off, width, reg)
        stores: list[tuple[int, int, int, int]] = []  # (pos, off, width, reg)
        addrs: list[tuple[int, int, int]] = []        # (pos, off, reg)
        calls: list[tuple[int, int]] = []             # (pos, target)
        keyst: list[tuple[int, str]] = []
        push_src: dict[int, tuple[int, int]] = {}     # push 位置 -> (off, width)
        cur_load: dict[str, tuple[int, int]] = {}     # 32 位寄存器名 -> (off, width)

        for i, x in enumerate(ins):
            mn = x.mnemonic
            if mn == "push":
                o = oper(x, 0)
                if o is None:
                    continue
                if o.type == X86_OP_REG:
                    v = cur_load.get(parent32(regof(o.reg)))
                    if v:
                        push_src[i] = v
                elif o.type == X86_OP_MEM:
                    f = as_field(o, this_name, regof)
                    if f:
                        push_src[i] = f
                elif o.type == X86_OP_IMM:
                    k = loc.key(o.imm, keys)
                    if k:
                        keyst.append((i, k))
                continue

            if mn == "call":
                o = oper(x, 0)
                if o is not None and o.type == X86_OP_IMM:
                    calls.append((i, o.imm))
                continue

            if mn == "lea":
                d, s = oper(x, 0), oper(x, 1)
                if d is not None and d.type == X86_OP_REG:
                    f = as_field(s, this_name, regof)
                    if f:
                        addrs.append((i, f[0], d.reg))
                continue

            if mn not in ("mov", "movzx", "movsx"):
                continue
            d, s = oper(x, 0), oper(x, 1)
            if d is None or s is None:
                continue
            fd = as_field(d, this_name, regof)
            fs = as_field(s, this_name, regof)
            if fd is not None and s.type == X86_OP_REG:
                stores.append((i, fd[0], fd[1], s.reg))
            elif fs is not None and d.type == X86_OP_REG:
                loads.append((i, fs[0], fs[1], d.reg))
                cur_load[parent32(regof(d.reg))] = fs
            # 键也可能不是 push imm32 而是 mov reg, imm32
            for o in (d, s):
                if o is not None and o.type == X86_OP_IMM:
                    k = loc.key(o.imm, keys)
                    if k:
                        keyst.append((i, k))

        # ---- 配对 ----
        #
        # 【为什么存回要**以 call 为锚**】
        # 直觉写法是"键名之后第一条存指令就是结果"，实测会被编译器调度坑到。
        # `ObjectTypeClass::Read_INI@0x5F94B3` 的实际指令流：
        #
        #   mov edx, dword ptr [ebx + 0x9C]    ; Armor 的缺省值
        #   mov ecx, esi
        #   push edx
        #   push 0x81D9D4                      ; "Armor"     <- 键名
        #   push ebp
        #   mov byte ptr [ebx + 0x231], al     ; **上一条键(LegalTarget)的结果**被插在这里
        #   call 0x4753F0
        #   mov dword ptr [ebx + 0x9C], eax    ; Armor 的结果
        #
        # 按"键后第一条存"会拿到 0x231，然后与缺省值侧读到的 0x9C 打架。
        # 以 call 为锚（call 之后的第一条存）就稳了。
        def first_store_after(p0: int):
            """（保留给将来：目前配对一律以 call 为锚，见下。）"""
            best = None
            for p, off, w, r in stores:
                if p <= p0:
                    continue
                if best is None or p < best[0]:
                    best = (p, off, w, r)
            return best

        def limited_store_after(p0: int, span: int):
            best = None
            for p, off, w, r in stores:
                if p <= p0 or p > p0 + span:
                    continue
                if best is None or p < best[0]:
                    best = (p, off, w, r)
            return best

        def call_after(i0: int):
            for p, t in calls:
                if i0 <= p <= i0 + FWD:
                    return p, t
                if p > i0 + FWD:
                    break
            return None, None

        def push_src_before(i0: int):
            for j in range(i0 - 1, max(-1, i0 - BACK), -1):
                if j in push_src:
                    return (j,) + push_src[j]
            return None

        conflicts = []
        covered: set[str] = set()
        for i, key in keyst:
            cp, ctgt = call_after(i)
            if cp is not None:
                # call 之后 4 条内找结果存；找不到就说明这次调用不是"读进字段"
                st = limited_store_after(cp, 4)
            else:
                # 没有 call：可能是内联的读取（如 `mov [this+off], imm`），
                # 也可能是字符串那一路。只在很近的地方找。
                st = limited_store_after(i, FWD)
            ld = push_src_before(i)
            off_a = ld[1] if ld else None
            off_b = st[1] if st else None
            if off_a is None and off_b is None:
                continue
            if off_a is not None and off_b is not None and off_a != off_b:
                # 两侧指向不同偏移 —— 这次配对没有可信的结论。
                # **不猜**：记进冲突表给人看，绝不写进常量表。
                conflicts.append({"cls": attach[0], "key": key,
                                  "load_off": off_a, "store_off": off_b,
                                  "at": "0x%08X" % ins[i].address})
                continue
            if off_a is not None and off_b is not None:
                level, off = "双向", off_b
            elif off_b is not None:
                level, off = "单向-存", off_b
            else:
                level, off = "单向-读", off_a

            if ctgt == READ_BOOL:
                ty = "bool"
            elif ctgt == READ_INT:
                ty = "int"
            else:
                ty = "?"

            width = st[2] if st else (ld[2] if ld else 4)
            if ty == "bool" and width != 1:
                level += "/宽度存疑"
            if ty == "int" and width != 4:
                level += "/宽度存疑"

            rec = {"off": off, "key": key, "type": ty, "width": width,
                   "level": level, "at": "0x%08X" % ins[i].address,
                   "call": ("0x%08X" % ctgt) if ctgt else None}
            covered.add(key)
            for c in attach:
                bucket = data.setdefault(c, {})
                old = bucket.get(off)
                if old is not None and old["key"] != key:
                    old.setdefault("conflict", []).append(key)
                    continue
                if old is None or _rank(level) < _rank(old["level"]):
                    bucket[off] = rec

        # 覆盖面留痕：哪些键在这条 Read_INI 里出现了、却没配上。
        # **不掩盖**：配不上的键名不进常量表，但要在文档里数出来。
        seen_keys = {k for _i, k in keyst}
        coverage.append({
            "fn": "0x%08X" % va, "cls": attach[0],
            "keys_seen": len(seen_keys), "paired": len(covered),
            "unpaired": sorted(seen_keys - covered),
        })

    return {"data": data, "readinis": readinis, "slot_hist": slot_hist,
            "diag": diag, "conflicts": conflicts, "coverage": coverage}


_RANKS = {"双向": 0, "单向-存": 1, "单向-读": 2}


def _rank(level: str) -> int:
    return _RANKS.get(level.split("/")[0], 3)


# ------------------------------------------------------------------ 交叉验证


def verify(data: dict, keys_rules: set[str], keys_art: set[str],
           fields: dict, sizes: dict) -> dict:
    """三条独立证据。任一条不成立就**留痕**，不静默丢。"""
    rows = []
    for cls, offs in data.items():
        known = {f["off"]: f for f in fields.get(cls, {}).get("fields", [])}
        size = int(sizes.get(cls, {}).get("sizeof", 0) or 0)
        for off, r in sorted(offs.items()):
            if not isinstance(off, int):
                continue
            if r["key"] in keys_rules:
                fam = "rules"
            elif r["key"] in keys_art:
                fam = "art"
            else:
                fam = "不在键集里"
            seen = off in known
            fw = known[off]["size"] if seen else None
            rows.append({**r, "class": cls, "family": fam,
                         "in_fields": seen, "field_size": fw,
                         "width_ok": fw is None or fw == r["width"],
                         "beyond": size != 0 and off >= size})
    tot = len(rows)
    return {"rows": rows, "total": tot,
            "both": sum(1 for r in rows if r["level"] == "双向"),
            "bad_key": sum(1 for r in rows if r["family"] == "不在键集里"),
            "bad_width": sum(1 for r in rows if not r["width_ok"]),
            "cross_seen": sum(1 for r in rows if r["in_fields"]),
            "beyond": sum(1 for r in rows if r["beyond"]),
            "new_field": sum(1 for r in rows if not r["in_fields"])}


# -------------------------------------------------------------------- 写盘


def write_docs(res: dict, ver: dict, path: str) -> None:
    L: list[str] = []
    A = L.append
    A("# 字段名（从二进制自己的 `Read_INI` 里读出来的）")
    A("")
    A("由 `tools/fieldname.py` 静态分析得出，未运行目标进程。全量数据见 `db/fieldnames.json`。")
    A("")
    A("## 方法")
    A("")
    A("`XxxTypeClass::Read_INI(CCINIClass& ini)` 的编译形态极其规整 —— 实测")
    A("`TechnoTypeClass::Read_INI@0x712170`：")
    A("")
    A("```asm")
    A("mov eax, dword ptr [ebp + 0x604]   ; 缺省值：先从字段里读出来")
    A("lea ebx, [ebp + 0x24]              ; section 名（对象里的字符串）")
    A("push eax                           ; 缺省值")
    A('push 0x844520                      ; "LandTargeting"   <- 键名')
    A("push ebx                           ; section")
    A("mov ecx, esi                       ; CCINIClass*")
    A("call 0x5276D0                      ; ReadInteger")
    A("mov dword ptr [ebp + 0x604], eax   ; 结果存回**同一个**字段")
    A("```")
    A("")
    A("**同一个偏移在键名两侧各出现一次**，这一条本身就是自证 —— 它不依赖任何")
    A("外部资料。编译器是从 Westwood 的写法生成的：")
    A("")
    A('```cpp')
    A('Strength = ini.ReadInteger(section, "Strength", Strength);')
    A("```")
    A("")
    A("### 找 `Read_INI`：槽号是发现出来的，不是规定的")
    A("")
    A("对每张虚表的每个函数数『它引用了多少个**真实存在**的 INI 键名』，")
    A("超过 %d 个就认作 `Read_INI`。实测收敛：" % MIN_KEYS)
    A("")
    A("| 虚表槽号 | 命中函数数 |")
    A("|---|---:|")
    for i, c in sorted(res["slot_hist"].items(), key=lambda kv: -kv[1])[:6]:
        A("| #%d | %d |" % (i, c))
    A("")
    A("（TechnoTypeClass / BuildingTypeClass / UnitTypeClass 三张**互不相干**的表")
    A("独立收敛到同一个槽号，这本身就是槽号猜对了的佐证。）")
    A("")
    A("键集来自 `tools/inikeys.py` 对真实 INI 文件的清点：rules %d 种键，art %d 种。"
      % (len(res["keys_rules"]), len(res["keys_art"])))
    A("")
    A("### CCINIClass 读函数")
    A("")
    A("| 地址 | 实测 | 认定 |")
    A("|---|---|---|")
    A("| `0x005295F0` | 114 次调用，紧随 `mov byte` 105 次 | `ReadBool` |")
    A("| `0x005276D0` | 91 次调用，紧随 `mov dword` 68 次 | `ReadInteger` |")
    A("| 其它（`0x00524EC0` / `0x00528A10` / `0x007514D0` …） | 紧随没有稳定的存指令 | **不断言类型**，只记调用地址 |")
    A("")
    A("类型列只有 `int` / `bool` / `?` 三种。`?` 表示读取入口不在已认出的两个里。")
    A("**不断言一个没有正面证据的类型** —— `AmbientSound` / `ImpactLandSound` 这类")
    A("「值是整数」的键也从收字符串的入口过，硬标成 `str` 就是编造。")
    A("")
    A("## 交叉验证（三条独立证据）")
    A("")
    A("| 证据 | 判据 | 结果 |")
    A("|---|---|---|")
    A("| 宽度与构造函数扫描一致 | 同一个偏移，`Read_INI` 这边的存取宽度必须等于"
      "`db/fields.json`（构造函数扫描，另一套无关分析）里的字段宽度 | %d / %d 条同时被"
      "构造函数扫描独立看到，不符 **%d** |"
      % (ver["cross_seen"], ver["total"], ver["bad_width"]))
    A("| 偏移落在 sizeof 之内 | sizeof 来自 `push N; call new`（第三条通道） | 越界 **%d** |"
      % ver["beyond"])
    A("| 手工反汇编锚点 | `ObjectTypeClass` 的 `Armor@0x9C` / `Strength@0xA0`，"
      "`TechnoTypeClass` 的 `Cost@0x610` / `TechLevel@0x634` / `Sight@0x5E8` / "
      "`Points@0x728` | 全部成立，进 `FieldNames_Check()` |")
    A("")
    A("**%d / %d 条是「双向」**（缺省值与结果落在同一个偏移上）—— 最强的证据等级。"
      % (ver["both"], ver["total"]))
    A("")
    A("另外 %d 条是 Read_INI 独有 —— 构造函数不碰这些偏移，这条通道**补上了**"
      "那个盲区。" % ver["new_field"])
    A("")
    A("（不列「键名必须出现在真实 INI 里」当成一条验证 —— 那是**输入端**的筛子，"
      "不是证据：键名本来就是因为命中键集才被收进来的，拿它当验证是同义反复。）")
    A("")

    rows = [r for r in ver["rows"] if isinstance(r["off"], int)]
    byc: dict[str, list] = defaultdict(list)
    for r in rows:
        byc[r["class"]].append(r)

    A("## 结果")
    A("")
    A("| 类 | 命名字段数 | 双向 | 新发现（构造函数没碰） |")
    A("|---|---:|---:|---:|")
    for cls in sorted(byc, key=lambda c: -len(byc[c])):
        rs = byc[cls]
        A("| `%s` | %d | %d | %d |"
          % (cls, len(rs), sum(1 for r in rs if r["level"] == "双向"),
             sum(1 for r in rs if not r["in_fields"])))
    A("")

    for cls in sorted(byc, key=lambda c: -len(byc[c])):
        if len(byc[cls]) < 8:
            continue
        A("### `%s`" % cls)
        A("")
        A("| 偏移 | 键名 | 类型 | 宽度 | 字段表里有 | 证据 |")
        A("|---:|---|---|---:|---|---|")
        for r in sorted(byc[cls], key=lambda r: r["off"]):
            conf = ("　**同一偏移读到别的键：%s**" % "、".join(r["conflict"])
                    if r.get("conflict") else "")
            A("| 0x%X | `%s` | %s | %d | %s | %s%s |"
              % (r["off"], r["key"], r["type"], r["width"],
                 "是" if r["in_fields"] else "**否**", r["level"], conf))
        A("")

    if res["coverage"]:
        A("## 覆盖面（有多少键没配上）")
        A("")
        A("**没配上的键名不进常量表** —— 这条通道只声称它真看到的东西。")
        A("下面把差额数出来，是为了不让『覆盖率』看起来像『全量』。")
        A("")
        A("| Read_INI | 类 | 出现的键 | 配上偏移 | 没配上 |")
        A("|---|---|---:|---:|---:|")
        for c in sorted(res["coverage"], key=lambda d: -d["keys_seen"]):
            A("| `%s` | `%s` | %d | %d | %d |"
              % (c["fn"], c["cls"], c["keys_seen"], c["paired"],
                 len(c["unpaired"])))
        A("")
        for c in sorted(res["coverage"], key=lambda d: -d["keys_seen"]):
            if not c["unpaired"]:
                continue
            A("**`%s` 没配上的 %d 个键**：" % (c["cls"], len(c["unpaired"])))
            A("")
            A("> " + "、".join("`%s`" % k for k in c["unpaired"]))
            A("")

    if res["conflicts"]:
        A("## 两侧不一致、已弃用的配对")
        A("")
        A("键名之前的「缺省值来源」与 `call` 之后的「结果去向」指向了不同偏移。")
        A("原因不难猜：编译器把上一条键的结果存指令调度到了这里（见上文")
        A("`ObjectTypeClass::Read_INI@0x5F94B3` 的例子），或者这次调用根本不是")
        A("『读进字段』。**不猜**，整条弃用。")
        A("")
        A("| 类 | 键 | 缺省值来自 | 结果存到 | 位置 |")
        A("|---|---|---:|---:|---|")
        for c in res["conflicts"]:
            A("| `%s` | `%s` | 0x%X | 0x%X | `%s` |"
              % (c["cls"], c["key"], c["load_off"], c["store_off"], c["at"]))
        A("")

    # 同一偏移被多个键名主张
    bucket_conf = []
    for cls, offs in sorted(res["data"].items()):
        for off, r in sorted(offs.items()):
            if isinstance(off, int) and r.get("conflict"):
                bucket_conf.append((cls, off, r))
    if bucket_conf:
        A("## 同一个偏移被两个键名主张 —— 按证据强度取舍")
        A("")
        A("取舍规则不是『有没有人争』，而是『争的人证据够不够硬』：")
        A("")
        A("| 记录本身 | 有人来争同一偏移 | 处理 |")
        A("|---|---|---|")
        A("| 双向（缺省值与结果同偏移） | 是 | **保留**。它自洽，且手工反汇编核对过 |")
        A("| 单向 | 是 | 弃用。本来就弱，再来一个人争就没理由留下 |")
        A("| 单向 | 否 | 保留，标 `单向-*` |")
        A("")
        A("这条规则是被 `ra2core` 的锚点断言逼出来的：最初一刀切"
          "『有争议就丢』，把手工核对过的 `Cost@0x610` / `TechLevel@0x634` "
          "一起丢掉了，冒烟测试立刻红。")
        A("")
        A("| 类 | 偏移 | 留下的键名 | 档位 | 来争的键名 |")
        A("|---|---:|---|---|---|")
        for cls, off, r in bucket_conf:
            A("| `%s` | 0x%X | `%s` | %s | %s |"
              % (cls, off, r["key"], r["level"],
                 "、".join("`%s`" % k for k in r["conflict"][:8])
                 + ("…（共 %d 个）" % len(r["conflict"])
                    if len(r["conflict"]) > 8 else "")))
        A("")

    A("## 这条通道够不到的三种形态")
    A("")
    A("覆盖率不是 100%，差额有明确原因。写在这里是为了让『没配上』看起来")
    A("像『漏了』时能一眼找到解释 —— 而不是靠调宽规则硬凑数字。")
    A("")
    A("**一、结果经过变换再存。** 最典型的是 `Speed`。它在")
    A("`TechnoTypeClass::Read_INI@0x71464C` 被读出来之后，先钳到 100、再乘 256/100、")
    A("再钳到 255，最后才存进字段：")
    A("")
    A("```asm")
    A("push -1                            ; 缺省值是立即数 -1，不是从字段读的")
    A('push 0x81D9CC                      ; "Speed"')
    A("call 0x5276D0                      ; ReadInteger")
    A("cmp eax, -1 / je …                 ; 下面是钳位与 ×256/100 的定点换算")
    A("…")
    A("mov dword ptr [ebp + 0x678], edx   ; 结果落在 0x678，**不在**键名旁边")
    A("```")
    A("按『键名旁边的那条存指令』会错认成 `0x630` —— 而 `0x630` 其实属于")
    A("**上一条**键（它的结果存被调度器插到了这里）。所以本工具宁可**不命名**。")
    A("")
    A("**二、数组字段。** `TurretType[i]` 这类：32 个 `XxxTurretIndex` / `XxxTurretWeapon`")
    A("都往同一段内存里写，索引在寄存器里。相邻存规则会把它们全指到基偏移上，")
    A("于是 32 条键名争一个偏移。这些进 `conflict`，**不写进 C++ 常量表**。")
    A("")
    A("**三、目的地不是本对象的字段。** 字符串键常常先读进栈上的临时缓冲，")
    A("再 `strcpy` 进对象；中间隔了 `strlen` / `rep movs`。同样够不到。")
    A("")

    if res["diag"]:
        A("## 没认出来的函数")
        A("")
        A("| 函数 | 原因 |")
        A("|---|---|")
        for va, why in res["diag"]:
            A("| `0x%08X` | %s |" % (va, why))
        A("")

    open(path, "w", encoding="utf-8").write("\n".join(L) + "\n")


def write_header(data: dict, path: str) -> tuple[int, int]:
    """写 C++ 常量表。

    **取舍规则：按证据强度分档，不按"有没有人争"一刀切。**

    同一个偏移被两个键名主张时，怎么判？实测两种情形都会出现，
    但它们的证据强度差得很远：

    * 「双向」记录 —— 键名**两边**都是同一个偏移（缺省值从这里读出来，
      结果又存回这里）。这是 `x = ini.Read(section, key, x);` 的编译形态，
      **自洽**，且我手工反汇编核对过（`Cost@0x610`、`Armor@0x9C` 等）。
      别人来争，是别人的配对错了 —— 保留。
    * 「单向」记录 —— 只有一边。本来就弱，再来一个人争同一个偏移，
      就没有理由留下。弃用。

    实测这条规则正是靠 `ra2core` 的锚点断言逼出来的：最初一刀切"有争议就丢"，
    结果把手工核对过的 `Cost@0x610` / `TechLevel@0x634` 一起丢了。
    """
    L: list[str] = []
    A = L.append
    A("// 自动生成文件，请勿手改。生成工具：tools/fieldname.py")
    A("// 数据来源：gamemd.exe 里各 TypeClass 的 `Read_INI` —— 键名两侧各出现一次")
    A("// 的字段访问给出 (偏移, 名字)。完整数据见 db/fieldnames.json，说明见 docs/fieldnames.md。")
    A("//")
    A("// 只收「双向」条目（缺省值与结果都落在同一个偏移上），以及没被人争过的单向条目。")
    A("// 单向 + 有争议 = 证据不够，一律不进本表。")
    A("")
    A("#pragma once")
    A("")
    A("#include <cstdint>")
    A("#include <cstring>")
    A("")
    A("namespace ra2 {")
    A("namespace re {")
    A("")
    A("struct FieldName {")
    A("    uint32_t off;")
    A("    const char* key;   // INI 键名（原样，大小写以文件为准）")
    A("    uint8_t width;     // 字段宽度，字节")
    A("    const char* type;  // int / bool / ?（? = 读取入口不在已认出的两个里）")
    A("};")
    A("")
    A("struct ClassFieldNames {")
    A("    const char* cls;")
    A("    uint32_t count;")
    A("    const FieldName* fields;")
    A("};")
    A("")
    n_tot = n_skip = n_kept_conf = 0
    keep: dict[str, list] = {}
    for cls in sorted(data):
        items = []
        for o, r in sorted(data[cls].items()):
            if not isinstance(o, int):
                continue
            has_conf = bool(r.get("conflict"))
            if has_conf and r["level"] != "双向":
                n_skip += 1
                continue
            if has_conf:
                n_kept_conf += 1
            items.append((o, r))
        n_tot += len(items)
        if items:                      # 空数组在 C++ 里是非法声明，整类跳过
            keep[cls] = items
    for cls in sorted(keep):
        safe = re.sub(r"[^A-Za-z0-9_]", "_", cls)
        A("inline constexpr FieldName kFN_%s[] = {" % safe)
        for off, r in keep[cls]:
            A('    {0x%X, "%s", %d, "%s"},' % (off, r["key"], r["width"], r["type"]))
        A("};")
    A("")
    A("inline constexpr ClassFieldNames kFieldNames[] = {")
    for cls in sorted(keep):
        safe = re.sub(r"[^A-Za-z0-9_]", "_", cls)
        A('    {"%s", %u, kFN_%s},' % (cls, len(keep[cls]), safe))
    A("};")
    A("")
    A("inline constexpr uint32_t kFieldNameClassCount =")
    A("    sizeof(kFieldNames) / sizeof(kFieldNames[0]);")
    A("")
    A("/// 查 (类, 偏移) 的字段名 —— 也就是这个偏移是从哪个 INI 键读出来的。找不到返回 nullptr。")
    A("inline const char* FieldNameOf(const char* cls, uint32_t off) {")
    A("    if (cls == nullptr) return nullptr;")
    A("    for (uint32_t i = 0; i < kFieldNameClassCount; ++i) {")
    A("        if (kFieldNames[i].cls == nullptr || std::strcmp(kFieldNames[i].cls, cls) != 0)")
    A("            continue;")
    A("        for (uint32_t j = 0; j < kFieldNames[i].count; ++j)")
    A("            if (kFieldNames[i].fields[j].off == off) return kFieldNames[i].fields[j].key;")
    A("        return nullptr;")
    A("    }")
    A("    return nullptr;")
    A("}")
    A("")
    A("}  // namespace re")
    A("}  // namespace ra2")
    A("")
    open(path, "w", encoding="utf-8").write("\n".join(L))
    return n_tot, n_skip, n_kept_conf


# ----------------------------------------------------------------------- 主


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--top", type=int, default=0)
    ap.add_argument("--keys", default=os.path.join(ROOT, "build", "_inikeys.txt"))
    a = ap.parse_args()

    keys_rules, keys_art = load_key_set(a.keys)
    keys = keys_rules | keys_art

    img = PEImage(DEFAULT_IMAGE)
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True
    loc = Locator(img)

    rtti = json.load(open(os.path.join(DB, "rtti.json"), encoding="utf-8"))
    funcs = json.load(open(os.path.join(DB, "functions.json"), encoding="utf-8"))
    fields_raw = json.load(open(os.path.join(DB, "fields.json"), encoding="utf-8"))
    depth = FS.class_depth(os.path.join(DB, "rtti.json"))

    res = scan_readini(img, rtti, funcs, keys, md, loc, depth)
    res["keys_rules"] = keys_rules
    res["keys_art"] = keys_art

    print("真实的 INI 键：rules %d，art %d" % (len(keys_rules), len(keys_art)))
    print("认作 Read_INI 的函数：%d 个（引用 >=%d 个真实键）"
          % (len(res["readinis"]), MIN_KEYS))
    if res["slot_hist"]:
        print("  虚表槽号分布：" + "，".join(
            "槽 #%d %d 次" % (i, c) for i, c in
            sorted(res["slot_hist"].items(), key=lambda kv: -kv[1])[:6]))
    if a.top:
        for va, n, ks in sorted(res["readinis"], key=lambda t: -t[1])[:a.top]:
            print("    0x%08X %3d 键  %s" % (va, n, sorted(ks)[:5]))
    for va, why in res["diag"]:
        print("  未归属：0x%08X %s" % (va, why))

    data = res["data"]
    sizes = json.load(open(os.path.join(DB, "sizes.json"),
                           encoding="utf-8")).get("classes", {})
    ver = verify(data, keys_rules, keys_art, fields_raw.get("classes", {}), sizes)

    print()
    print("抽出字段名 %d 条，覆盖 %d 个类" % (ver["total"], len(data)))
    if ver["total"]:
        print("  双向（缺省值与结果同偏移）：%d（%.0f%%）"
              % (ver["both"], 100.0 * ver["both"] / ver["total"]))
    print("  键不在真实键集里：%d（按输入端的筛子，恒为 0）" % ver["bad_key"])
    print("  宽度与构造函数扫描的字段宽度不符：%d（另 %d 条被构造函数扫描独立看到）"
          % (ver["bad_width"], ver["cross_seen"]))
    print("  偏移超出该类 sizeof：%d" % ver["beyond"])
    print("  Read_INI 独有（构造函数扫描没碰过）：%d" % ver["new_field"])

    print()
    print("覆盖面（每条 Read_INI 里出现的键 -> 配上偏移的键）：")
    for c in sorted(res["coverage"], key=lambda d: -d["keys_seen"]):
        print("  %-24s 键 %3d  配上 %3d  没配上 %3d"
              % (c["cls"], c["keys_seen"], c["paired"],
                 len(c["unpaired"])))
    print("  被判为两侧不一致而弃用：%d 条" % len(res["conflicts"]))

    with open(os.path.join(DB, "fieldnames.json"), "w", encoding="utf-8") as f:
        json.dump({"method": "从 Read_INI 的『键名两侧同偏移』配对",
                   "min_keys": MIN_KEYS,
                   "slot_hist": {str(k): v for k, v in res["slot_hist"].items()},
                   "verify": {k: ver[k] for k in
                              ("total", "both", "bad_key", "bad_width",
                               "cross_seen", "beyond", "new_field")},
                   "coverage": res["coverage"],
                   "conflicts": res["conflicts"],
                   "classes": {c: {str(o): r for o, r in offs.items()}
                               for c, offs in sorted(data.items())}},
                  f, ensure_ascii=False, indent=1)

    write_docs(res, ver, os.path.join(DOCS, "fieldnames.md"))
    n_tot, n_skip, n_kept = write_header(data,
                                         os.path.join(ROOT, "src", "re", "FieldNames.h"))
    print()
    print("已写：db/fieldnames.json  docs/fieldnames.md  src/re/FieldNames.h")
    print("  C++ 常量表收 %d 条（其中 %d 条有人争但本身是「双向」证据，保留）；"
          "%d 条单向且有争议，只留在文档里" % (n_tot, n_kept, n_skip))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
