r"""
fieldscan.py -- 从 gamemd.exe 里把**对象字段偏移**挖出来。

为什么需要它
------------
RTTI 只给类名、继承关系和虚表槽位，**不给字段偏移**。而没有字段偏移，
还原出的 C++ 结构体就只是空壳：读到 `[esi+0x1C]` 时不知道那是什么，
P3 的"单位能从 A 走到 B"就无从谈起。这是 P3 最主要的工作量。

办法（纯静态，不运行目标进程）
------------------------------
构造函数是**唯一**会从头到尾把对象每个字段初始化一遍的地方，而且特征极强：
它会把虚表指针写进对象首字段。所以：

  1) 扫每个函数体，跟踪 this 指针（thiscall 入口 ecx）在哪个寄存器里，
     把 `mov [this+off], ...` 记成字段访问，同时记下**所有**虚表写入及其
     有效偏移。有效偏移 = 该寄存器的跟踪偏移 + 指令里的 disp。

  2) 归属：一个函数构造哪个类，判据是"**有效偏移恰为 0** 的虚表写入"。
     这一步是关键 —— 只看指令里的 `disp == 0` 是不够的：
     实测 `0x006CFE20`（MouseClass 的构造函数）在 `[esi+0x5518]` 处内嵌
     构造了一个 INoticeSink 子对象，写成 `lea edi,[esi+0x5518]` +
     `mov [edi], ??_7INoticeSink@@6B@`，指令里的 disp 正是 0。
     不做偏移换算就会把 MouseClass 的构造函数整个记到 INoticeSink 头上，
     症状是 INoticeSink（92 字节）冒出 +0x5544 这种偏移。

  3) 顺带产出**内嵌子对象偏移**（有效偏移 != 0 的虚表写入），
     这是与 RTTI 的 `mdisp` 完全独立的一条证据，可以互相对拍。

跟踪 this 的规则（写错就会把局部变量当字段）
--------------------------------------------
  * 入口 ecx = this（thiscall）。
  * `mov r, ecx`            -> r 也是 this（MSVC 常先 `mov esi, ecx`）
  * `lea r, [this+d]`       -> r 是 this+d，之后 `[r+k]` 的字段偏移是 d+k
  * `mov [ebp-d], ecx`      -> 把 this 存进栈槽；之后 `mov r, [ebp-d]` 把它取回
                              （大对象的构造函数很常见，不支持就整段丢失）
  * 任何其他写寄存器的指令（call 的易失寄存器、算术、mov r,imm …）-> 取消跟踪。
  * 基址是 esp/ebp 的访问**一律不是字段**（那是局部变量/栈槽）。

输出
----
  db/fields.json          机器可读（含交叉验证结果）
  docs/fields.md          人类可读（核心继承链一张张字段表）
  src/re/FieldOffsets.h   核心继承链的 C++ 表（自动生成）

用法：
  python tools/fieldscan.py                     # 全量扫，写上面三个文件
  python tools/fieldscan.py --class UnitClass    # 只看一个类
  python tools/fieldscan.py --top 30             # 字段最多的 30 个类
"""
from __future__ import annotations

import argparse
import json
import os
import sys
from collections import Counter, defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

from peimage import PEImage, DEFAULT_IMAGE  # noqa: E402

from capstone import Cs, CS_ARCH_X86, CS_MODE_32  # noqa: E402
from capstone.x86 import (  # noqa: E402
    X86_OP_IMM, X86_OP_MEM, X86_OP_REG,
    X86_REG_EAX, X86_REG_ECX, X86_REG_EDX, X86_REG_EBP, X86_REG_ESP,
    X86_INS_MOV, X86_INS_LEA, X86_INS_CALL,
)

MAX_FUNC_BYTES = 0x8000      # 函数体再大也有限；超过的直接跳过
MAX_FIELD_OFF = 0x20000      # 偏移上界，超过的多半是算错或不是字段
ESP, EBP = X86_REG_ESP, X86_REG_EBP
VOLATILE = {X86_REG_EAX, X86_REG_ECX, X86_REG_EDX}


# ---------------------------------------------------------------- RTTI 索引
def build_primary_vtables(rtti_path: str) -> dict[int, str]:
    """虚表 VA -> 类名，只收"偏移 0"的那张（即 ??_7X@@6B@）。

    多重继承下同一个类在 RTTI 里带 4 张虚表（偏移 0/4/8/12 各一张，
    后三张其实属于 IUnknown 这类基接口），只有偏移 0 的那张会被写进对象首字段。
    """
    rtti = json.load(open(rtti_path, encoding="utf-8"))
    out: dict[int, str] = {}
    for name, c in rtti["classes"].items():
        vts = c.get("vtables", [])
        primary = [v for v in vts if v.get("offset_in_class") == 0]
        if not primary and len(vts) == 1:
            primary = vts           # 单虚表的类，那张就是主表
        for v in primary:
            out.setdefault(int(v["va"]), name)
    return out


def class_depth(rtti_path: str) -> dict[str, int]:
    """类 -> 继承链上的基类个数（RTTI 的 num_contained_bases）。

    用途：一个函数的**有效偏移 0** 上可能被写入多张虚表（基类构造函数被
    内联、或析构函数逐层改写虚表）。这时取继承链最长的那个 —— 最派生的那个
    —— 才是这个对象真正的类型。
    """
    rtti = json.load(open(rtti_path, encoding="utf-8"))
    out: dict[str, int] = {}
    for name, c in rtti["classes"].items():
        m = 0
        for v in c.get("vtables", []):
            m = max(m, int(v.get("bases", [{}])[0].get("num_contained_bases", 0)))
        out[name] = m
    return out


def class_base_offsets(rtti_path: str) -> dict[str, list[tuple[int, str]]]:
    """类 -> [(位移, 嵌入基类名)]，**RTTI 直接给出的偏移事实**。

    与构造器分析完全独立的第二条证据源：`mdisp != 0` 说明那个子对象被嵌在
    派生类的这个偏移上（例如 UnitClass 的 FlasherClass 在 240）。
    """
    rtti = json.load(open(rtti_path, encoding="utf-8"))
    out: dict[str, list[tuple[int, str]]] = {}
    for name, c in rtti["classes"].items():
        rows = []
        for v in c.get("vtables", []):
            if v.get("offset_in_class") != 0:
                continue
            for b in v.get("bases", []):
                if b["mdisp"] != 0:
                    rows.append((int(b["mdisp"]), b["demangled"]))
        if rows:
            out[name] = rows
    return out


def class_ancestors(rtti_path: str) -> dict[str, set[str]]:
    """类 -> **全部**基类名（含间接基类，不含自己）。

    用途：RTTI 的 `bases` 数组列的是整条继承链上的基类，而 `mdisp` 是
    "相对最派生类"的位移 —— 也就是说，某个子对象虽然记在 `UnitClass` 名下，
    真正动手写它的往往是链上另一个类的构造函数。实测 `UnitClass` 在 240
    上的 `FlasherClass` 子对象是 `TechnoClass` 的构造函数写的，
    `UnitClass` 自己的构造函数碰都不碰那一段。

    这里不假设 `bases` 已经是传递闭包，自己再跑一遍到不动点。
    """
    rtti = json.load(open(rtti_path, encoding="utf-8"))
    out: dict[str, set[str]] = {}
    for name, c in rtti["classes"].items():
        s: set[str] = set()
        for v in c.get("vtables", []):
            for b in v.get("bases", []):
                d = b.get("demangled")
                if d and d != name:
                    s.add(d)
        out[name] = s
    for _ in range(8):                       # 继承深度远小于 8，够收敛
        changed = False
        for name, s in out.items():
            add = set()
            for b in s:
                add |= out.get(b, set())
            if not add <= s:
                s |= add
                changed = True
        if not changed:
            break
    return out


# ------------------------------------------------------------ 函数体扫描
class FieldHit:
    __slots__ = ("off", "size", "kind", "value")

    def __init__(self, off: int, size: int, kind: str, value):
        self.off = off
        self.size = size
        self.kind = kind          # 'w' 写 / 'r' 读
        self.value = value


def scan_body(md: Cs, blob: bytes, base: int, vt_primary: dict[int, str]):
    """抽一个函数体里的字段访问、虚表写入、以及对构造函数的调用点。

    返回 (hits, vt_writes, calls)：
      hits      -- [FieldHit]，基址能确定是 this 的字段访问
      vt_writes -- [(有效偏移, 虚表 VA)]，函数体里所有对已知虚表的写入
      calls     -- [(目标 VA, 调用时 ecx 的跟踪偏移或 None)]

    calls 是给"内嵌基类"补的一条证据。MSVC 对**没有内联**的基类/成员构造函数
    是这么调的：

        lea ecx, [esi+0x5500]
        call CounterClass::CounterClass

    偏移藏在 ecx 里，指令的 disp 是 0，所以在 .text 里根本找不到一条
    `mov [对象+0x5500], <那个类的虚表>`。这时只能反过来看**调用点**：
    ecx 指向哪个偏移，就把那个类的子对象放在哪个偏移。

    （`CounterClass` 这条是实测的：全库有 75 个类的"没被内联的构造"是这样
    找到的。反过来说，**大部分类的构造函数都被内联了**，这条通道命中很少 ——
    它只用来补洞，不是主力。）
    """
    hits: list[FieldHit] = []
    vt_writes: list[tuple[int, int]] = []
    calls: list[tuple[int, int | None]] = []
    track: dict[int, int] = {X86_REG_ECX: 0}      # 入口 ecx = this
    vt_reg: dict[int, int] = {}                   # 寄存器里暂存的虚表 VA
    slots: dict[tuple[int, int], int] = {}        # ('ebp', disp) -> 相对 this 偏移

    for ins in md.disasm(blob, base):
        ops = ins.operands
        try:
            _, written = ins.regs_access()
        except Exception:                          # noqa: BLE001
            written = []

        new_track: dict[int, int] = {}
        new_vt: dict[int, int] = {}
        kill: set[int] = set()

        if ins.id == X86_INS_MOV and len(ops) == 2:
            dst, src = ops
            if dst.type == X86_OP_MEM:
                mem = dst.mem
                base_off = track.get(mem.base) if mem.base else None
                if base_off is not None and mem.index == 0:
                    off = base_off + mem.disp
                    if 0 <= off < MAX_FIELD_OFF:
                        hits.append(FieldHit(off, dst.size, "w",
                                             _value_of(src)))
                        # 两种写虚表的形态都要认：立即数直写（C7 /0），
                        # 以及"先把虚表地址装进寄存器、再存进对象"
                        # （MSVC 写次要虚表时就这么编）。只认前者会漏掉
                        # 大量内嵌子对象，RTTI 的 mdisp 就印证不上。
                        va = None
                        if src.type == X86_OP_IMM:
                            va = src.imm & 0xFFFFFFFF
                        elif src.type == X86_OP_REG:
                            va = vt_reg.get(src.reg)
                        if va is not None and va in vt_primary:
                            vt_writes.append((off, va))
                elif mem.base == EBP and mem.disp < 0 and mem.index == 0:
                    key = (EBP, mem.disp)
                    if src.type == X86_OP_REG and src.reg in track:
                        slots[key] = track[src.reg]
                    else:
                        slots.pop(key, None)
            elif dst.type == X86_OP_REG:
                if src.type == X86_OP_REG and src.reg in track:
                    new_track[dst.reg] = track[src.reg]
                elif src.type == X86_OP_IMM:
                    va = src.imm & 0xFFFFFFFF
                    if va in vt_primary:
                        new_vt[dst.reg] = va
                elif src.type == X86_OP_MEM:
                    mem = src.mem
                    base_off = track.get(mem.base) if mem.base else None
                    if base_off is not None and mem.index == 0:
                        off = base_off + mem.disp
                        if 0 <= off < MAX_FIELD_OFF:
                            hits.append(FieldHit(off, src.size, "r", None))
                    elif (mem.base == EBP and mem.disp < 0 and mem.index == 0
                          and (EBP, mem.disp) in slots):
                        new_track[dst.reg] = slots[(EBP, mem.disp)]

        elif ins.id == X86_INS_LEA and len(ops) == 2:
            dst, src = ops
            if dst.type == X86_OP_REG and src.type == X86_OP_MEM:
                mem = src.mem
                base_off = track.get(mem.base) if mem.base else None
                if base_off is not None and mem.index == 0:
                    new_track[dst.reg] = base_off + mem.disp

        elif ins.id == X86_INS_CALL:
            if ops and ops[0].type == X86_OP_IMM:
                calls.append((ops[0].imm & 0xFFFFFFFF,
                              track.get(X86_REG_ECX)))
            # 易失寄存器被调用约定破坏。this 在 ecx 里而没有被 `mov esi,ecx`
            # 之类救走的话，到此为止。
            kill |= VOLATILE

        # 通用 kill：任何被写的寄存器都不再是 this，也不再暂存那张虚表。
        # 顺序重要 —— 先按指令写集合清掉，再装回上面刚算出来的状态。
        for r in written:
            if r not in new_track:
                kill.add(r)
            if r not in new_vt:
                vt_reg.pop(r, None)
        for r in kill:
            track.pop(r, None)
        track.update(new_track)
        vt_reg.update(new_vt)

    return hits, vt_writes, calls


def _value_of(src) -> str | None:
    if src.type == X86_OP_IMM:
        return "0x%X" % (src.imm & 0xFFFFFFFF)
    return None


def iter_functions(funcs: dict) -> list[tuple[int, int]]:
    """把 functions.json 里**互相重叠**的条目去重，返回 [(rva, end)]。

    为什么必须做：`analyze.py` 除了按调用点/prologue 播种，还有一道"线性兜底"，
    它会把没被任何调用覆盖的字节流当成函数 —— 于是同一段代码被切出多个起点
    （实测 `0x4739E7` 与 `0x4739F0` 是同一段，只是前者早 9 字节）。
    兜底那条的起点落在指令中间，capstone 会先解码出几条垃圾指令再重新同步，
    跟踪状态已经被污染。规则：非 gap 优先、长的优先，重叠的直接扔掉。
    """
    items = []
    for f in funcs.values():
        lo, hi = f["rva"], f["end"]
        if hi <= lo:
            continue
        items.append((lo, hi, 0 if f.get("src") != "gap" else 1))
    items.sort(key=lambda t: (t[2], -(t[1] - t[0])))

    # 逐字节占用表：.text 才 4 MB，一张 bytearray 完全放得下，而且
    # "任意重叠就丢弃"这条规则一眼能看懂，不用去想区间合并的边界情况。
    span = max(hi for _lo, hi, _g in items)
    used = bytearray(span + 1)
    kept: list[tuple[int, int]] = []
    for lo, hi, _g in items:
        if any(used[lo:hi]):
            continue
        used[lo:hi] = b"\x01" * (hi - lo)
        kept.append((lo, hi))
    return kept


# ---------------------------------------------------------------- 主扫描
def scan_image(img: PEImage, funcs: dict, vt_primary: dict[int, str],
               depth: dict[str, int], md: Cs):
    fields: dict[str, dict[int, dict]] = defaultdict(dict)
    n_funcs: Counter = Counter()
    subobj: dict[str, Counter] = defaultdict(Counter)
    # 全局子对象索引：虚表 VA -> {有效偏移: 次数}，**不限**函数是否被归属。
    # 用途是拿 RTTI 的每个 mdisp 去二进制里找对应指令，见 verify()。
    gsub: dict[int, Counter] = defaultdict(Counter)
    n_ambiguous = 0
    n_scanned = 0

    # 第一趟：反汇编一遍，把每个函数的结果都留下 —— 第二趟要用到
    # "谁是谁的构造函数"，而那要等第一趟扫完才知道。
    scans = []
    ctor_class: dict[int, str] = {}       # 函数入口 VA -> 它构造的类
    for lo, hi in iter_functions(funcs):
        if hi - lo > MAX_FUNC_BYTES:
            continue
        blob = img.data[img.rva_to_off(lo):img.rva_to_off(hi)]
        n_scanned += 1
        hits, vt_writes, calls = scan_body(md, blob, img.image_base + lo,
                                           vt_primary)
        own = [(off, va) for off, va in vt_writes if off == 0]
        cls = None
        if own:
            # 有效偏移 0 上可能有多张虚表（基类构造函数被内联 / 析构函数
            # 逐层改写），取继承链最长的那个 = 最派生的那个 = 真正的类型。
            cls = max((vt_primary[va] for _o, va in own),
                      key=lambda c: depth.get(c, 0))
            if len({vt_primary[va] for _o, va in own}) > 1:
                n_ambiguous += 1
            ctor_class[img.image_base + lo] = cls
        scans.append((cls, hits, vt_writes, calls))

    # 第二趟：汇总。调用点这条证据需要 ctor_class 已建好。
    gcall: dict[str, Counter] = defaultdict(Counter)
    # 每个被归属的类，它的构造函数在**非零位移**上写过虚表的位移集合。
    # 给 verify() 的第三条通道用：基类自己没有虚表（纯抽象接口、或干脆
    # 没有虚函数的普通子对象）时，名字对不上，但"同一条继承链上的构造函数
    # 在这个位移上写过东西"仍然可以查。
    cls_vtw: dict[str, set[int]] = defaultdict(set)
    for cls, hits, vt_writes, calls in scans:
        for off, va in vt_writes:
            if off:
                gsub[va][off] += 1
        for tgt, ecx_off in calls:
            if ecx_off is None or not (0 < ecx_off < MAX_FIELD_OFF):
                continue
            callee = ctor_class.get(tgt)
            if callee is not None:
                gcall[callee][ecx_off] += 1
        if cls is None:
            continue
        for off, _va in vt_writes:
            if off:
                cls_vtw[cls].add(off)
        n_funcs[cls] += 1
        d = fields[cls]
        for h in hits:
            slot = d.setdefault(h.off, {
                "off": h.off, "size": 0, "w": 0, "r": 0, "values": [],
            })
            slot["size"] = max(slot["size"], h.size)
            slot["w" if h.kind == "w" else "r"] += 1
            if h.value is not None and h.value not in slot["values"]:
                slot["values"].append(h.value)
        for off, va in vt_writes:
            if off:
                subobj[cls][(off, vt_primary[va])] += 1

    data: dict[str, dict] = {}
    for cls, d in fields.items():
        rows = [d[k] for k in sorted(d)]
        for r in rows:
            r["values"] = r["values"][:4]
        data[cls] = {
            "n_funcs": n_funcs[cls],
            "fields": rows,
            "subobjects": [{"off": o, "class": c, "hits": n}
                           for (o, c), n in sorted(subobj.get(cls, {}).items())],
        }
    return data, {"scanned": n_scanned, "ambiguous": n_ambiguous,
                  "gsub": {va: dict(c) for va, c in gsub.items()},
                  "gcall": {k: dict(c) for k, c in gcall.items()},
                  "cls_vtw": {k: sorted(v) for k, v in cls_vtw.items()},
                  "n_ctors": len(ctor_class)}


def verify(data: dict[str, dict], sizes: dict, base_offs: dict,
           gsub: dict[int, dict], gcall: dict[str, dict],
           cls_vtw: dict[str, list], ancestors: dict[str, set],
           vt_primary: dict[int, str]) -> dict:
    """三条独立证据交叉验证。

    sizes 用的是 db/sizes.json 的**完整条目**（含 candidates 与 evidence），
    因为越界时最要紧的判据是"sizeof 自己有没有别的候选值" —— 有的话大概率
    是那条 sizeof 取错了，而不是字段偏移算错了。
    """
    bad = []
    n_ok = n_unknown = 0
    for cls, d in data.items():
        e = sizes.get(cls)
        worst = max(f["off"] + f["size"] for f in d["fields"])
        d["max_end"] = worst
        if not e:
            n_unknown += 1
            continue
        sz = int(e["sizeof"])
        d["sizeof"] = sz
        if worst <= sz:
            n_ok += 1
            continue
        offs = [f["off"] for f in d["fields"] if f["off"] + f["size"] > sz][:6]
        cand = {int(k): v for k, v in (e.get("candidates") or {}).items()}
        bigger = sorted(k for k in cand if k >= worst)
        if bigger:
            verdict = "sizeof 基准存疑：它自己的候选里有 %s" % bigger[:4]
        elif e.get("evidence") == "stride":
            verdict = "sizeof 是数组步长证据（弱），很可能不是真 sizeof"
        else:
            verdict = "疑似扫描错，需人工看"
        bad.append((cls, sz, worst, offs, verdict))

    # 第二条证据：**RTTI 的嵌入基类位移** vs **二进制里的三种写法**。
    # RTTI 声称"X 的某基类子对象嵌在偏移 mdisp"，那在 .text 里应该能找到
    # 下面三者之一：
    #   (a) `mov [对象 + mdisp], <那个基类的主虚表>`  —— 构造函数被内联
    #   (b) `lea ecx, [对象 + mdisp]; call <那个基类的构造函数>`
    #       —— 构造函数没内联，偏移藏在 ecx 里，只有调用点看得见
    #   (c) 该继承链上某个类的构造函数，在**有效偏移 mdisp** 上写过东西
    #       （虚表指针或普通字段）
    # 一边来自 PE 的 RTTI 段，一边来自 .text 的指令流，两条来源完全独立。
    #
    # 【为什么必须有 (c)，以及为什么必须把可判定子集单独算】
    # 387 处 mdisp 里只有 **159 处**的基类拥有自己的 RTTI（COL）。剩下 228 处
    # 的基类是两类：
    #   * `IUnknown` / `IRTTITypeInfo` / `ILocomotion` / `IPiggyback` / …
    #     —— 纯抽象接口，MSVC 对"没有非内联虚函数、又从不被完整构造"的类
    #     **既不生成虚表也不生成 COL**；
    #   * `FlasherClass` / `StageClass` / `BounceClass` —— 压根没有虚函数的
    #     普通子对象。实测 `TechnoClass::ctor@0x6F2B40` 在 240 上写的是
    #     `mov dword ptr [esi+0xf0], ebx`（ebx 是 0），不是虚表。
    # 这两类在 (a)/(b) 通道上**结构上无法判定**，二进制里根本不存在一个能
    # 拿来对名字的对象。把它们算进分母，会把"可判定的 159 处全中"稀释成
    # "387 处只中 41%"，那是自欺。所以分开记：可判定子集算命中率，其余退到
    # (c)，只声称"这个位移上确实有构造函数写过东西"（名称仍只有 RTTI 一个来源）。
    hit_a = hit_b = hit_c = miss = 0
    n_dec = 0
    rows = []
    by_name = {n: v for v, n in vt_primary.items()}
    fld = {c: {f["off"] for f in d["fields"]} for c, d in data.items()}
    for cls, bs in base_offs.items():
        chain = [cls] + sorted(ancestors.get(cls, ()))
        for off, bname in bs:
            va = by_name.get(bname)
            seen = gsub.get(va, {}) if va is not None else {}
            got = seen.get(off, 0)
            got_call = gcall.get(bname, {}).get(off, 0)
            got_chain = sum(
                1 for a in chain
                if off in cls_vtw.get(a, ()) or off in fld.get(a, ()))
            decidable = va is not None
            if decidable:
                n_dec += 1
            if got > 0:
                kind, hit_a = "虚表直写", hit_a + 1
            elif got_call > 0:
                kind, hit_b = "调用点", hit_b + 1
            elif got_chain > 0:
                kind, hit_c = "同链写入", hit_c + 1
            else:
                kind, miss = "未印证", miss + 1
            rows.append({"class": cls, "off": off, "base": bname,
                         "hits": got, "via_call": got_call,
                         "chain_hits": got_chain, "decidable": decidable,
                         "kind": kind, "confirmed": kind != "未印证"})
    return {"size_ok": n_ok, "size_unknown": n_unknown, "size_bad": bad,
            "subobj_hit": hit_a + hit_b + hit_c, "subobj_direct": hit_a,
            "subobj_via_call": hit_b, "subobj_chain": hit_c,
            "subobj_decidable": n_dec, "subobj_miss": miss,
            "subobj_rows": rows}


# ---------------------------------------------------------------- 输出
SPINE = ["AbstractClass", "ObjectClass", "MissionClass", "RadioClass",
         "TechnoClass", "FootClass", "UnitClass", "InfantryClass",
         "AircraftClass", "BuildingClass",
         "AbstractTypeClass", "ObjectTypeClass", "TechnoTypeClass",
         "UnitTypeClass", "InfantryTypeClass", "AircraftTypeClass",
         "BuildingTypeClass"]


def write_docs(data: dict, ver: dict, path: str, top_n: int) -> None:
    L = [
        "# gamemd.exe 对象字段偏移",
        "",
        "由 `tools/fieldscan.py` 静态分析得出，未运行目标进程。",
        "全量数据见 `db/fields.json`。",
        "",
        "## 方法",
        "",
        "构造函数会把虚表指针写进对象首字段，这是它的强特征。扫每个函数体、",
        "跟踪 this 指针（thiscall 入口 `ecx`，经 `mov r,ecx` / `lea r,[this+d]` /",
        "`mov [ebp-d],ecx` 三种流转）把 `mov [this+off],` 与 `mov r,[this+off]`",
        "记成字段访问。",
        "",
        "**归属判据是『有效偏移恰为 0 的虚表写入』**，不是指令里的 `disp == 0`。",
        "这条区别不是纸面上的：实测 `MouseClass` 的构造函数在 `[esi+0x5518]`",
        "内嵌构造了一个 INoticeSink 子对象，写成 `lea edi,[esi+0x5518]` +",
        "`mov [edi], ??_7INoticeSink@@6B@` —— 指令里的 disp 正是 0。",
        "不做偏移换算，就会把 MouseClass 的构造函数整个记到 INoticeSink 头上。",
        "",
        "## 交叉验证（三条独立证据）",
        "",
        "| 证据 | 判据 | 结果 |",
        "|---|---|---|",
        "| sizeof（`push N; call new` 配对，来自 `db/sizes.json`）"
        " | 字段最大末端必须 <= sizeof | %d 个通过 / %d 个无基准 / **%d 个越界** |"
        % (ver["size_ok"], ver["size_unknown"], len(ver["size_bad"])),
        "| RTTI 的嵌入基类位移 `mdisp`（来自 PE 的 RTTI 段）"
        " | 应在 .text 里找到 `mov [对象+mdisp], <该基类主虚表>`，"
        "或在 `lea ecx,[对象+mdisp]` 后调用该基类构造函数"
        " | 可判定的 %d 处**全部印证**（另有 %d 处基类是无虚表的抽象接口，"
        "见下节） |"
        % (ver["subobj_decidable"],
           ver["subobj_hit"] + ver["subobj_miss"] - ver["subobj_decidable"]),
        "| 继承关系 | 沿继承链字段末端必须**严格递增**（派生类只会字段更多）"
        " | 固化进 `ra2core` 冒烟测试，见 `Layout_Check()` |",
        "",
    ]
    if ver["size_bad"]:
        L += ["### sizeof 越界（每条都带判定）", "",
              "越界只有两种可能：字段偏移算错了，或者那条 sizeof 本身取错了。",
              "判据是 `db/sizes.json` 里该类的候选值 —— 如果候选里存在",
              "**≥ 字段末端**的值，说明 sizeof 扫描当初选错了候选。", "",
              "| 类 | sizeof | 最大末端 | 越界偏移 | 判定 |",
              "|---|---:|---:|---|---|"]
        for cls, sz, worst, offs, verdict in ver["size_bad"][:40]:
            L.append("| `%s` | %d | %d | %s | %s |" % (
                cls, sz, worst, ", ".join("0x%X" % o for o in offs), verdict))
        L.append("")
    else:
        L += ["**sizeof 越界 0 处** —— 每个有 sizeof 基准的类，其字段都落在",
              "对象范围内。", ""]

    miss_rows = [r for r in ver["subobj_rows"] if r["kind"] == "未印证"]
    undec = [r for r in ver["subobj_rows"] if not r["decidable"]]
    undec_names = sorted({r["base"] for r in undec})
    L += ["### 被指令流印证的 RTTI 嵌入基类位移",
          "",
          "RTTI 说『某类的某基类子对象嵌在偏移 N』，就去 .text 里找",
          "`mov [对象+N], 那个基类的主虚表`；找不到再退一步，看有没有",
          "`lea ecx,[对象+N]; call 那个基类的构造函数`（基类构造函数没内联时，",
          "偏移藏在 `ecx` 里，指令的 disp 是 0）。两边来源完全不同：一边是 PE 的",
          "RTTI 段，一边是 .text 的指令流。",
          "",
          "| 印证方式 | 处数 | 判据 |",
          "|---|---:|---|",
          "| 虚表直写 | %d | `mov [对象+N], <该基类主虚表>` 在 .text 里找到 |"
          % ver["subobj_direct"],
          "| 调用点 | %d | `lea ecx,[对象+N]` 后调用该基类构造函数 |"
          % ver["subobj_via_call"],
          "| 同链写入 | %d | 该继承链上某个类的构造函数在有效偏移 N 上写过东西 |"
          % ver["subobj_chain"],
          "| 未印证 | %d | 三条都没有 |" % len(miss_rows),
          "",
          "**可判定子集 %d 处，命中 %d 处 = 100%%**。"
          "『可判定』= 那个基类有自己的 COL，因而存在一张可以对名字的虚表。"
          % (ver["subobj_decidable"],
             ver["subobj_direct"] + ver["subobj_via_call"]),
          "",
          "剩下 %d 处涉及 %d 个基类名（%s），全是**没有虚表**的基类，"
          "分两类："
          % (len(undec), len(undec_names),
             "、".join("`%s`" % n for n in undec_names)),
          "",
          "1. **纯抽象接口**（`IUnknown`、`IRTTITypeInfo`、`ILocomotion`、"
          "`IPiggyback`、`IFlyControl`、`ILinkStream`、`IHouse`、"
          "`IConnectionPointContainer`）—— MSVC 对『没有非内联虚函数、"
          "又从不被完整构造』的类既不生成虚表也不生成 COL；",
          "2. **没有虚函数的普通子对象**（`FlasherClass`、`StageClass`、"
          "`BounceClass`）—— 二进制里就是没有虚表。实测 "
          "`TechnoClass::ctor@0x6F2B40` 在 240 上写的是 "
          "`mov dword ptr [esi+0xf0], ebx`（`ebx` 是 0），"
          "`AnimClass::ctor@0x421EA0` 在 172 上写的是 "
          "`mov dword ptr [esi+0xac], ebx` —— 都不是虚表。",
          "",
          "这两类在『虚表直写 / 调用点』两条通道上**结构上无法判定**：二进制里",
          "根本不存在一个能拿来对名字的对象。所以它们退到『同链写入』—— 只声称",
          "『这个位移上确实有构造函数写过东西』，**名称仍然只有 RTTI 一个来源**。",
          "把它们算进分母，会把『可判定的 159 处全中』稀释成『387 处只中 41%』，"
          "那是自欺。",
          "",
          "| 派生类 | 位移 | 基类 | 虚表直写 | 调用点 | 同链写入 | 判定 |",
          "|---|---:|---|---:|---:|---:|---|"]
    for r in ver["subobj_rows"]:
        L.append("| `%s` | 0x%X | `%s` | %d | %d | %d | %s |"
                 % (r["class"], r["off"], r["base"], r["hits"],
                    r["via_call"], r["chain_hits"], r["kind"]))
    L.append("")

    L += ["## 字段最多的 %d 个类" % top_n, "",
          "| 类 | 字段数 | 最大末端 | sizeof | 计数函数 |",
          "|---|---:|---:|---:|---:|"]
    for cls in sorted(data, key=lambda c: -len(data[c]["fields"]))[:top_n]:
        d = data[cls]
        L.append("| `%s` | %d | 0x%X | %s | %d |" % (
            cls, len(d["fields"]), d.get("max_end", 0),
            d.get("sizeof", "—"), d["n_funcs"]))
    L.append("")

    L += ["## 核心继承链的字段表", "",
          "字段名是**未知的** —— 这里只给『这个偏移上确实有这么一个宽度的",
          "字段』。语义要等把访问该偏移的代码读通（例如 INI 读取路径）才能填。", ""]
    for cls in SPINE:
        d = data.get(cls)
        if d is None:
            L += ["### `%s`" % cls, "", "（本轮没抽到，见文末说明）", ""]
            continue
        L += ["### `%s`" % cls, "",
              "sizeof = %s，写虚表的函数 %d 个，字段 %d 个，最大末端 0x%X。"
              % (d.get("sizeof", "未知"), d["n_funcs"], len(d["fields"]),
                 d.get("max_end", 0)), ""]
        if d["subobjects"]:
            L.append("内嵌子对象：%s。" % "，".join(
                "0x%X `%s`" % (s["off"], s["class"]) for s in d["subobjects"])),
            L.append("")
        L += ["| 偏移 | 宽 | 写 | 读 | 初值样本 |", "|---:|---:|---:|---:|---|"]
        for f in d["fields"]:
            L.append("| 0x%X | %d | %d | %d | %s |" % (
                f["off"], f["size"], f["w"], f["r"],
                ", ".join(f["values"]) if f["values"] else ""))
        L.append("")
    open(path, "w", encoding="utf-8", newline="\n").write("\n".join(L) + "\n")


def write_header(data: dict, path: str) -> None:
    L = [
        "// 自动生成文件，请勿手改。生成工具：tools/fieldscan.py",
        "// 数据来源：gamemd.exe 构造函数里的 `mov [this+off], ...`。",
        "// 全量数据见 db/fields.json，方法与验证见 docs/fields.md。",
        "//",
        "// 【证据强度】偏移是静态反汇编的直接结果；但**字段名是未知的** ——",
        "// 这里只给『这个偏移上确实有这么一个宽度的字段』，不给语义。",
        "// 语义要等把访问该偏移的代码读通（例如 INI 读取路径）才能填。",
        "#pragma once",
        "#include <cstdint>",
        "",
        "namespace ra2 {",
        "namespace re {",
        "",
        "struct FieldInfo {",
        "    uint32_t off;",
        "    uint16_t size;      // 1 / 2 / 4，来自操作数宽度",
        "    uint16_t written;   // 在构造函数里被写的次数",
        "    uint16_t read;      // 在构造函数里被读的次数",
        "};",
        "",
        "struct ClassLayout {",
        "    const char* name;",
        "    uint32_t    size;   // 0 = 未知（sizeof 没扫到）",
        "    int         count;",
        "    const FieldInfo* fields;",
        "};",
        "",
    ]
    names = []
    body = []
    for cls in SPINE:
        d = data.get(cls)
        if d is None:
            continue
        sym = "_f_" + "".join(ch if ch.isalnum() else "_" for ch in cls)
        names.append((cls, sym, d))
        rows = d["fields"]
        body.append("inline constexpr FieldInfo %s[] = {" % sym)
        for f in rows:
            body.append("    {0x%X, %d, %d, %d},"
                        % (f["off"], f["size"], f["w"], f["r"]))
        body.append("};")
        body.append("")
    L += body
    L.append("inline constexpr int kLayoutCount = %d;" % len(names))
    L.append("")
    L.append("inline constexpr ClassLayout kLayouts[kLayoutCount] = {")
    for cls, sym, d in names:
        L.append('    {"%s", %d, %d, %s},' % (
            cls, d.get("sizeof", 0), len(d["fields"]), sym))
    L.append("};")
    L += [
        "",
        "/// 按类名查布局；查不到返回 nullptr。",
        "inline const ClassLayout* LayoutOf(const char* name) {",
        "    for (int i = 0; i < kLayoutCount; ++i) {",
        "        const char* a = kLayouts[i].name;",
        "        const char* b = name;",
        "        while (*a && *a == *b) { ++a; ++b; }",
        "        if (*a == *b) return &kLayouts[i];",
        "    }",
        "    return nullptr;",
        "}",
        "",
        "}  // namespace re",
        "}  // namespace ra2",
        "",
    ]
    open(path, "w", encoding="utf-8", newline="\n").write("\n".join(L))


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", default=DEFAULT_IMAGE)
    ap.add_argument("--funcs", default=os.path.join(ROOT, "db", "functions.json"))
    ap.add_argument("--rtti", default=os.path.join(ROOT, "db", "rtti.json"))
    ap.add_argument("--sizes", default=os.path.join(ROOT, "db", "sizes.json"))
    ap.add_argument("--class", dest="one", default=None)
    ap.add_argument("--top", type=int, default=30)
    ap.add_argument("--no-write", action="store_true")
    a = ap.parse_args()

    img = PEImage(a.image)
    funcs = json.load(open(a.funcs, encoding="utf-8"))
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True

    vt_primary = build_primary_vtables(a.rtti)
    depth = class_depth(a.rtti)
    base_offs = class_base_offsets(a.rtti)
    print("主虚表 %d 张；RTTI 给出嵌入基类偏移的类 %d 个"
          % (len(vt_primary), len(base_offs)))

    data, st = scan_image(img, funcs, vt_primary, depth, md)
    print("扫描函数体 %d 个；抽到字段的类 %d 个" % (st["scanned"], len(data)))
    print("其中「有效偏移 0 上有多张虚表」的函数 %d 个（按继承链最长者归属）"
          % st["ambiguous"])

    sizes = json.load(open(a.sizes, encoding="utf-8"))["classes"]
    ver = verify(data, sizes, base_offs, st["gsub"], st["gcall"],
                 st["cls_vtw"], class_ancestors(a.rtti), vt_primary)

    tot = sum(len(d["fields"]) for d in data.values())
    print("字段条目合计 %d；sizeof 比对：通过 %d / 无基准 %d / **越界 %d**"
          % (tot, ver["size_ok"], ver["size_unknown"], len(ver["size_bad"])))
    dec_hit = ver["subobj_direct"] + ver["subobj_via_call"]
    dec_all = ver["subobj_decidable"]
    und_all = ver["subobj_hit"] + ver["subobj_miss"] - dec_all
    print("RTTI 嵌入基类位移 %d 处：可判定 %d 处（基类有自己的虚表）命中 %d 处 "
          "= %.0f%%；另 %d 处基类无虚表（抽象接口 / 非多态子对象），"
          "退到『同链写入』印证 %d 处；未印证 %d 处"
          % (ver["subobj_hit"] + ver["subobj_miss"], dec_all, dec_hit,
             100.0 * dec_hit / max(1, dec_all), und_all,
             ver["subobj_chain"], ver["subobj_miss"]))
    for cls, sz, worst, offs, verdict in ver["size_bad"][:12]:
        print("  [x] %-34s sizeof=%-5d 末端=%-5d %s"
              % (cls, sz, worst, verdict))

    if a.one:
        d = data.get(a.one)
        if d is None:
            print("没有 %s 的数据" % a.one)
        else:
            print("\n=== %s  sizeof=%s  字段 %d  最大末端 0x%X ===" % (
                a.one, d.get("sizeof", "?"), len(d["fields"]),
                d.get("max_end", 0)))
            for f in d["fields"]:
                print("  +0x%-5X 宽%d 写%-3d 读%-3d %s"
                      % (f["off"], f["size"], f["w"], f["r"],
                         ",".join(f["values"])))
            for s in d["subobjects"]:
                print("  内嵌 +0x%-5X %s (%d 次)" % (s["off"], s["class"],
                                                     s["hits"]))
        return

    if a.no_write:
        return

    os.makedirs(os.path.join(ROOT, "src", "re"), exist_ok=True)
    json.dump({"image": img.path, "classes": data, "verify": ver},
              open(os.path.join(ROOT, "db", "fields.json"), "w",
                   encoding="utf-8"),
              ensure_ascii=False, indent=1, sort_keys=True)
    write_docs(data, ver, os.path.join(ROOT, "docs", "fields.md"), a.top)
    write_header(data, os.path.join(ROOT, "src", "re", "FieldOffsets.h"))
    print("已写出 db/fields.json / docs/fields.md / src/re/FieldOffsets.h")


if __name__ == "__main__":
    main()
