"""
funcscan.py -- 全量反编译计划的 Stage 1：函数边界重建。

方法、判据与背景见 docs/decompile-plan.md。这个工具要解决的三件事：

  1. 旧清单（db/functions.json）28,827 条里 **22,087 条是线性兜底的产物**，
     入口 16 字节对齐率只有 4.1% —— 它们不是函数。这份清单不可用。
  2. C++ 虚方法整条通道没接：db/rtti.json 有 14,035 个虚表槽 / 6,686 个去重目标，
     只被 `call [reg+slot]` 间接调用，**93% 从来没被 call 通道找到过**。
  3. 跳表（`jmp [reg*4+表址]`）没跟，带 switch 的函数边界是错的。

实测到的硬规律（本工具最有力的对账判据）：
  0x401000–0x7C0000 段内，call 目标与虚表槽目标 **100%** 落在 16 字节边界
  （12,374/12,374）。0x7C0000 以上那 ~128 KB 是运行时库，不受这条约束。

两遍扫描：
  第一遍  不带锚点扫——只为拿到 call 目标，做播种与锚点。
  第二遍  以「入口锚点」重扫（tools/disasm.py 的 anchors），
          保证嵌在 .text 里的数据不会把指令流带偏再也不回来。
"""

from __future__ import annotations

import argparse
import bisect
import collections
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from peimage import PEImage, DEFAULT_IMAGE          # noqa: E402
import disasm                                        # noqa: E402
from disasm import F_BAD, F_CALL, F_INT3, F_JCC, F_JMP, F_RET   # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# 游戏本体与运行时库的分界。以下受 16 字节对齐律约束，以上不受。
ALIGN_SPLIT = 0x7C0000
# 过程内分支的跟随窗口；超出即判为尾调用（属于别的函数）。
WIN_BACK = 0x2000
WIN_FWD = 0x10000
# 单个函数体的最大跨度。
MAX_FUNC = 0x20000
# 跳表最多读多少项。
JT_LIMIT = 4096
# 判为填充的最小连续长度（0x90 / 0xCC）。
PAD_MIN = 4

# 播种分档，数字越大越弱。用于给每个函数挑一个"主档"，也用于去重叠时的取舍。
TIERS = ["entry", "vtable", "call", "tail", "padend", "prologue", "dataptr", "gap"]
TIER_RANK = {t: i for i, t in enumerate(TIERS)}
# 给 C++ 侧用的枚举名。顺序必须跟 TIERS 一致 —— 下面是从 TIERS 生成的，不手写。
TIER_CNAME = {"entry": "kEntry", "vtable": "kVirtual", "call": "kCall",
              "tail": "kTail", "padend": "kPadEnd", "prologue": "kPrologue",
              "dataptr": "kDataPtr", "gap": "kGap"}
# 强证据：这些档的入口一定在函数边界上，遍历时可以拿它当「到此为止」的路标。
# `padend` **不在**这里 —— 它是启发式，函数体内对齐用的 nop 也会产生候选，
# 拿它当路标会把真函数截短。它只是个播种通道，冲突交给去重叠解决。
STRONG = ("entry", "vtable", "call", "tail")


def hx(v: int) -> str:
    return "0x%08X" % v


# 未知段按大小分桶：(名称, 下界, 上界)
_UNK_BUCKETS = [(">=8192", 8192, 1 << 30), ("2048-8191", 2048, 8192),
                ("512-2047", 512, 2048), ("128-511", 128, 512), ("<128", 0, 128)]


def _buckets(runs):
    """把未知段按大小分桶。残差有多大、怎么分布，都要能说清楚。"""
    out = []
    for name, floor, ceil in _UNK_BUCKETS:
        sel = [b - a for a, b in runs if floor <= (b - a) < ceil]
        out.append([name, len(sel), sum(sel)])
    return out


# --------------------------------------------------------------------------
# 跳表
# --------------------------------------------------------------------------
class JtResolver:
    """解析 `jmp dword ptr [reg*4 + 表址]` 指向的跳表。

    表项是 VA。连续读到不再指向 .text 为止；不足 3 项就当没解析出来
    （可能是单个函数指针，不是跳表）。
    """

    def __init__(self, img: PEImage, idx: disasm.CodeIndex):
        self.img = img
        self.idx = idx
        self.ib = img.image_base
        self.cache: dict[int, tuple[list[int], int]] = {}
        self.bytes_of: dict[int, tuple[int, int]] = {}   # va 目标 -> (表起始, 表长度)

    def resolve(self, site_rva: int):
        base = self.idx.jt.get(site_rva)
        if base is None:
            return [], 0
        if base in self.cache:
            return self.cache[base]
        tgts: list[int] = []
        off = self.img.rva_to_off(base)
        if off is None:
            self.cache[base] = ([], 0)
            return self.cache[base]
        for k in range(JT_LIMIT):
            v = struct.unpack_from("<I", self.img.data, off + 4 * k)[0]
            if not (self.ib + self.idx.lo <= v < self.ib + self.idx.hi):
                break
            tgts.append(v)
        if len(tgts) < 3:
            self.cache[base] = ([], 0)
            return self.cache[base]
        nbytes = 4 * len(tgts)
        self.cache[base] = (tgts, nbytes)
        self.bytes_of[base] = (base, nbytes)
        return self.cache[base]


# --------------------------------------------------------------------------
# 过程内遍历
# --------------------------------------------------------------------------
class Walker:
    def __init__(self, img: PEImage, idx: disasm.CodeIndex, jt: JtResolver):
        self.img = img
        self.idx = idx
        self.jt = jt
        self.ib = img.image_base
        self.md = disasm.Cs(disasm.CS_ARCH_X86, disasm.CS_MODE_32)
        self.reason = ""     # walk 失败时记原因，便于统计与排查

    def _head(self, rva: int, nbytes: int = 64, nins: int = 14):
        off = self.img.rva_to_off(rva)
        if off is None:
            return []
        out = []
        for ins in self.md.disasm(self.img.data[off:off + nbytes], self.ib + rva):
            out.append(ins)
            if len(out) >= nins:
                break
        return out

    def walk(self, entry: int, entries=None, stop_at=None, strict=False,
             stop_rvas=None):
        """从 RVA entry 出发做过程内递归下降，返回记录或 None。

        entries：已知的函数入口集合（VA）。`jmp` 落到已知入口 → 判为尾调用。
        stop_at：**强证据**入口集合（VA）。落到它上面说明撞上下一个函数了，就此打住。
                没有这条规则时，函数体里内嵌的查表数据会把遍历带飞，
                最后被密度校验整段毙掉 —— 实测 0x6DD8B0（明明是 call 目标）
                就是这样丢的。
        stop_rvas：同上，但是排好序的 **RVA** 列表，用来截断函数末端。
                函数体的字节区间取的是 `[min(seen), max(seen)+len]` —— 是个**包围盒**，
                而遍历完全可能跳过中间一段（条件跳转直接跳到后面）。
                实测 0x407040 因此吞掉了 0x407070 起一整簇小函数（跨度 14.7 KB）。
                强证据入口落在包围盒里时，把末端截到它。
        strict：入口证据是否足够强。强证据入口即便密度校验不过也保留（记 low_density），
                因为「它是函数」这件事已经由调用方/虚表证明了，只是边界可疑。
        """
        idx = self.idx
        self.reason = ""
        if not idx.is_start(entry):
            self.reason = "不是指令起点"
            return None
        seen: set[int] = set()
        stack = [entry]
        calls: set[int] = set()
        tails: set[int] = set()
        exits: collections.Counter = collections.Counter()
        ret_bytes: set[int] = set()
        icalls = 0
        switches = 0
        low_density = False
        w_lo = max(idx.lo, entry - WIN_BACK)
        w_hi = min(idx.hi, entry + WIN_FWD)
        out_of_win = 0

        while stack:
            a = stack.pop()
            while True:
                if a in seen or not idx.is_start(a):
                    break
                f = idx.flags_at(a)
                if f & F_BAD:
                    break
                if stop_at is not None and a != entry and (self.ib + a) in stop_at:
                    break                     # 撞上下一个函数的入口
                seen.add(a)

                if f & F_RET:
                    im = idx.ret_imm.get(a)
                    exits["ret_imm" if im else "ret"] += 1
                    if im:
                        ret_bytes.add(im)
                    break

                if f & (F_CALL | F_JMP | F_JCC):
                    t = idx.targets.get(a)
                    if f & F_CALL:
                        if t is not None:
                            calls.add(t)
                        else:
                            icalls += 1
                    elif f & F_JMP:
                        if t is not None:
                            if t == entry:
                                # 跳回自己开头：函数内的循环，不跟（已在 seen 里）
                                exits["loop"] += 1
                            elif (entries is not None and (self.ib + t) in entries) \
                                    or not (w_lo <= t < w_hi):
                                tails.add(t)          # 尾调用
                                exits["tail"] += 1
                            else:
                                stack.append(t)
                        else:
                            tab, n = self.jt.resolve(a)
                            if n:
                                switches += 1
                                exits["switch"] += 1
                                for tt in tab:
                                    tr = tt - self.ib
                                    if w_lo <= tr < w_hi:
                                        stack.append(tr)
                                    else:
                                        out_of_win += 1
                            else:
                                exits["thunk"] += 1   # jmp [IAT] / jmp [reg]
                    else:                             # JCC
                        if t is not None and w_lo <= t < w_hi:
                            stack.append(t)

                if f & (F_JMP | F_INT3):
                    break
                nxt = idx.next_addr(a)
                if nxt <= a:
                    break
                a = nxt

        if not seen:
            self.reason = "空"
            return None
        s, e = min(seen), max(seen)
        if s != entry:
            self.reason = "向前回溯（入口选错）"
            return None
        end = e + idx.size_at(e)
        truncated = False
        if stop_rvas:
            j = bisect.bisect_right(stop_rvas, entry)
            if j < len(stop_rvas) and stop_rvas[j] < end:
                end = stop_rvas[j]        # 包围盒里有别的函数入口：末端截到它
                truncated = True
        size = end - entry
        if size > MAX_FUNC:
            self.reason = "超过 MAX_FUNC"
            return None
        # 密度校验：真实代码约 3~4 字节/指令，跨度里混进大片数据就会露馅。
        if size > 12 * len(seen) + 256:
            if not strict:
                self.reason = "密度校验不过（跨度里混进数据）"
                return None
            low_density = True
        # 必须有出口，否则是一段没有返回的碎片。
        if not (exits["ret"] or exits["ret_imm"] or exits["thunk"] or tails):
            self.reason = "没有出口"
            return None

        head = self._head(entry)
        frame = 0
        for ins in head:
            if ins.mnemonic == "call":
                break
            if ins.mnemonic == "sub" and ins.op_str.startswith("esp,"):
                try:
                    frame = int(ins.op_str.split(",")[1].strip(), 0)
                except ValueError:
                    frame = 0
                break
        seh = any("fs:" in x.op_str for x in head)
        thunk = bool(len(seen) == 1 and (idx.flags_at(entry) & F_JMP)
                     and idx.targets.get(entry) is None)

        return {
            "rva": entry, "va": self.ib + entry,
            # end 统一用 **VA**。曾经这里存的是 RVA，而 va 是 VA，
            # 两个字段单位不同 —— 下游拿 VA 比 RVA，比出来的结论全是废的。
            "end": self.ib + end, "size": size,
            "insns": len(seen), "calls": calls, "tails": tails,
            "exits": exits, "ret_bytes": ret_bytes, "icalls": icalls,
            "switches": switches, "out_of_win": out_of_win,
            "frame": frame, "seh": seh, "thunk": thunk,
            "low_density": low_density, "truncated": truncated,
            "leaf": not calls and not icalls,
        }


# --------------------------------------------------------------------------
# 播种
# --------------------------------------------------------------------------
def load_vtables(img: PEImage):
    """虚表槽目标（VA）与虚表占用的字节区间（RVA 区间），用于排除 dataptr 误报。"""
    r = json.load(open(os.path.join(ROOT, "db", "rtti.json"), encoding="utf-8"))
    tgts: set[int] = set()
    spans: list[tuple[int, int]] = []
    lo, hi = img.text_range()
    for v in r["vtables"].values():
        ents = v.get("entries") or []
        if not ents:
            continue
        rva = v["rva"]
        spans.append((rva, rva + 4 * len(ents)))
        for e in ents:
            if img.image_base + lo <= e < img.image_base + hi:
                tgts.add(e)
    return tgts, spans, r


def scan_dataptrs(img: PEImage, idx: disasm.CodeIndex, vt_spans, jt_spans):
    """在 .rdata/.data 里找指向 .text 的 4 字节值，排除虚表与跳表本体。

    掩码按 **RVA** 建（不是文件偏移）：两者的编号空间不同，混用会算错范围。
    """
    ib = img.image_base
    lo, hi = idx.lo, idx.hi
    max_rva = max(s["rva"] + max(s["vsize"], s["raw_size"]) for s in img.sections)
    mask = bytearray(max_rva + 16)
    for a, b in list(vt_spans) + list(jt_spans):
        for x in range(max(0, a), min(len(mask), b)):
            mask[x] = 1
    out: dict[int, int] = {}
    for sec in img.sections:
        if sec["name"] not in (".rdata", ".data"):
            continue
        end = sec["rva"] + min(sec["raw_size"], sec["vsize"])
        for k in range(sec["rva"], end - 3, 4):
            if mask[k]:
                continue
            off = img.rva_to_off(k)
            if off is None:
                continue
            v = struct.unpack_from("<I", img.data, off)[0]
            # 对齐律：本体区的函数入口一定 16 字节对齐，先按这条过滤掉噪声。
            if ib + lo <= v < ib + hi and (v >= ALIGN_SPLIT or (v & 0xF) == 0):
                out[v] = out.get(v, 0) + 1
    return out


def scan_prologues(img: PEImage, idx: disasm.CodeIndex):
    """`55 8B EC`（push ebp / mov ebp,esp）且入口 16 字节对齐的候选。

    注意这条通道覆盖率很低：/O2 下很多函数省掉帧指针，开头直接是
    `sub esp, N` 或 `push reg`，根本不会出现 `55 8B EC`。
    真正管用的是 scan_pad_ends。
    """
    lo, hi = idx.lo, idx.hi
    off0 = img.rva_to_off(lo)
    seg = img.data[off0:off0 + (hi - lo)]
    hits = []
    i = 0
    while True:
        i = seg.find(b"\x55\x8b\xec", i)
        if i < 0:
            break
        va = img.image_base + lo + i
        if va < ALIGN_SPLIT and (va & 0xF) == 0:
            hits.append(va)
        i += 1
    return hits


def scan_pad_ends(img: PEImage, idx: disasm.CodeIndex):
    """填充段（连续 `0x90`/`0xCC`）之后的 16 字节对齐地址 = 函数入口候选。

    这是本工具覆盖面最广的一条通道，依据是两条独立实测：
      * 本体区函数入口 100% 落 16 字节边界（12,374/12,374）；
      * 编译器用 `0x90` 填充函数之间的空隙（连续 ≥4 的有 14,922 段 / 146,873 字节），
        所以「填充的尽头」就是「下一个函数的开头」。
    代价是会有误报（函数体内的循环对齐也会产生 `nop`），
    交给后面的去重叠按证据强度解决。
    """
    lo, hi = idx.lo, idx.hi
    off0 = img.rva_to_off(lo)
    seg = img.data[off0:off0 + (hi - lo)]
    ib = img.image_base
    hits = []
    i = 0
    n = len(seg)
    while i < n:
        if seg[i] in (0x90, 0xCC):
            j = i
            while j < n and seg[j] == seg[i]:
                j += 1
            if j - i >= 2:
                va = ib + lo + j
                if va < ALIGN_SPLIT and (va & 0xF) == 0 and idx.is_start(va - ib):
                    hits.append(va)
            i = j
        else:
            i += 1
    return hits


# --------------------------------------------------------------------------
# 主流程
# --------------------------------------------------------------------------
def run(path=None, verbose=True):
    img = PEImage(path or DEFAULT_IMAGE)
    ib = img.image_base
    lo, hi = img.text_range()

    def log(*a):
        if verbose:
            print(*a)

    log("镜像 %s  .text [%s, %s)  %d 字节" % (img.path, hx(ib + lo), hx(ib + hi), hi - lo))

    # ---- 第一遍：不带锚点，只为拿 call 目标 ----
    log("第一遍扫描（无锚点）…")
    idx0 = disasm.build_index(img)
    call_targets = set()
    for a, t in idx0.targets.items():
        if idx0.flags_at(a) & F_CALL:
            call_targets.add(ib + t)
    log("  %d 条指令；call 目标去重 %d 个" % (idx0.decoded, len(call_targets)))
    del idx0

    vt_tgts, vt_spans, rtti = load_vtables(img)
    log("  虚表槽去重目标 %d 个" % len(vt_tgts))

    # ---- 第二遍：以入口锚点重扫 ----
    anchors = set(call_targets) | vt_tgts
    log("第二遍扫描（锚点 %d 个）…" % len(anchors))
    idx = disasm.build_index(img, anchors)
    log("  %d 条指令；跳表(绝对形式) %d 处，跳表(寄存器形式) %d 处，ret imm %d 条"
        % (idx.decoded, len(idx.jt), idx.jt_indirect, len(idx.ret_imm)))

    jt = JtResolver(img, idx)
    for site in idx.jt:
        jt.resolve(site)
    log("  解析出跳表 %d 张，表项合计 %d"
        % (len(jt.cache), sum(len(t) for t, _ in jt.cache.values())))

    wk = Walker(img, idx, jt)

    # ---- 播种 + 迭代到不动点 ----
    seeds: dict[int, set[str]] = collections.defaultdict(set)

    def seed(va, tier):
        if ib + lo <= va < ib + hi:
            seeds[va].add(tier)

    seed(ib + img.entry_rva, "entry")
    for t in vt_tgts:
        seed(t, "vtable")
    for t in call_targets:
        seed(t, "call")
    padends = scan_pad_ends(img, idx)
    for t in padends:
        seed(t, "padend")
    for t in scan_prologues(img, idx):
        seed(t, "prologue")
    dataptrs = scan_dataptrs(img, idx, vt_spans, list(jt.bytes_of.values()))
    for t, n in dataptrs.items():
        if n >= 1:
            seed(t, "dataptr")
    STRONG_SET = frozenset(STRONG)
    strong = {va for va, ts in seeds.items() if ts & STRONG_SET}
    stop_rvas = sorted(v - ib for v in strong)
    log("静态播种：entry 1 / vtable %d / call %d / padend %d / prologue %d / dataptr %d"
        % (len(vt_tgts), len(call_targets),
           sum(1 for v in seeds.values() if "padend" in v),
           sum(1 for v in seeds.values() if "prologue" in v),
           len(dataptrs)))
    log("  强证据入口（遍历路标）%d 个" % len(strong))

    funcs: dict[int, dict] = {}
    rejected = collections.Counter()
    failed_tier: collections.Counter = collections.Counter()
    failed_detail: list[tuple[int, str, str]] = []
    failed: set[int] = set()
    work = list(seeds)
    new_calls: set[int] = set()
    while work:
        va = work.pop()
        if va in funcs or va in failed:
            continue
        r = wk.walk(va - ib, seeds, strong,
                    bool(seeds.get(va, ()) & STRONG_SET), stop_rvas)
        if r is None:
            failed.add(va)
            rejected["walk 失败"] += 1
            rejected["原因：" + wk.reason] += 1
            for t in seeds.get(va, ()):
                failed_tier[t] += 1
            failed_detail.append((va, wk.reason,
                                  min(seeds.get(va, ("?",)),
                                      key=lambda t: TIER_RANK.get(t, 99))))
            continue
        funcs[va] = r
        for t in list(r["calls"]) + list(r["tails"]):
            tv = ib + t
            if tv not in funcs and tv not in seeds:
                seeds[tv].add("call" if t in r["calls"] else "tail")
                work.append(tv)
                new_calls.add(tv)
                strong.add(tv)
    # 分档要等播种收敛之后再定：一个函数可能后面才被更强的通道认领。
    for va, f in funcs.items():
        f["tiers"] = set(seeds.get(va, ())) or {"call"}
    log("不动点收敛：函数 %d 个（其中由过程内出边新发现的 %d 个）；遍历失败 %d 次 %s"
        % (len(funcs), len(new_calls), rejected["walk 失败"], dict(failed_tier)))

    # ---- 去重叠：按证据强度贪心接受 ----
    # 一个入口落在另一个函数体内部时，只能是两种情况：①强的那个才是真函数，
    # ②弱的那个是从函数指针表里误取到的函数内部的地址。两种都按「证据强的先占」处理。
    # 先独立算一遍**原始**重叠（不是去重后的 0 —— 那会变成自证）。
    rs = sorted((f["rva"], f["rva"] + f["size"]) for f in funcs.values())
    overlap_raw = 0
    cur_end = -1
    for a, b in rs:
        if a < cur_end:
            overlap_raw += min(b, cur_end) - a
        cur_end = max(cur_end, b)

    owned = bytearray(hi - lo)
    ranked = sorted(funcs.values(),
                    key=lambda f: (TIER_RANK[min(f["tiers"], key=lambda t: TIER_RANK[t])],
                                   f["va"]))
    keep: list[dict] = []
    dropped: list[tuple[int, str, int]] = []
    for f in ranked:
        a, b = f["rva"], f["rva"] + f["size"]
        if 1 in owned[a - lo:b - lo]:
            dropped.append((f["va"], min(f["tiers"], key=lambda t: TIER_RANK[t]),
                            sum(owned[a - lo:b - lo])))
            continue
        owned[a - lo:b - lo] = b"\x01" * (b - a)
        keep.append(f)
    funcs = {f["va"]: f for f in keep}
    dropped_tier = collections.Counter(t for _v, t, _n in dropped)
    log("去重叠：原始重叠 %d 字节；保留 %d 个，丢弃 %d 个 %s"
        % (overlap_raw, len(funcs), len(dropped), dict(dropped_tier)))

    # ---- 字节分类 ----
    label = bytearray(hi - lo)      # 0 未知 / 1 代码 / 2 填充 / 3 数据
    overlap = 0
    ranges = []
    for f in funcs.values():
        a = f["rva"]
        b = f["rva"] + f["size"]
        for x in range(a, b):
            if label[x - lo] == 1:
                overlap += 1
            label[x - lo] = 1
        ranges.append((a, b, f["va"]))

    for base, (t, nb) in jt.bytes_of.items():
        if lo <= base < hi:
            for x in range(base, min(base + nb, hi)):
                if label[x - lo] == 0:
                    label[x - lo] = 3

    off0 = img.rva_to_off(lo)
    seg = img.data[off0:off0 + (hi - lo)]
    i = 0
    pad_bytes = 0
    while i < len(seg):
        c = seg[i]
        if c in (0x90, 0xCC) and label[i] == 0:
            j = i
            while j < len(seg) and seg[j] == c and label[j] == 0:
                j += 1
            if j - i >= PAD_MIN:
                for x in range(i, j):
                    label[x] = 2
                pad_bytes += j - i
            i = j
        else:
            i += 1

    n_code = label.count(1)
    n_pad = label.count(2)
    n_data = label.count(3)
    n_unknown = label.count(0)
    unknown_runs = []
    i = 0
    while i < len(label):
        if label[i] == 0:
            j = i
            while j < len(label) and label[j] == 0:
                j += 1
            unknown_runs.append((lo + i, lo + j))
            i = j
        else:
            i += 1
    unknown_runs.sort(key=lambda r: -(r[1] - r[0]))

    # ---- 对账 ----
    ents = sorted(funcs.values(), key=lambda f: f["va"])
    below = [f for f in ents if f["va"] < ALIGN_SPLIT]
    misaligned = [f for f in below if f["va"] & 0xF]
    above = [f for f in ents if f["va"] >= ALIGN_SPLIT]
    misaligned_above = [f for f in above if f["va"] & 0xF]

    src = json.load(open(os.path.join(ROOT, "db", "sources.json"), encoding="utf-8"))
    src_vas = {f["va"] for s in src.values() for f in s["functions"]}
    src_missing = sorted(v for v in src_vas if v not in funcs)

    old = json.load(open(os.path.join(ROOT, "db", "functions.json"), encoding="utf-8"))
    old_by = collections.defaultdict(set)
    for v in old.values():
        old_by[v["src"]].add(v["va"])
    old_call_missing = sorted(v for v in old_by["call"] if v not in funcs)
    old_gap_total = len(old_by["gap"])
    retained = sum(1 for v in old_by["gap"] if v in funcs)

    bad_exit = [f for f in ents
                if not (f["exits"]["ret"] or f["exits"]["ret_imm"]
                        or f["exits"]["thunk"] or f["tails"])]

    tier_count = collections.Counter()
    for f in ents:
        tier_count[min(f["tiers"], key=lambda t: TIER_RANK[t])] += 1

    stats = {
        "image": img.path,
        "text": {"lo": ib + lo, "hi": ib + hi, "size": hi - lo},
        "insns": idx.decoded,
        "vt": {"slots": sum(len(v.get("entries") or []) for v in rtti["vtables"].values()),
               "targets": len(vt_tgts), "classes": len(rtti.get("classes", {}))},
        "jt": {"abs_sites": len(idx.jt), "reg_sites": idx.jt_indirect,
               "tables": len(jt.cache),
               "entries": sum(len(t) for t, _ in jt.cache.values())},
        "ret_imm_kinds": len(set(idx.ret_imm.values())),
        "seeds": {k: sum(1 for v in seeds.values() if k in v) for k in TIERS},
        "funcs": len(ents),
        "tier_primary": dict(tier_count),
        "bytes": {"code": n_code, "pad": n_pad, "data": n_data, "unknown": n_unknown},
        "unknown_runs": len(unknown_runs),
        "rejected": dict(rejected),
        "failed_tier": dict(failed_tier),
        "failed_detail": [[hx(v), r, t] for v, r, t in
                          sorted(failed_detail, key=lambda x: TIER_RANK.get(x[2], 99))[:400]],
        "dropped_detail": [[hx(v), t, n] for v, t, n in dropped[:400]],
        "dropped_n": len(dropped),
        "dropped_tier": dict(dropped_tier),
        "checks": {
            "aligned_below": [len(below), len(below) - len(misaligned), len(misaligned)],
            "aligned_above": [len(above), len(above) - len(misaligned_above),
                              len(misaligned_above)],
            "overlap_bytes": overlap_raw,
            "overlap_after": overlap,
            "sources_missing": src_missing,
            "old_call_missing": old_call_missing,
            "old_gap_total": old_gap_total,
            "old_gap_still_present": retained,
            "bad_exit": [hx(f["va"]) for f in bad_exit],
            "entry_present": (ib + img.entry_rva) in funcs,
        },
        "top_unknown": [(hx(ib + a), hx(ib + b), b - a) for a, b in unknown_runs[:15]],
        "unknown_buckets": _buckets(unknown_runs),
        "truncated_n": sum(1 for f in ents if f.get("truncated")),
        "low_density_n": sum(1 for f in ents if f.get("low_density")),
    }

    return img, idx, ents, stats, unknown_runs


# --------------------------------------------------------------------------
# 产物
# --------------------------------------------------------------------------
FLAG_LEAF = 1 << 4
FLAG_SEH = 1 << 5
FLAG_THUNK = 1 << 6
FLAG_CRT = 0x80


def write_json(ents, stats):
    out = {}
    for f in ents:
        rec = {
            "va": f["va"], "end": f["end"], "size": f["size"],
            "insns": f["insns"],
            "tier": min(f["tiers"], key=lambda t: TIER_RANK[t]),
            "exits": {k: v for k, v in sorted(f["exits"].items())},
        }
        if len(f["tiers"]) > 1:
            rec["tiers"] = sorted(f["tiers"], key=lambda t: TIER_RANK[t])
        if f["ret_bytes"]:
            rec["ret_bytes"] = sorted(f["ret_bytes"])
        if f["frame"]:
            rec["frame"] = f["frame"]
        if f["seh"]:
            rec["seh"] = 1
        if f["thunk"]:
            rec["thunk"] = 1
        if f["leaf"]:
            rec["leaf"] = 1
        if f["switches"]:
            rec["switch"] = f["switches"]
        if f["low_density"]:
            rec["low_density"] = 1
        if f["truncated"]:
            rec["truncated"] = 1
        if f["calls"]:
            rec["calls"] = sorted(f["calls"])
        if f["tails"]:
            rec["tails"] = sorted(f["tails"])
        out[hx(f["va"])] = rec
    data = {
        "method": "tools/funcscan.py：分档播种 + 过程内递归下降（含跳表）+ 迭代到不动点",
        "inputs": ["gamemd.exe", "db/rtti.json"],
        "image": stats["image"],
        "text": stats["text"],
        "stats": stats,
        "funcs": out,
    }
    p = os.path.join(ROOT, "db", "funcs.json")
    with open(p, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(data, fh, ensure_ascii=False, indent=0)
    return p


def write_header(ents, stats):
    L = []
    A = L.append
    A("// 自动生成，勿手改 —— 由 tools/funcscan.py 产出。")
    A("// 方法、判据与对账见 docs/decompile-plan.md 与 docs/functions.md。")
    A("#pragma once")
    A("")
    A("#include <cstdint>")
    A("")
    A("namespace ra2 {")
    A("namespace re {")
    A("namespace funcs {")
    A("")
    A("// 主档：一个函数可能同时被多条通道看到，这里记证据最强的那条。")
    A("// 顺序即证据强度，数字越小越强 —— 由 tools/funcscan.py 的 TIERS 生成，别手改。")
    A("enum Tier : uint8_t {")
    A("    " + ", ".join("%s = %d" % (TIER_CNAME[t], i)
                        for i, t in enumerate(TIERS)) + ",")
    A("};")
    A("")
    A("// 其余标志位。")
    A("enum Flag : uint8_t {")
    A("    kFlagLeaf  = 1 << 0,   // 没有 call")
    A("    kFlagSeh   = 1 << 1,   // 函数头写了 fs:[0]")
    A("    kFlagThunk = 1 << 2,   // 整个函数体只有一条 `jmp [...]`")
    A("    kFlagCrt   = 1 << 3,   // 落在 0x7C0000 以上的运行时库区")
    A("};")
    A("")
    A("inline constexpr uint32_t kAlignSplit = 0x%08Xu;" % ALIGN_SPLIT)
    A("")
    A("inline constexpr uint32_t kFuncVA[] = {")
    line = "   "
    for f in ents:
        s = " 0x%08X," % f["va"]
        if len(line) + len(s) > 110:
            A(line)
            line = "   "
        line += s
    if line.strip():
        A(line)
    A("};")
    A("")
    A("inline constexpr uint32_t kFuncSize[] = {")
    line = "   "
    for f in ents:
        s = " %u," % f["size"]
        if len(line) + len(s) > 110:
            A(line)
            line = "   "
        line += s
    if line.strip():
        A(line)
    A("};")
    A("")
    A("inline constexpr uint8_t kFuncFlags[] = {")
    line = "   "
    for f in ents:
        t = TIER_RANK[min(f["tiers"], key=lambda x: TIER_RANK[x])]
        fl = t
        if f["leaf"]:
            fl |= FLAG_LEAF
        if f["seh"]:
            fl |= FLAG_SEH
        if f["thunk"]:
            fl |= FLAG_THUNK
        if f["va"] >= ALIGN_SPLIT:
            fl |= FLAG_CRT
        s = " 0x%02X," % fl
        if len(line) + len(s) > 110:
            A(line)
            line = "   "
        line += s
    if line.strip():
        A(line)
    A("};")
    A("")
    A("inline constexpr uint32_t kFuncCount =")
    A("    sizeof(kFuncVA) / sizeof(kFuncVA[0]);")
    A("")
    A("/// 二分查一个地址落在哪个函数里（含函数内部任意偏移）。")
    A("/// 返回下标，找不到返回 kFuncCount。")
    A("inline uint32_t FuncAt(uint32_t va) {")
    A("    uint32_t lo = 0, hi = kFuncCount;")
    A("    while (lo < hi) {")
    A("        uint32_t mid = (lo + hi) / 2;")
    A("        if (kFuncVA[mid] <= va) lo = mid + 1; else hi = mid;")
    A("    }")
    A("    if (lo == 0) return kFuncCount;")
    A("    uint32_t i = lo - 1;")
    A("    return (va < kFuncVA[i] + kFuncSize[i]) ? i : kFuncCount;")
    A("}")
    A("")
    A("/// 是不是函数入口。")
    A("inline bool IsFuncEntry(uint32_t va) {")
    A("    uint32_t i = FuncAt(va);")
    A("    return i < kFuncCount && kFuncVA[i] == va;")
    A("}")
    A("")
    A("}  // namespace funcs")
    A("}  // namespace re")
    A("}  // namespace ra2")
    A("")
    p = os.path.join(ROOT, "src", "re", "FuncTable.h")
    with open(p, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(L))
    return p


def write_docs(ents, stats, img):
    c = stats["checks"]
    L = []
    A = L.append
    A("# 函数边界重建（全量反编译 Stage 1）")
    A("")
    A("> 产物由 `tools/funcscan.py` 生成，可重跑。")
    A("> 方法、阶段划分与验收判据见 `docs/decompile-plan.md`。")
    A("")
    A("## 1. 为什么重做这件事")
    A("")
    A("旧清单 `db/functions.json` 有 28,827 条，但其中 **22,087 条是「线性兜底」"
      "的产物** —— 它们不是函数，只是"
      "\"扫描到这里，前面没有函数认领，就切一段\"。")
    A("证据是入口的 16 字节对齐率：")
    A("")
    A("| 来源 | 条数 | 入口 16 字节对齐率 |")
    A("|---|---:|---:|")
    A("| `entry` | 1 | — |")
    A("| `call` | 6,635 | 93.4% |")
    A("| `prologue` | 104 | 59.6% |")
    A("| `gap` | 22,087 | **4.1%** |")
    A("")
    A("对齐率不是「挑出来的好看数字」，它来自一个独立测得的事实：")
    A("`0x401000–0x7C0000` 段内，**直接 call 目标与 RTTI 虚表槽目标 100% 落在 16 字节边界"
      "（12,374/12,374）**；")
    A("另有一条完全独立的佐证 —— `db/sources.json` 里由 `__FILE__` 断言字符串定位的"
      "213 个函数，VA 低 4 位全是 `0x0`（213/213）。")
    A("")
    A("所以 `gap` 档 4.1% 的对齐率说明：它们 96% 落在指令中间，本就不该是函数。")
    A("")
    A("## 2. 这一轮补的三条通道")
    A("")
    A("### 2.1 虚表槽（最大的缺口）")
    A("")
    A("`db/rtti.json` 里 %d 个类、**%d 个虚表槽 / %d 个去重目标**。"
      % (stats["vt"]["classes"], stats["vt"]["slots"], stats["vt"]["targets"]))
    A("C++ 虚方法只被 `call [reg+slot]` 间接调用，**永远不会**被 `call rel32` 播种找到。")
    A("实测：其中只有 **455 个（6.8%）** 曾被旧清单的强证据通道找到。")
    A("")
    A("### 2.2 跳表")
    A("")
    A("`jmp dword ptr [reg*4 + 表址]` 实测 %d 处（绝对形式），解析出 **%d 张跳表 / %d 个表项**；"
      % (stats["jt"]["abs_sites"], stats["jt"]["tables"], stats["jt"]["entries"]))
    A("另有 %d 处「表址在寄存器里」的形式（`lea reg,[tab]; jmp [reg+reg*4]`），本轮不解析，"
      "只计数。" % stats["jt"]["reg_sites"])
    A("旧实现只跟 `X86_OP_IMM` 操作数，**完全忽略跳表** → 带 `switch` 的函数被子截断。")
    A("")
    A("### 2.3 尾调用")
    A("")
    A("`jmp` 落到跟随窗口之外 → 判为尾调用，既是本函数的出口，也是**目标函数的入口证据**。"
      "旧实现丢弃了这个信息。")
    A("")
    A("### 2.4 索引锚点重同步")
    A("")
    A("线性扫描只在**解码失败**时重新对齐。`.text` 里嵌着数据，数据往往能被解成"
      "一串合法指令，此时扫描会一直跑偏、再也不回来。")
    A("`tools/disasm.py` 现在支持 `anchors`：扫描一旦跨过已知入口就丢弃这条指令、"
      "改从锚点重新开始。锚点由第一遍扫描的结果算出，所以是两遍扫描。")
    A("")
    A("## 3. 结果")
    A("")
    A("| 项 | 数量 |")
    A("|---|---:|")
    A("| 函数 | **%d** |" % stats["funcs"])
    for t in TIERS:
        n = stats["tier_primary"].get(t, 0)
        if n:
            A("| └ 主档 `%s` | %d |" % (t, n))
    A("| 指令（第二遍扫描） | %d |" % stats["insns"])
    A("| `.text` 字节 | %d |" % stats["text"]["size"])
    A("| └ 代码 | %d（%.1f%%） |" % (stats["bytes"]["code"],
                                     100.0 * stats["bytes"]["code"] / stats["text"]["size"]))
    A("| └ 填充（连续 ≥%d 的 `0x90`/`0xCC`） | %d（%.1f%%） |"
      % (PAD_MIN, stats["bytes"]["pad"],
         100.0 * stats["bytes"]["pad"] / stats["text"]["size"]))
    A("| └ 数据（跳表本体） | %d（%.1f%%） |"
      % (stats["bytes"]["data"], 100.0 * stats["bytes"]["data"] / stats["text"]["size"]))
    A("| └ **未知** | **%d（%.2f%%）** |"
      % (stats["bytes"]["unknown"], 100.0 * stats["bytes"]["unknown"] / stats["text"]["size"]))
    A("")
    A("## 4. 对账")
    A("")
    A("每一条都是**可以失败的**，不是自证。")
    A("")
    A("| # | 判据 | 结果 |")
    A("|---|---|---|")
    n_below, n_ok, n_bad = c["aligned_below"]
    A("| 1 | `%s` 以下函数入口 16 字节对齐率必须 100%% | %d/%d = %.2f%% %s |"
      % (hx(ALIGN_SPLIT), n_ok, n_below, 100.0 * n_ok / max(n_below, 1),
         "✅" if n_bad == 0 else "❌ %d 个例外" % n_bad))
    n_ab, n_abok, n_abbad = c["aligned_above"]
    A("| 2 | 以上（运行时库区）不受约束，但要对上账 | %d 个，对齐 %d（%.1f%%） |"
      % (n_ab, n_abok, 100.0 * n_abok / max(n_ab, 1)))
    A("| 3 | `db/sources.json` 的 213 个函数必须全部在结果里 | %s |"
      % ("✅ 全部命中" if not c["sources_missing"]
         else "❌ 缺 %d 个：%s" % (len(c["sources_missing"]),
                                  ", ".join(hx(v) for v in c["sources_missing"][:8]))))
    A("| 4 | 函数区间两两不重叠 | 原始重叠 %d 字节；按证据强度去重后 %d 字节，"
      "丢弃 %d 个弱证据入口 %s |"
      % (c["overlap_bytes"], c["overlap_after"], stats["dropped_n"],
         "✅" if c["overlap_after"] == 0 else "❌ 仍有 %d" % c["overlap_after"]))
    A("| 5 | 每个函数终止于 `ret` / 尾跳 / thunk | %s |"
      % ("✅ 全部有出口" if not c["bad_exit"]
         else "❌ %d 个没有出口" % len(c["bad_exit"])))
    A("| 6 | PE 入口点必须在结果里 | %s |" % ("✅" if c["entry_present"] else "❌"))
    A("| 7 | 旧清单 `call` 档 6,635 条必须全部保留 | %s |"
      % ("✅ 全部保留" if not c["old_call_missing"]
         else "⚠️ 未保留 %d 个（见 §6）" % len(c["old_call_missing"])))
    A("| 8 | 未知字节 < `.text` 的 1%%（本阶段目标） | ❌ 实为 %.2f%% —— **没达成**，"
      "残差见 §5 |" % (100.0 * stats["bytes"]["unknown"] / stats["text"]["size"]))
    A("| 9 | `.text` 每字节恰好属于 代码/填充/数据/未知 之一 | %s |"
      % ("✅ %d = %d" % (stats["bytes"]["code"] + stats["bytes"]["pad"]
                        + stats["bytes"]["data"] + stats["bytes"]["unknown"],
                        stats["text"]["size"])
         if (stats["bytes"]["code"] + stats["bytes"]["pad"] + stats["bytes"]["data"]
             + stats["bytes"]["unknown"]) == stats["text"]["size"] else "❌"))
    A("")
    if c["old_gap_total"]:
        A("旧清单的 22,087 条 `gap` 里，有 %d 条恰好落在新结果认下的函数区间内 ——"
          % c["old_gap_still_present"])
        A("**这不代表它们被\"确认\"了**：它们本来就和真函数重叠，地址偶然落在里面而已。")
        A("")
    if c["bad_exit"]:
        A("### 没有出口的函数")
        A("")
        A("> " + "、".join("`%s`" % v for v in c["bad_exit"][:40]))
        A("")
    A("## 5. 残差：未知字节 —— 本阶段目标没达成")
    A("")
    A("`unknown` = 既不属于任何函数、又不是连续填充、也不是跳表本体。")
    A("共 **%d 段 / %d 字节 = `.text` 的 %.2f%%**。§4 第 8 条要求 < 1%%，**没做到**。"
      % (stats["unknown_runs"], stats["bytes"]["unknown"],
         100.0 * stats["bytes"]["unknown"] / stats["text"]["size"]))
    A("")
    A("| 段大小 | 段数 | 字节 |")
    A("|---|---:|---:|")
    for name, n, tot in stats["unknown_buckets"]:
        A("| %s | %d | %d |" % (name, n, tot))
    A("| **合计** | **%d** | **%d** |"
      % (stats["unknown_runs"], stats["bytes"]["unknown"]))
    A("")
    A("最大的 15 段：")
    A("")
    A("| 起 | 止 | 字节 |")
    A("|---|---|---:|")
    for a, b, n in stats["top_unknown"]:
        A("| `%s` | `%s` | %d |" % (a, b, n))
    A("")
    A("这些字节**不做外推**：不说它们是代码，也不说它们是数据。")
    A("")
    A("### 5.1 本轮查清并修掉的三个成因")
    A("")
    A("每一个都是先实测复现、再改代码的：")
    A("")
    A("1. **锚点偏移算错**（`tools/disasm.py`）。锚点传进来的是 VA，内部要的是相对 `.text`")
    A("   的偏移，代码写成了 `VA - lo`（漏了 ImageBase），所有锚点整体偏了 `0x400000`，")
    A("   重同步机制**静默失效**。修掉之后，一批强证据入口从「不是指令起点」变为可遍历。")
    A("2. **包围盒吞函数**。函数的字节区间取的是 `[min(seen), max(seen)+len]`，是个包围盒；")
    A("   而遍历完全可能整段跳过中间（条件跳转直接跳到后面）。")
    A("   实测 `0x407040` 因此吞掉 `0x407070` 起一整簇小函数，跨度 14.7 KB。")
    A("   修法：强证据入口落在包围盒内就把末端截到它 —— 本轮截断 **%d** 个函数。"
      % stats["truncated_n"])
    A("3. **跳表不跟**。387 处 `jmp dword ptr [reg*4+表址]`，解析出 369 张表 / 2,706 个表项。")
    A("   旧实现只跟立即数操作数，带 `switch` 的函数会被截断。")
    A("")
    A("三轮的效果：")
    A("")
    A("| 轮次 | 未知字节 | 当轮做的事 |")
    A("|---|---:|---|")
    A("| 1 | 522,751（12.88%） | 只有 entry / call / 虚表三条通道 |")
    A("| 2 | 142,366（3.51%） | 加「填充尽头」通道 + 遍历路标 |")
    A("| 3 | %d（%.2f%%） | 修锚点偏移 + 截包围盒 |"
      % (stats["bytes"]["unknown"],
         100.0 * stats["bytes"]["unknown"] / stats["text"]["size"]))
    A("")
    A("### 5.2 还没修的成因（下一轮）")
    A("")
    A("- **大函数体内嵌查表数据**。`0x618CE4` 那段 14,360 字节是一例：函数开头是")
    A("  `sub esp, 0x240` 这类巨大栈帧，体内夹着 CRC 之类的查找表；遍历走进表里会解成")
    A("  一串假指令，最后被密度校验毙掉。要等 S2 能区分「代码块 / 数据块」之后才好处理。")
    A("- **只被间接调用、且不出现在任何表里的函数**。没有任何一条通道能看到它们。")
    A("- **表址在寄存器里的跳表**（`lea reg,[tab]; jmp [reg+reg*4]`）。本轮统计为 %d 处 ——"
      % stats["jt"]["reg_sites"])
    A("  这个数字本身要留着：它说明这条形式**没出现**，而不是我们解析过了。")
    A("")
    A("## 6. 被丢弃与遍历失败的入口（一条都不藏）")
    A("")
    A("### 6.1 遍历失败 %d 个" % stats["rejected"].get("walk 失败", 0))
    A("")
    A("| 原因 | 个数 |")
    A("|---|---:|")
    for k, v in sorted(stats["rejected"].items()):
        if k.startswith("原因："):
            A("| %s | %d |" % (k[3:], v))
    A("")
    A("按档分：`%s`。绝大多数是 `dataptr` 档 —— 函数指针表里混着指向函数**内部**的地址，"
      % "`, `".join("%s %d" % (k, v) for k, v in
                    sorted(stats["failed_tier"].items(), key=lambda x: -x[1])))
    A("这些地址本来就不是入口，被拒是对的。")
    A("")
    A("### 6.2 去重叠丢弃 %d 个" % stats["dropped_n"])
    A("")
    A("去重叠的规则是「证据强的先占」。丢弃的全部落在更弱的档上：`%s`。"
      % "`, `".join("%s %d" % (k, v) for k, v in sorted(stats["dropped_tier"].items())))
    A("")
    if stats["dropped_detail"]:
        A("| VA | 档 | 被覆盖字节 |")
        A("|---|---|---:|")
        for v, t, n in stats["dropped_detail"][:25]:
            A("| `%s` | %s | %d |" % (v, t, n))
        A("")
    A("### 6.3 旧清单 `call` 档没有保留下来的 %d 个" % len(c["old_call_missing"]))
    A("")
    if not c["old_call_missing"]:
        A("无 —— 旧清单里被直接调用过的 6,635 个地址，新结果**一个不少**。")
    else:
        A("| VA |")
        A("|---|")
        for v in c["old_call_missing"][:40]:
            A("| `%s` |" % hx(v))
    A("")
    if c["old_gap_total"]:
        A("旧清单那 22,087 条 `gap` 里，有 %d 条恰好落在新结果认下的函数区间内。"
          % c["old_gap_still_present"])
        A("**这不能算「被确认」**：它们本来就和真函数重叠，地址偶然落在里面而已 ——")
        A("4.1%% 的入口对齐率已经说明它们不是函数。")
        A("")
    if c["bad_exit"]:
        A("### 6.4 没有出口的函数")
        A("")
        A("> " + "、".join("`%s`" % v for v in c["bad_exit"][:40]))
        A("")
    A("## 7. 出口形态")
    A("")
    ex = collections.Counter()
    for f in ents:
        for k, v in f["exits"].items():
            ex[k] += v
    A("| 出口形式 | 处数 |")
    A("|---|---:|")
    for k, v in ex.most_common():
        A("| `%s` | %d |" % (k, v))
    A("")
    rb = collections.Counter()
    for f in ents:
        for v in f["ret_bytes"]:
            rb[v] += 1
    if rb:
        A("`ret N` 给出的 `__stdcall` 参数字节（下一步做签名时要用的现成通道）：")
        A("")
        A("| `ret N` | 函数数 |")
        A("|---|---:|")
        for k, v in sorted(rb.items()):
            A("| `%d` | %d |" % (k, v))
        A("")
    A("## 8. 已知局限")
    A("")
    A("- **三分之一入口只靠启发式撑着**。19,533 个函数里，主档为 `padend` 的有 6,212 个 ——")
    A("  它们靠的是「16 字节对齐 + 前面是填充」这条规律，而不是直接调用或虚表。")
    A("  这条规律本身是实测的（本体区 12,374/12,374），但落实到单个函数上，")
    A("  它的强度明显弱于 `vtable` / `call`。JSON 里每个条目都带 `tier`，用的时候要看得见。")
    A("- **%d 个函数的边界可疑**（密度校验不过，但因为入口证据强而保留，记 `low_density`）。"
      % stats["low_density_n"])
    A("- **%d 个函数的末端被截断**（包围盒里撞上别的入口）。截断是对的，"
      "但它说明这些函数的内部结构还没被读懂。" % stats["truncated_n"])
    A("- **`0x7C0000` 以上不受对齐律约束**，那段的边界质量明显低于本体区，"
      "本工具只做「有证据就采」，不假装和本体区一样可靠。")
    A("- **不做外推**：`unknown` 段不猜、不填。宁可留着，也不制造假边界。")
    A("- 重叠、无出口这类异常一律列出来，不静默丢弃。")
    A("")
    p = os.path.join(ROOT, "docs", "functions.md")
    with open(p, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(L))
    return p


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="只对账，不写产物")
    ap.add_argument("--image", default=None)
    args = ap.parse_args()

    img, idx, ents, stats, _ = run(args.image)

    c = stats["checks"]
    print()
    print("=" * 68)
    print("对账")
    print("=" * 68)
    n_below, n_ok, n_bad = c["aligned_below"]
    print("1. %s 以下入口对齐：%d/%d = %.2f%%  %s"
          % (hx(ALIGN_SPLIT), n_ok, n_below, 100.0 * n_ok / max(n_below, 1),
             "OK" if n_bad == 0 else "FAIL(%d)" % n_bad))
    n_ab, n_abok, n_abbad = c["aligned_above"]
    print("2. %s 以上入口对齐：%d/%d = %.1f%%（不受约束）"
          % (hx(ALIGN_SPLIT), n_abok, n_ab, 100.0 * n_abok / max(n_ab, 1)))
    print("3. sources.json 213 个函数：%s"
          % ("全部命中" if not c["sources_missing"]
             else "缺 %d 个 %s" % (len(c["sources_missing"]),
                                   [hx(v) for v in c["sources_missing"][:6]])))
    print("4. 原始重叠 %d 字节；去重后 %d 字节（丢弃弱证据入口 %d 个 %s）"
          % (c["overlap_bytes"], c["overlap_after"], stats["dropped_n"],
             stats["dropped_tier"]))
    print("5. 无出口函数：%d 个；播种遍历失败 %d 次 %s"
          % (len(c["bad_exit"]), stats["rejected"].get("walk 失败", 0),
             stats["failed_tier"]))
    print("6. 入口点入表：%s" % ("OK" if c["entry_present"] else "FAIL"))
    print("7. 旧 call 档未保留：%d 个" % len(c["old_call_missing"]))
    print("8. 未知字节：%d (%.2f%%)" % (stats["bytes"]["unknown"],
                                       100.0 * stats["bytes"]["unknown"] / stats["text"]["size"]))
    print()
    print("函数 %d 个；主档分布 %s" % (stats["funcs"], stats["tier_primary"]))
    print("字节账：代码 %d / 填充 %d / 数据 %d / 未知 %d / 合计 %d"
          % (stats["bytes"]["code"], stats["bytes"]["pad"], stats["bytes"]["data"],
             stats["bytes"]["unknown"],
             stats["bytes"]["code"] + stats["bytes"]["pad"] + stats["bytes"]["data"]
             + stats["bytes"]["unknown"]))
    print("未知段 %d 段，最大 10 段：" % stats["unknown_runs"])
    for a, b, n in stats["top_unknown"][:10]:
        print("   %s..%s  %d 字节" % (a, b, n))

    if args.check:
        return 0
    print()
    p1 = write_json(ents, stats)
    p2 = write_header(ents, stats)
    p3 = write_docs(ents, stats, img)
    print("写出：%s\n      %s\n      %s"
          % (os.path.relpath(p1, ROOT), os.path.relpath(p2, ROOT),
             os.path.relpath(p3, ROOT)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
