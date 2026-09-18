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

三遍扫描（都是 tools/disasm.py 的同一套扫描器）：
  第一遍  不带锚点扫 —— 只为拿到 call 目标，做播种与锚点。
  第二遍 a  以「入口锚点」重扫（anchors）—— 拿它认跳表。
  第二遍 b  锚点 + **跳表本体当数据区**（data_ranges）再扫一遍，正式用这一遍。

  为什么要 b：锚点是**点**，跳表是**区间**。跳表本体的 dword 恰好能解成合法
  指令，扫描扎进去就回不来 —— 实测一条函数因此被截断、表尾那个真入口拿不到
  指令边界。详见 docs/functions.md §5.1 第 6 条。
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
# 判为填充的最小连续长度（0x90 / 0xCC）。定成 2 是有实测依据的：
# 「填充的尽头就是下一个 16 对齐函数」这条规律在连续 ≥2 时成立，
# 而单字节的 0x90 会先落到空隙通道的候选里（0x40201F 那种），
# 走不出来才在最后一步归为填充。
PAD_MIN = 2

# 空隙候选被拒后**值得重试**的原因 —— 判定口径是"边界条件变了就可能翻盘"。
# 空隙是**顺序剥离**的：同一段空隙里先认下来的函数会把可用边界收窄，
# 所以下面这三类失败再跑一轮未必还失败：
#   没有出口     —— 硬边界把出口切掉了
#   向前回溯     —— 回溯窗口撞上边界，入口自洽性可能随边界改变
#   密度校验不过 —— 跨度里混着尚未被认领的邻居
# 而 `不是指令起点` / `空` / `超过 MAX_FUNC` 是**确定性**的：
# 跟边界无关，重试纯属浪费（实测 3,195 个 × 11 轮全是白跑）。
GAP_RETRY_REASONS = frozenset({
    "没有出口",
    "向前回溯（入口选错）",
    "密度校验不过（跨度里混进数据）",
    # 走通了、但出口只剩 `bound` 且拿不出独立证据（见 only_bound_ok）。
    # 边界收窄后它可能拿到真出口，所以也进重试集。
    "只剩 bound 出口且无独立证据",
})

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

# 「这个入口不在 seeds 里」时用的空档位集合。**不能写成 `()`** ——
# `() & frozenset(...)` 会抛 TypeError，而 tuple 与 frozenset 只有
# 在"默认值"这条路径上才会相遇，等于是个只在冷门分支上炸的雷。
NO_TIERS: frozenset = frozenset()


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
        # **口径：`(表起始 RVA, 表字节长度)`** —— 是"起始 + 长度"，不是"起始 + 结束"。
        # 这个区别踩过两次：`range(a, b)` 形式的消费方（`scan_dataptrs` 的掩码、
        # 数据区区间）会把它当"起始 + 结束"，于是 `range(0x3C7758, 48)` 是**空区间**，
        # 逻辑静默失效、且看不出任何异常。要"起始 + 结束"请用 `ranges()`。
        self.bytes_of: dict[int, tuple[int, int]] = {}

    def ranges(self) -> list[tuple[int, int]]:
        """跳表本体区间，`(起始 RVA, 结束 RVA)` 半开 —— 给要 `range(a, b)` 的消费方用。"""
        return [(base, base + nb) for base, nb in self.bytes_of.values()]

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
             stop_rvas=None, bounds=None, strict_density=None):
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
        bounds：(lo, hi) **RVA**，硬边界。给「空隙通道」用：候选入口是猜的，
                必须把它关在空隙里，否则一次遍历就能扫掉几十 KB，
                既慢又会把整片数据当成一个"函数"。
                越界的分支目标一律按尾调用处理，线性推进到边界即停，
                并在这种情况下记一个 `bound` 出口（见下）。
        strict_density：**密度校验**是否放宽。默认跟随 strict。空隙通道要把这两件事
                拆开：它的候选有硬边界兜着，出口判据可以放宽（否则 fallthru /
                走到边界这两类真出口全被毙，实测漏掉整片 CRT 函数）；
                但密度校验**绝不能放** —— 那是在"猜"的场景里唯一的防伪手段。
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
        if bounds is None:
            w_lo = max(idx.lo, entry - WIN_BACK)
            w_hi = min(idx.hi, entry + WIN_FWD)
        else:
            # 硬边界模式：窗口就是给定的区间，回溯也不许越过它。
            w_lo = max(idx.lo, bounds[0])
            w_hi = min(idx.hi, bounds[1])
        out_of_win = 0
        # 遍历是否"撞上下一个强证据入口就停"了。用来识别一种真函数：
        # 本身不含 ret/jmp，一路 fall through 进下一个函数（不返回的跳板）。
        stopped_at_entry = False

        while stack:
            a = stack.pop()
            while True:
                # 硬边界模式下窗口是**半开**区间 [w_lo, w_hi)：越界地址一律不认。
                # 这是最后一道闸 —— 下面每个分支各自也查过窗口，但兜底闸放这里，
                # 任何新加的分支路径都绕不过去。
                if bounds is not None and not (w_lo <= a < w_hi):
                    break
                if a in seen or not idx.is_start(a):
                    break
                f = idx.flags_at(a)
                if f & F_BAD:
                    break
                if stop_at is not None and a != entry and (self.ib + a) in stop_at:
                    stopped_at_entry = True
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
                if bounds is not None:
                    # 硬边界模式：线性推进的下一条必须**严格小于** w_hi。
                    # 原先写成 `nxt > w_hi` 时，恰好落在边界上的那条指令会被解码、
                    # 进 seen —— 而它按定义属于**下一个**区间。实测 CRT 区有 3 个
                    # 函数因此各多算 1 条指令，S2 的「遍历指令 100% 落块」判据
                    # 随即报出 3 条"块外指令"。
                    #
                    # 撞上边界同时要记成出口：空隙通道给的上界就是「下一个函数开头」，
                    # 函数被边界截断不代表它是碎片（实测 0x7D6F86 这类
                    # `push ebp; mov ebp,esp; sub esp,0x78; ...` 的真函数靠这条保住）。
                    if nxt >= w_hi:
                        exits["bound"] += 1
                        break
                elif nxt > w_hi:
                    # 非硬边界：上界是随手取的探索窗口，撞上它没有证据价值，只停不记。
                    break
                a = nxt

        # 把指令地址集合留给调用方（Stage 2 建基本块要用）。
        # 只在成功返回时才有意义 —— 失败时是空集，别误用。
        self.seen = seen

        if not seen:
            self.reason = "空"
            return None
        s, e = min(seen), max(seen)
        if s != entry:
            self.reason = "向前回溯（入口选错）"
            return None
        end = e + idx.size_at(e)
        if end > w_hi:
            end = w_hi                     # 包围盒末端同样不许越界
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
        if strict_density is None:
            strict_density = strict
        if size > 12 * len(seen) + 256:
            if not strict_density:
                self.reason = "密度校验不过（跨度里混进数据）"
                return None
            low_density = True
        # 必须有出口，否则是一段没有返回的碎片。
        #
        # 出口形态一共这几类，**漏掉任何一类都会把真函数当成碎片丢掉**：
        #   ret / ret_imm  —— 常规返回（ret_imm 即 __stdcall 的参数字节）
        #   thunk          —— `jmp [IAT]` / `jmp [reg]`，导入跳板或寄存器间接跳
        #   tails          —— 尾调用（jmp 出窗口或到已知入口）
        #   switch         —— 唯一的出口是一张跳转表（实测有这种函数）
        #   loop           —— `jmp <本函数入口>`，即 `jmp $`：**不返回的陷阱**。
        #                     实测 0x4F4D80 / 0x5175D0 就是 16 字节槽里的
        #                     `eb fe` + 14 个 nop，而且有真实 call 指向它们。
        #   fallthru       —— 走到强证据入口就停了、中间没有 ret/jmp。
        #                     实测 0x6BEC50 = `push ecx; call 0x7CBDDC`（不返回的
        #                     纯虚/异常跳板），后面用 nop 填到 16 字节。
        #   bound          —— 线性推进撞上**硬边界**（只有空隙通道有）。实测
        #                     0x7D6F86 = `push ebp; mov ebp,esp; sub esp,0x78; ...`
        #                     是 CRT 里一个正牌函数，被空隙上界截断而已。
        #
        # 原判据只认前四类里的三类（ret/ret_imm/thunk/tails），把后四类全毙了 ——
        # 代价是旧表的 call 档整整丢了 4 条**真入口**（见 docs/functions.md）。
        #
        # 后四类**额外要求证据够强**（strict）：`ret` 是自证的，走到 ret 就是函数；
        # 而这四种出口本身不自证 —— 一个 `jmp $` 也可能只是对齐填充，
        # 一段以跳转表收尾的也可能是从派生入口走出来的中途片段。
        # 只有「有 call 点 / 占虚表槽」这类外部证据在，才认它是函数。
        plain_exit = (exits["ret"] or exits["ret_imm"] or exits["thunk"] or tails)
        weak_exit = (exits["switch"] or exits["loop"] or stopped_at_entry
                     or exits["bound"])
        if stopped_at_entry and not plain_exit and not (exits["switch"] or exits["loop"]):
            exits["fallthru"] += 1            # 让它出现在产物里，别只活在注释里
        if not (plain_exit or (weak_exit and strict)):
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
            "insns": len(seen),
            # calls / tails 同样必须换算成 VA，理由和 end 完全一样。
            # 内部遍历用 RVA（idx 的编号空间），**只在出口处换算一次**。
            # 这条当初漏了：calls/tails 一直是 RVA，而 va 是 VA ——
            # 于是「谁调用了 0x6BEC50」这种查询永远查不到，还查不出错。
            "calls": sorted(self.ib + t for t in calls),
            "tails": sorted(self.ib + t for t in tails),
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
# 空隙通道用的两个小工具
# --------------------------------------------------------------------------
def gaps_of(spans, lo, hi):
    """spans 是 [(a, b)] 的 RVA 区间（可乱序）。返回 [lo, hi) 内未被覆盖的极大区间。

    只做区间求补，不带任何启发式 —— 「哪里还是空的」这件事必须是算出来的，
    不能靠人眼看列表。
    """
    cl = []
    for a, b in spans:
        a, b = max(a, lo), min(b, hi)
        if a < b:
            cl.append((a, b))
    cl.sort()
    merged = []
    for a, b in cl:
        if merged and a <= merged[-1][1]:
            merged[-1] = (merged[-1][0], max(merged[-1][1], b))
        else:
            merged.append((a, b))
    out = []
    pos = lo
    for a, b in merged:
        if a > pos:
            out.append((pos, a))
        pos = max(pos, b)
    if pos < hi:
        out.append((pos, hi))
    return out


def ptr_runs(seg, base_rva, tlo, thi, min_items=3):
    """找「连续若干 4 字节值都落在 [tlo, thi)」的极大字节段 —— 跳转表 / 函数指针表。

    这是**数据**，不是代码，必须先把它们标出来：
      * 空隙通道的候选入口要避开它们（否则在 15 KB 的指针表里空转）；
      * 分类阶段要把它们记成 `data` 而不是 `unknown`。

    为什么不能只按 4 字节对齐找：实测 MSVC 的跳转表前面常垫一条 2 字节的
    `mov edi,edi`（`8b ff`），整张表因此是 **2 mod 4** 对齐的 ——
    0x4059D0 处是 `8b ff`，dword 表从 0x4059D2 才开始（表项 0x405639 / 0x4059C9 /
    0x4055EB，都是 case 目标）。所以四个相位都要试。
    """
    n = len(seg)
    runs = []
    for off in range(4):
        i = off
        start = None
        while i + 4 <= n:
            v = struct.unpack_from("<I", seg, i)[0]
            if tlo <= v < thi:
                if start is None:
                    start = i
            else:
                if start is not None and (i - start) >= 4 * min_items:
                    runs.append((start, i))
                start = None
            i += 4
        if start is not None:
            e = start + 4 * ((n - start) // 4)
            if e - start >= 4 * min_items:
                runs.append((start, e))
    runs.sort()
    out = []
    for a, b in runs:
        if out and a <= out[-1][1]:
            out[-1] = (out[-1][0], max(out[-1][1], b))
        else:
            out.append((a, b))
    return [(base_rva + a, base_rva + b) for a, b in out]


def byte_index_tables(seg, base_rva, tlo, thi, regions, min_bytes=8, max_dw=4096):
    """找 MSVC 密集 switch 的**字节索引表**。

    MSVC 对「case 值连续且密集」的 switch 生成的是：

        movzx eax, byte ptr [eax + 字节表]
        jmp   dword ptr [eax*4 + dword表]

    所以布局上字节表**紧跟在 dword 表之后**，而且每个字节值都 `<` dword 表的项数。

    关键是判据不能定成「字节值小」—— 那样会把一堆零填充也算进来。
    真正的判据是**「这些值确实能当那张表的索引」**，这是可验证的：
    拿实测的 `0x0071F6F8` 验，dword 表 2 项，后面是 `00 00 01 01 00 01 01 01`，
    最大值 1 < 2，成立。

    regions 只给「还没被认领的字节段」—— 不去动已经认定是代码的地方。
    返回 [(lo_rva, hi_rva)]，整段（dword 表 + 字节表）都应记成数据。
    """
    out = []
    for ra, rb in regions:
        for ph in range(4):
            p = ra + ph
            while p + 8 <= rb:
                ndw = 0
                q = p
                while q + 4 <= rb and ndw < max_dw:
                    if not (tlo <= struct.unpack_from("<I", seg, q - base_rva)[0] < thi):
                        break
                    ndw += 1
                    q += 4
                if ndw >= 2:
                    lim = min(0x20, ndw - 1)
                    r = q
                    while r < rb and seg[r - base_rva] <= lim:
                        r += 1
                    if r - q >= min_bytes:
                        out.append((p, r))
                        p = r
                        continue
                p += 4
    out.sort()
    merged = []
    for a, b in out:
        if merged and a <= merged[-1][1]:
            merged[-1] = (merged[-1][0], max(merged[-1][1], b))
        else:
            merged.append((a, b))
    return merged


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
    # 拆成 2a / 2b，中间夹一次跳表解析：
    #   2a  只带锚点扫一遍 —— 这一遍质量已经够好，用它来认跳表；
    #   2b  把 2a 认出来的跳表**当数据区**再扫一遍，扫描不再钻进表里，
    #       表尾于是拿到指令边界（表尾/表首之间往往正夹着一个函数入口）。
    # 为什么不直接拿第一遍（无锚点）的跳表：那一遍质量差，从数据里误码出来的
    # `jmp [reg*4+disp]` 会指向随机位置，一旦当数据区就会**误遮真代码**。
    # 多扫一遍的成本约 10 秒，换掉一整类静默失效，值得。
    anchors = set(call_targets) | vt_tgts
    log("第二遍扫描（锚点 %d 个）…" % len(anchors))
    idx_a = disasm.build_index(img, anchors)
    n_a = idx_a.decoded
    jt_a = JtResolver(img, idx_a)
    for site in idx_a.jt:
        jt_a.resolve(site)
    dranges = sorted(jt_a.ranges())
    del idx_a
    log("  2a（只带锚点）：%d 条指令；认出跳表 %d 张 / %d 字节 -> 作数据区遮罩"
        % (n_a, len(dranges), sum(b - a for a, b in dranges)))
    idx = disasm.build_index(img, anchors, data_ranges=dranges)
    log("  2b（锚点 + 数据区）：%d 条指令；跳表(绝对形式) %d 处，"
        "跳表(寄存器形式) %d 处，ret imm %d 条"
        % (idx.decoded, len(idx.jt), idx.jt_indirect, len(idx.ret_imm)))

    jt = JtResolver(img, idx)
    for site in idx.jt:
        jt.resolve(site)
    # 注意：`len(jt.cache)` **不等于**表的张数 —— `resolve()` 把失败也缓存了
    # （失败存 `([], 0)`），所以 cache 是"站点去过重的表址数"，
    # 而 `bytes_of` 只收解析成功的（≥3 项）。报数时报后者。
    log("  跳表站点 %d 处 -> 解析出表 %d 张，表项合计 %d"
        % (len(idx.jt), len(jt.bytes_of),
           sum(len(t) for t, _ in jt.cache.values())))

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
    # 播种仍按原列表顺序（不改成 set —— 迭代顺序变了会影响下游依赖顺序的逻辑）；
    # 另存一个集合，给空隙通道当"像代码"的独立证据用（见 _gap_only_bound_ok）。
    prologues = scan_prologues(img, idx)
    for t in prologues:
        seed(t, "prologue")
    prologue_set = frozenset(prologues)
    dataptrs = scan_dataptrs(img, idx, vt_spans, jt.ranges())
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
    new_calls: set[int] = set()
    bounds_of: dict[int, tuple[int, int]] = {}
    gap_rej: collections.Counter = collections.Counter()

    def pump(work):
        """把队列跑干：walk、收函数、把新发现的 call / tail 目标塞回队列。
        返回这一趟**新收下**的函数 VA 列表 —— 空隙通道要靠它更新"已覆盖"图。

        第一次跑是「已知证据的发散」，后面几次跑是空隙通道的候选 —— 两者必须是
        同一个函数，否则「空隙补出来的函数」和「call 跟出来的函数」会漂移成两套标准。
        """
        got = []
        while work:
            va = work.pop()
            queued.discard(va)
            if va in funcs or va in failed:
                continue
            r = wk.walk(va - ib, seeds, strong,
                        bool(seeds.get(va, NO_TIERS) & STRONG_SET), stop_rvas,
                        bounds_of.get(va))
            if r is None:
                failed.add(va)
                rejected["walk 失败"] += 1
                rejected["原因：" + wk.reason] += 1
                if va in bounds_of:
                    gap_rej[wk.reason] += 1
                for t in seeds.get(va, ()):
                    failed_tier[t] += 1
                failed_detail.append((va, wk.reason,
                                      min(seeds.get(va, ("?",)),
                                          key=lambda t: TIER_RANK.get(t, 99))))
                continue
            funcs[va] = r
            got.append(va)

            def link(t, kind, first_seed):
                """记一条出边目标：档位照加；只有「**头一次**被播种」的目标才入队、
                才升为强证据路标。跟旧实现保持一致 —— 把每个 call 目标都升成强证据
                看着更"对"，但数据里误解出来的 `E8 rel32` 会因此把真函数拦腰截断。"""
                seeds[t].add(kind)          # 档位可以追加，取了 min 自然会选强的
                if t in funcs or not first_seed:
                    return
                new_calls.add(t)
                if t not in strong:
                    strong.add(t)
                    # stop_rvas 必须跟着长。原来它只在开头算一次，
                    # 于是「后面才发现的强入口」能当 stop_at 却不能截包围盒 ——
                    # 这正是 0x407040 那种吞掉整簇小函数的条件。
                    bisect.insort(stop_rvas, t - ib)
                if t not in queued:
                    queued.add(t)
                    work.append(t)

            for t in r["calls"]:
                link(t, "call", t not in seeds)
            for t in r["tails"]:
                link(t, "tail", t not in seeds)
        return got

    queued: set[int] = set(seeds)
    pump(list(seeds))
    log("不动点收敛：函数 %d 个（其中由过程内出边新发现的 %d 个）；遍历失败 %d 次 %s"
        % (len(funcs), len(new_calls), rejected["walk 失败"], dict(failed_tier)))

    # ---- 第 2 轮：空隙通道 ----
    # 为什么需要它：S1 收官时对账第 6 条（unknown < 1%）**没做到**，逐段查下来
    # 残下来的根本不是数据，而是我们没播种到的**真函数**：
    #   0x00402020  `8b 41 14 c3`              getter，只被虚表间接调用
    #   0x0040EBC0  `b9 2C 73 88 00; e9 ...`   尾调用跳板
    #   0x00412540  `e9 5B FF 0C 00`           纯 jmp 跳板
    # 共同点是「没有任何直接 call 指向它」，而虚表通道只覆盖 db/rtti.json
    # 里那 6,686 个槽。真正的判据不该是"谁指过它"，而是
    # **「空隙里 16 对齐处，一段能解成合法指令、且有出口的代码」**。
    #
    # 三条约束保证这不会退化成"把空闲字节都当函数"：
    #   ① 候选只在**函数没覆盖**的字节里取 —— 不可能切碎已有函数；
    #   ② 入口 16 字节对齐（§3 实测规律，本体区 100% 成立）；
    #   ③ walk() 的硬边界就是那一段空隙 —— 越界即停，且必须走到底有出口。
    # 另加一条：指针表（连续 4 字节值都指向 .text）整体排除 —— 那是数据。
    off0 = img.rva_to_off(lo)
    seg_all = img.data[off0:off0 + (hi - lo)]
    tables = ptr_runs(seg_all, lo, ib + lo, ib + hi)
    table_of = bytearray(hi - lo)
    for a, b in tables:
        for x in range(a - lo, b - lo):
            table_of[x] = 1
    log("  指针表（连续 ≥3 个 dword 都指向 .text，判为数据）%d 段 / %d 字节"
        % (len(tables), sum(b - a for a, b in tables)))

    gap_rounds = 0
    gap_cand_n = 0
    gap_failed: set[int] = set()
    # 临时排查开关：FS_DBG=0x7C6FC8,0x7D910A 时，把这些地址在空隙通道里的
    # 每一次出现都打出来（提出、拒绝原因、接受的区间）。
    # 按**数值**比，不要比字符串 —— hx() 输出 8 位十六进制，人写字习惯写 6 位，
    # 这个开关自己就先错过一次。
    _dbg = set()
    for _x in os.environ.get("FS_DBG", "").split(","):
        _x = _x.strip()
        if _x:
            try:
                _dbg.add(int(_x, 0))
            except ValueError:
                pass

    def gap_candidates(a, b):
        """一段空隙里值得一试的候选入口（RVA），升序。

        两条规则**按区段分开**，别混：
          * 本体区（`< ALIGN_SPLIT`）：只取 16 对齐处。这是实测规律，100% 成立。
          * 运行时库区（`>= ALIGN_SPLIT`）：**不要求对齐** —— 那里不受对齐律约束，
            实测硬按 16 对齐去取，会把 CRT 里一整批
            `push ebp; mov ebp, esp; sub esp, N` 的真函数全部漏掉
            （残差里 8,493 字节，绝大多数是这种真代码）。
            改成取「空隙开头」、「每一段填充之后的那个字节」，再加
            「**每一段指针表之后**」（跳过 0x00/0x90/0xCC 垫料）。
            最后这条是实测逼出来的：`0x7C7758` 是一张 12 项跳表的表首，
            `0x7C7788` 才是用它的那个真函数开头（`push ebp; push ebx;
            xor edx,edx; …`），中间只隔一个 `0x00`。不把表后位置当候选，
            这个真函数就没人发现；而表首**前面**那一个 `0x90` 反倒会成候选、
            走通、认成函数，把真函数整个吞掉。
        """
        out = []
        # 空隙开头：本体区必须 16 对齐，库区就是它自己。
        if ib + a >= ALIGN_SPLIT or (a & 0xF) == 0:
            out.append(a)
        else:
            out.append((a + 15) // 16 * 16)
        # 空隙内部：本体区按 16 步进；库区按「填充尽头」取。
        i = out[0]
        while i < b:
            out.append(i)
            if ib + i < ALIGN_SPLIT:
                i += 16
            else:
                j = i + 1
                while j < b and seg_all[j - lo] not in (0x90, 0xCC):
                    j += 1
                while j < b and seg_all[j - lo] in (0x90, 0xCC):
                    j += 1
                i = max(j, i + 1)
        # 库区追加：每张指针表结束之后、跳过垫料的那个位置。
        # 只**加**候选、不改动上面已有的推进逻辑 —— 加法不会让原来的候选消失。
        for ts, te in tables:
            if ib + a >= ALIGN_SPLIT and a <= te < b:
                k = te
                while k < b and seg_all[k - lo] in (0x00, 0x90, 0xCC):
                    k += 1
                if k < b:
                    out.append(k)
        return sorted(set(x for x in out if a <= x < b))

    def in_table(x):
        """x 是否落在已知指针表内，**或紧贴在表头前面 1~3 字节（垫料）**。

        方向很重要，**只往右看，不许往左看**：
          * `d = 0`   —— x 本身是表里的字节（数据）；
          * `d = 1..3`—— x 与表头只隔 1~3 字节，这不到 4 个字节装不下一条像样的
            指令，只可能是表前的对齐垫料。`ptr_runs` 要试 4 个相位
            （MSVC 常使表首不是 4 对齐，前面垫 1~3 字节），所以窗口取 3。

        实测就是这么漏的：`0x7C7758` 才是一张 12 项跳表的表首
        （dword 全指向 .text：`007C75C1 007C7694 …`），它前面一个 `0x90`
        （`0x7C7757`）成了一段空隙的开头，被当成候选、走通、认成函数
        （size 964），把 `0x7C7788` 那个真函数整个吞掉。

        **往左看会误杀**：表尾之后第一个字节（`0x7C7788`）往往正是那个
        真函数的入口，若把 `x-1..x-3` 也算进来，它会被自己右边的表挡掉。
        本函数只在空隙通道当闸门用（见下方 `gap_candidates` 的调用点），
        过宽会误杀真函数，过窄只会多留一个假函数——所以宁可只往右看。
        """
        i = x - lo
        n = len(table_of)
        for d in (0, 1, 2, 3):
            j = i + d
            if 0 <= j < n and table_of[j]:
                return True
        return False

    def only_bound_ok(r, va):
        """空隙候选的「像代码」闸门 —— 只对**唯一出口是撞硬边界**的候选生效。

        为什么需要这道闸：空隙通道把 `bound`（线性推进撞上硬边界）算成合法出口，
        是为了保住被上界截断的真函数 —— `0x7D6F86` 是
        `push ebp; mov ebp,esp; sub esp,0x78; ...`，正牌 CRT 函数，只是被截断了。

        但这条放宽会把**字节索引表**放进来。实测 `0x40DD54` 起是
        `01 01 01 00 01 01 …` 这样一张表，恰好在 16 对齐处（`0x40DD60`）
        能解码成一串 `add dword ptr [ecx], eax`：10 条指令、无控制流、无 call，
        一路"走"到边界 `0x40DD70`（下一个真函数的入口）——
        于是被判成"被边界截断的函数"，size 16。同型假函数实测 136 个，
        每一个都在 Stage 2 的 CFG 里留下一个「块末 = 函数末且无终止指令」的链断
        （S2 报 145 条），其中一个还造成孤儿块。

        判据：出口只剩 `bound` 这一条**不自证**的证据时，必须再有一条**独立**证据
        才认。独立证据取四类里任意一条：
          calls   —— 它自己发起过调用；
          frame   —— 头部有 `sub esp, N`（栈帧）；
          seh     —— 头部出现 `fs:`（SEH 注册）；
          prologue—— 入口是 `55 8B EC` 且 16 对齐。
        字节表这三种全不沾。被拒的字节如实留在 unknown 里，不硬凑。
        """
        ex = r["exits"]
        if ex["ret"] or ex["ret_imm"] or ex["thunk"] or r["tails"] \
                or ex["switch"] or ex["loop"]:
            return True                      # 有自证出口，无需再加证据
        return bool(r["calls"] or r["frame"] or r["seh"] or va in prologue_set)

    for rnd in range(16):
        # 上一轮被拒的空隙候选中，**只有"边界相关"的失败才值得重试**：
        # 硬边界可能因为同段空隙里新认下来的函数而变好。
        # `不是指令起点` / `空` / `超过 MAX_FUNC` 是确定性的 ——
        # 每轮重试它们纯属浪费（实测 3,195 个 × 11 轮全是白跑）。
        failed.difference_update(gap_failed)
        gap_failed.clear()
        spans = [(f["rva"], f["rva"] + f["size"]) for f in funcs.values()]
        gaps = gaps_of(spans, lo, hi)
        covered = bytearray(hi - lo)
        for a, b in spans:
            for x in range(a - lo, b - lo):
                covered[x] = 1
        before = len(funcs)
        taken = 0
        n_cand = 0
        for a, b in gaps:
            for x in gap_candidates(a, b):
                va = ib + x
                if _dbg and va in _dbg:
                    log("      DBG %s：轮 %d 提出（gap=[%s,%s) covered=%d funcs=%s failed=%s tab=%d）"
                        % (hx(va), rnd + 1, hx(ib + a), hx(ib + b),
                           covered[x - lo], va in funcs, va in failed,
                           table_of[x - lo]))
                if covered[x - lo] or va in funcs or va in failed or in_table(x):
                    continue
                n_cand += 1
                # 硬边界 = 这一段空隙**当前还没被认领的部分**。
                # 不用整段空隙：同一段里先认下来的函数会占掉一段，
                # 后面候选的边界必须收窄到它之后，否则会产生大面积互相压
                # （实测：用整段当边界时原始重叠 96 万字节、1,103 个空隙函数被丢掉）。
                lo_b = max(a, x)
                bounds_of[va] = (lo_b, b)
                seeds[va].add("gap")
                r = wk.walk(va - ib, seeds, strong, True, stop_rvas, (lo_b, b),
                            strict_density=False)
                if r is None:
                    if _dbg and va in _dbg:
                        log("      DBG %s：轮 %d 拒 —— %s"
                            % (hx(va), rnd + 1, wk.reason))
                    failed.add(va)
                    if wk.reason in GAP_RETRY_REASONS:
                        gap_failed.add(va)
                    rejected["walk 失败"] += 1
                    rejected["原因：" + wk.reason] += 1
                    gap_rej[wk.reason] += 1
                    failed_tier["gap"] += 1
                    failed_detail.append((va, wk.reason, "gap"))
                    continue
                if _dbg and va in _dbg:
                    log("      DBG %s：轮 %d 接受 size=%d insns=%d"
                        % (hx(va), rnd + 1, r["size"], r["insns"]))
                if not only_bound_ok(r, va):
                    # 走通了，但出口只有 `bound`，且拿不出任何独立证据 ——
                    # 这是字节索引表被解码成代码的典型样子，拒。
                    failed.add(va)
                    gap_failed.add(va)          # 边界收窄后仍可能翻盘，进重试集
                    rejected["原因：只剩 bound 出口且无独立证据"] += 1
                    gap_rej["只剩 bound 出口且无独立证据"] += 1
                    failed_tier["gap"] += 1
                    failed_detail.append((va, "只剩 bound 出口且无独立证据", "gap"))
                    continue
                funcs[va] = r
                taken += 1
                # 认下来的这段标记为已覆盖，里面的候选直接跳过（顺序剥离）。
                # 注意：不再手工推进 x —— 覆盖图比"下一个 16 对齐"更准，
                # 它同时管住了「新函数只覆盖了一部分」和「pump 又扩散进来一个」。
                e = min(r["rva"] + r["size"], b)
                for y in range(r["rva"] - lo, e - lo):
                    covered[y] = 1
                # 它的出边要当成新的播种证据，扩散到不动点。
                # 扩散出来的函数（可能**落在这段空隙里**）也要标成已覆盖，
                # 否则后面的候选会跟它们压上。
                for t in r["calls"]:
                    seeds[t].add("call")
                for t in r["tails"]:
                    seeds[t].add("tail")
                for nv in pump(list(r["calls"]) + list(r["tails"])):
                    rf = funcs[nv]
                    for y in range(rf["rva"] - lo,
                                   min(rf["rva"] + rf["size"], hi) - lo):
                        covered[y] = 1
        gap_rounds += 1
        gap_cand_n += n_cand
        rest = gaps_of([(f["rva"], f["rva"] + f["size"]) for f in funcs.values()], lo, hi)
        log("  空隙第 %d 轮：候选 %d 个 → 新函数 %d 个"
            "（未覆盖字节 %d 段 / %d 字节，含填充与数据）"
            % (rnd + 1, n_cand, len(funcs) - before,
               len(rest), sum(b - a for a, b in rest)))
        if len(funcs) == before:
            break
    # 分档要等**两条通道都收敛**之后再定：一个函数可能先被空隙通道收下，
    # 后面才被发现其实有 call 指着它 —— 那时它的档位应当升到 call。
    for va, f in funcs.items():
        f["tiers"] = set(seeds.get(va, ())) or {"call"}
    gap_kept = [f for f in funcs.values()
                if min(f["tiers"], key=lambda t: TIER_RANK.get(t, 99)) == "gap"]
    log("空隙通道合计：%d 轮，候选 %d 个，接受 %d 个（主档=gap），拒绝 %d 个 %s"
        % (gap_rounds, gap_cand_n, len(gap_kept),
           sum(gap_rej.values()), dict(gap_rej)))

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

    # 数据：指针表。空隙通道已经把「指针表」整段排除在候选之外，这里把它们
    # 落成 `data` —— 之前它们全被记成 unknown，于是 unknown 里混着两样东西：
    # 没播种到的真函数 + 内嵌在 .text 里的跳转表。**一样一样地落标签**，
    # 这样 unknown 才真正是「不知道」。
    data_tables = []
    for a, b in ptr_runs(seg, lo, ib + lo, ib + hi):
        n = 0
        for x in range(a - lo, b - lo):
            if label[x] == 0:
                label[x] = 3
                n += 1
        if n:
            data_tables.append((a, b, n))

    # 数据：字节索引表（紧跟在 dword 跳表之后的那张小表）。
    # 只有 dword 部分被 ptr_runs 抓到的表才够长；case 数少的表 dword 部分不足 3 项，
    # 于是整张表都留在 unknown 里 —— 本体区残差里 8,330 字节是这么来的。
    #
    # 注意扫描区段要**向前多给一段余量**：dword 表本身常常已经被 ptr_runs 判成数据，
    # 于是它落在"未知区段"之外，只扫未知区段就永远找不到配对的 dword 表 ——
    # 实测 0x5B81F8（字节表）前面 0x5B81D4 就是它的 dword 表，属于这种。
    MARGIN = 1024
    unk_regions = []
    i0 = 0
    while i0 < len(label):
        if label[i0] == 0:
            j0 = i0
            while j0 < len(label) and label[j0] == 0:
                j0 += 1
            unk_regions.append((max(lo, lo + i0 - MARGIN), lo + j0))
            i0 = j0
        else:
            i0 += 1
    byte_tabs = byte_index_tables(seg, lo, ib + lo, ib + hi, unk_regions)
    byte_tab_bytes = 0
    for a, b in byte_tabs:
        for x in range(a - lo, b - lo):
            if label[x] == 0:
                label[x] = 3
                byte_tab_bytes += 1

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

    # 填充（孤立单字节）。走到这里还没被认领、而且字节是 0x90 / 0xCC 的，
    # 只能是编译器插的对齐字节 —— 实测 353 处「1 字节空隙」全都是这种，
    # 形态一律是「上一个函数末尾 + 1 个 0x90 + 下一个 16 对齐的函数」。
    # 单列一类报出来，不跟成片的填充混在一起。
    pad1_n = 0
    for i in range(len(seg)):
        if label[i] == 0 and seg[i] in (0x90, 0xCC):
            label[i] = 2
            pad1_n += 1

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

    # 出口分两档，**别混成一个标签**：
    #   strong —— ret / ret_imm / thunk / tails，走到这里就能自证"这是个函数"；
    #   弱出口 —— switch / loop / fallthru，只有外部证据（有 call 点 / 占虚表槽）
    #             在，才认它是函数（见 walk() 里的说明）。
    # 原来的 bad_exit 只认 strong，于是那 4 个弱出口函数被标成"没有出口" ——
    # 和自己刚写下的判据自相矛盾。现在分开报。
    def _exit_kind(f):
        e = f["exits"]
        if e["ret"] or e["ret_imm"] or e["thunk"] or f["tails"]:
            return "strong"
        for k in ("switch", "loop", "fallthru", "bound"):
            if e[k]:
                return k
        return "none"

    bad_exit = [f for f in ents if _exit_kind(f) == "none"]
    weak_exit = sorted((f["va"], _exit_kind(f)) for f in ents
                       if _exit_kind(f) not in ("strong", "none"))

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
               "tables": len(jt.bytes_of),
               "entries": sum(len(t) for t, _ in jt.cache.values()),
               "data_bytes": sum(n for _, n in jt.bytes_of.values()),
               # 遮罩前后解出的指令数之差 = 从跳表字节里"解"出来的垃圾指令。
               # 这个数要留着：它是"表本体以前一直被当代码"的量化证据。
               "decode_drop": n_a - idx.decoded},
        "ret_imm_kinds": len(set(idx.ret_imm.values())),
        "seeds": {k: sum(1 for v in seeds.values() if k in v) for k in TIERS},
        "funcs": len(ents),
        "tier_primary": dict(tier_count),
        "bytes": {"code": n_code, "pad": n_pad, "data": n_data, "unknown": n_unknown},
        "pad1": pad1_n,
        "gap": {"rounds": gap_rounds, "cand": gap_cand_n, "kept": len(gap_kept),
                "rej": dict(gap_rej), "rej_total": sum(gap_rej.values())},
        "data_tables": {"n": len(data_tables),
                        "bytes": sum(n for _a, _b, n in data_tables),
                        "ptr_n": len(tables),
                        "ptr_bytes": sum(b - a for a, b in tables),
                        "byte_tabs": len(byte_tabs),
                        "byte_tab_bytes": byte_tab_bytes,
                        "list": [[hx(a), hx(b), n] for a, b, n in
                                 sorted(data_tables, key=lambda x: -x[2])[:20]]},
        "unknown_runs": len(unknown_runs),
        "rejected": dict(rejected),
        "failed_tier": dict(failed_tier),
        "failed_detail": [[hx(v), r, t] for v, r, t in
                          sorted(failed_detail, key=lambda x: TIER_RANK.get(x[2], 99))[:400]],
        "dropped_detail": [[hx(v), t, n] for v, t, n in dropped[:400]],
        "dropped_n": len(dropped),
        "dropped_tier": dict(dropped_tier),
        "old_call_total": len(old_by["call"]),
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
            "weak_exit": [(hx(v), k) for v, k in weak_exit],
            "entry_present": (ib + img.entry_rva) in funcs,
        },
        "top_unknown": [(hx(ib + a), hx(ib + b), b - a,
                         seg[a - lo:a - lo + 16].hex(" ")) for a, b in unknown_runs[:15]],
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
    A("| └ 数据（跳表本体 + 指针表） | %d（%.1f%%） |"
      % (stats["bytes"]["data"], 100.0 * stats["bytes"]["data"] / stats["text"]["size"]))
    A("| └ **未知** | **%d（%.2f%%）** |"
      % (stats["bytes"]["unknown"], 100.0 * stats["bytes"]["unknown"] / stats["text"]["size"]))
    A("")
    A("填充与数据各自还能再拆：")
    A("")
    A("| 细分 | 数量 |")
    A("|---|---:|")
    A("| 填充里的**孤立单字节** `0x90` | %d 字节（形态一律是「上个函数末尾 + 1 个 `0x90`"
      " + 下一个 16 对齐的函数」） |" % stats["pad1"])
    A("| 指针表（连续 ≥3 个 dword 都指向 `.text`） | %d 段 / %d 字节 |"
      % (stats["data_tables"]["ptr_n"], stats["data_tables"]["ptr_bytes"]))
    A("| 字节索引表（MSVC 密集 switch 的 case 索引表） | %d 张 / %d 字节 |"
      % (stats["data_tables"]["byte_tabs"], stats["data_tables"]["byte_tab_bytes"]))
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
    A("| 5 | 每个函数有出口（`ret`/尾跳/thunk 自证；`switch`/`loop`/`fallthru` "
      "须有强入口证据） | %s |"
      % ("✅ 全部有出口（其中 %d 个靠弱出口，见 §6.4）" % len(c["weak_exit"])
         if not c["bad_exit"]
         else "❌ %d 个没有出口" % len(c["bad_exit"])))
    A("| 6 | PE 入口点必须在结果里 | %s |" % ("✅" if c["entry_present"] else "❌"))
    A("| 7 | 旧清单 `call` 档 %d 条必须全部保留 | %s |"
      % (stats["old_call_total"],
         "✅ 全部保留" if not c["old_call_missing"]
         else "⚠️ 未保留 %d 个（见 §6）" % len(c["old_call_missing"])))
    _unk = stats["bytes"]["unknown"]
    _unkpct = 100.0 * _unk / stats["text"]["size"]
    A("| 8 | 未知字节 < `.text` 的 1%% | %s |"
      % ("✅ %.2f%%（%d 字节）" % (_unkpct, _unk) if _unkpct < 1.0
         else "❌ 实为 %.2f%% —— **没达成**，残差见 §5" % _unkpct))
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
    elif c["weak_exit"]:
        A("- **%d 个函数出口不自证**（`loop`/`switch`/`fallthru`），靠强入口证据收下，"
          "明细见 §6.4。" % len(c["weak_exit"]))
        A("")
    # 标题按**实测**写，不写死：这条判据（< 1%）本轮真的过了，
    # 但下一轮改口径就可能不过 —— 手写的标题一定会跟数字打架。
    A("## 5. 残差：未知字节 —— %s" % ("已达成本阶段目标" if _unkpct < 1.0
                                      else "本阶段目标没达成"))
    A("")
    A("`unknown` = 既不属于任何函数、又不是填充、也不是数据。")
    A("共 **%d 段 / %d 字节 = `.text` 的 %.2f%%**（§4 第 8 条要求 < 1%%）。"
      % (stats["unknown_runs"], _unk, _unkpct))
    A("")
    A("| 段大小 | 段数 | 字节 |")
    A("|---|---:|---:|")
    for name, n, tot in stats["unknown_buckets"]:
        A("| %s | %d | %d |" % (name, n, tot))
    A("| **合计** | **%d** | **%d** |"
      % (stats["unknown_runs"], stats["bytes"]["unknown"]))
    A("")
    A("最大的 15 段（附开头 16 字节的原始内容）：")
    A("")
    A("| 起 | 止 | 字节 | 开头 |")
    A("|---|---|---:|---|")
    for a, b, n, hexs in stats["top_unknown"]:
        A("| `%s` | `%s` | %d | `%s` |" % (a, b, n, hexs))
    A("")
    A("这些字节**不做外推**：不说它们是代码，也不说它们是数据。")
    A("")
    A("### 5.1 本轮查清并修掉的六个成因")
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
    A("3. **跳表不跟**。%d 处 `jmp dword ptr [reg*4+表址]`，解析出 %d 张表 / %d 个表项。"
      % (stats["jt"]["abs_sites"], stats["jt"]["tables"], stats["jt"]["entries"]))
    A("   旧实现只跟立即数操作数，带 `switch` 的函数会被截断。")
    A("4. **出口判据太窄**。原判据只认 `ret`/`ret_imm`/`thunk`/`tails`，")
    A("   把 `loop`（`jmp $` 陷阱）、`switch`（唯一出口是跳转表）、")
    A("   `fallthru`（一路落进下一个函数的不返回跳板）三类**真出口全毙了**。")
    A("   代价是旧清单 `call` 档丢了 4 条真入口（对账第 7 条）。修法见 §6.4：")
    A("   这三类出口必须配强入口证据才收。")
    A("5. **播种通道缺一条「没人指过的函数」**。这是把 unknown 从 2.81%% 压到 %.2f%% 的那一击。"
      % _unkpct)
    A("   逐段查下来，残下来的**根本不是数据**，而是没有直接 `call` 指向的真函数：")
    A("   `0x402020`（`8b 41 14 c3`，getter）、`0x40EBC0`（`mov ecx,imm; jmp`，尾跳跳板）、")
    A("   `0x412540`（`e9 rel32`，纯 jmp 跳板）。它们只被虚表间接调用，")
    A("   而虚表通道只能覆盖 `db/rtti.json` 里那 6,686 个槽。")
    A("   判据换成**「空隙里 16 对齐处，一段能解成合法指令、且有出口的代码」**，")
    A("   并给它三条约束（候选只在未覆盖字节里取 / 入口 16 字节对齐 / 遍历硬边界就是那段空隙）：")
    A("")
    A("   | 项 | 值 |")
    A("   |---|---:|")
    A("   | 轮数 | %d（收敛） |" % stats["gap"]["rounds"])
    A("   | 候选 | %d |" % stats["gap"]["cand"])
    A("   | 接受（主档 = `gap`） | %d |" % stats["gap"]["kept"])
    A("   | 拒绝 | %d |" % stats["gap"]["rej_total"])
    A("")
    A("   拒绝原因的分布：")
    A("")
    A("   | 原因 | 个数 |")
    A("   |---|---:|")
    for k, v in sorted(stats["gap"]["rej"].items(), key=lambda x: -x[1]):
        A("   | %s | %d |" % (k, v))
    A("")
    A("   其中 `不是指令起点` 占大多数 —— 那些 16 对齐地址落在数据串中间，")
    A("   反汇编器在那里根本没有指令边界。这正好说明该通道**没有**把数据当函数。")
    A("")
    A("6. **跳表本体被当成指令解码**。锚点是**点**，跳表是**区间** —— 扫描一头扎进"
      "一张 dword 表里，那些 dword 恰好能解成一串合法指令，于是再也回不到正轨，"
      "直到撞上下一个锚点。代价有两层：")
    A("")
    A("   - 表尾那个真入口**拿不到指令边界**。`0x7C7788` 是函数 `0x7C7371`"
      "内嵌跳表之后的续段，夹在表尾（`0x7C7758` 起 12 项）与下一处之间，"
      "反汇编器在那儿根本没有指令起点。")
    A("   - 函数在表前**被截断**：`0x7C7371` 一度只报到 998 字节，实际 **2,942 字节**；"
      "空隙通道只好在表里/表后造出假函数 —— 一个 `case` 标签 `0x7C77D0`"
      "（跳表 `0x7C7B3C` 的第 3 项）曾被当成「函数入口」。")
    A("")
    A("   修法：把跳表本体当**数据区**交给反汇编器"
      "（`disasm.build_index(..., data_ranges=…)`），扫描进区间直接跳到区间末重开解码"
      "—— 这一步**制造**了表尾的指令边界。全局代价/收益：遮罩 %d 张表 / %d 字节，"
      "解出的指令数从 %d 降到 %d（**少解 %d 条**从表字节里来的垃圾指令）。"
      % (stats["jt"]["tables"], stats["jt"]["data_bytes"],
         stats["insns"] + stats["jt"]["decode_drop"], stats["insns"],
         stats["jt"]["decode_drop"]))
    A("")
    A("   两次都栽在同一个坑上，一并记下：")
    A("")
    A("   - **`JtResolver.bytes_of` 是「起始 + 长度」，不是「起始 + 结束」。**"
      "消费方按 `range(a, b)` 用它时，`range(0x3C7758, 48)` 是**空区间** —— "
      "逻辑静默失效，没有任何异常。`scan_dataptrs` 的「排除跳表本体」因此**从来没生效过**。"
      "现在统一走 `JtResolver.ranges()`，口径写在函数名上。")
    A("   - **生成器里的循环顶检查等于没写。** 数据区跳过最初只写在外层 `while` 顶部，"
      "而 `md.disasm` 是生成器：`pos` 落进区间后，**同一次生成器调用**会接着往下解码，"
      "顶层那次检查根本轮不到。改成每条指令之后都查一次、命中即 `break` 回外层。")
    A("")
    A("另外两笔是「把不属于函数的东西正确地归位」，不是缩小分母：")
    A("")
    A("- **指针表判为数据**。`.text` 里嵌着跳转表，之前全被记成 unknown。")
    A("  判据：连续 ≥3 个 dword 值都落在 `.text` 内。本轮共认出 **%d 段 / %d 字节**；"
      % (stats["data_tables"]["ptr_n"], stats["data_tables"]["ptr_bytes"]))
    A("  其中此前落在 unknown 里、这轮才落上标签的是 %d 段 / %d 字节"
      "（其余的早被函数体盖住或已落过标签）。"
      % (stats["data_tables"]["n"], stats["data_tables"]["bytes"]))
    A("  注意**不能**假设 4 字节对齐：MSVC 常在表前垫一条 2 字节的 `mov edi,edi`，")
    A("  整张表因此是 `2 mod 4` 对齐的（实测 `0x4059D0`）。四个相位都要试。")
    A("- **孤立单字节填充**。%d 个字节形态一律是「上个函数末尾 + 1 个 `0x90` +"
      " 下一个 16 对齐的函数」，判为填充单列一类报出。" % stats["pad1"])
    A("")
    A("六轮的效果：")
    A("")
    A("| 轮次 | 未知字节 | 当轮做的事 |")
    A("|---|---:|---|")
    A("| 1 | 522,751（12.88%） | 只有 entry / call / 虚表三条通道 |")
    A("| 2 | 142,366（3.51%） | 加「填充尽头」通道 + 遍历路标 |")
    A("| 3 | 114,216（2.81%） | 修锚点偏移 + 截包围盒 + 跟跳表 |")
    A("| 4 | 114,170（2.81%） | 补出口判据（收下 `loop`/`switch`/`fallthru` + 强入口证据）")
    A("| 5 | 7,277（0.18%） | **空隙通道** + 指针表判数据 + 孤立填充字节")
    A("| 6 | **%d（%.2f%%）** | **跳表本体作数据区遮罩**（表尾拿回指令边界） |"
      % (_unk, _unkpct))
    A("")
    A("### 5.2 还没修的成因（留给下一阶段）")
    A("")
    A("- **跳表里那些字节型 case 表**（`00 01 01 01 ...`）。MSVC 的密集 switch 会在")
    A("  dword 跳表后面再放一张字节索引表，长度只能从代码里推，光看字节看不出来。")
    A("  dword 部分已经被判成数据，字节部分还留在 unknown 里。")
    A("- **表址在寄存器里的跳表**（`lea reg,[tab]; jmp [reg+reg*4]`）。本轮统计为 %d 处 ——"
      % stats["jt"]["reg_sites"])
    A("  这个数字本身要留着：它说明这条形式**没出现**，而不是我们解析过了。")
    A("- **大函数体内嵌查表数据**。这类段 S2 会给出块级证据（块之间的洞就是数据），")
    A("  这一阶段先不硬塞。")
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
    else:
        A("### 6.4 出口不自证、需要外部证据的函数")
        A("")
        A("下表这 %d 个函数**没有 `ret`**，它们靠的是另外三种出口形态：" % len(c["weak_exit"]))
        A("`loop`（`jmp $`，不返回的陷阱）、`switch`（唯一出口是跳转表）、")
        A("`fallthru`（一路落进下一个函数，说明它调用的是个不返回的例程）。")
        A("")
        A("这三类出口**本身不自证「这是个函数」** —— 一个 `eb fe` 也可能只是")
        A("对齐填充。之所以敢收，是因为它们同时有**强入口证据**（有 `call` 点、")
        A("或占着虚表槽）。判据写死在 `walk()` 里：弱出口必须配 `strict`。")
        A("")
        A("这一条是补出来的。第一版判据只认 `ret`/`ret_imm`/`thunk`/`tails`，")
        A("把这几类全毙了，代价是**旧清单 `call` 档整整丢了 4 条真入口**")
        A("（对账第 7 条由「未保留 4 个」变成「全部保留」）。")
        A("")
        A("| 入口 | 出口形态 |")
        A("|---|---|")
        for v, k in c["weak_exit"][:40]:
            A("| `%s` | `%s` |" % (v, k))
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
    A("- **三分之一入口只靠启发式撑着**。%d 个函数里，主档为 `padend` 的有 %d 个 ——"
      % (stats["funcs"], stats["tier_primary"].get("padend", 0)))
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
    print("5. 无出口函数：%d 个（另有 %d 个出口不自证：%s）；播种遍历失败 %d 次 %s"
          % (len(c["bad_exit"]), len(c["weak_exit"]),
             dict(collections.Counter(k for _, k in c["weak_exit"])),
             stats["rejected"].get("walk 失败", 0), stats["failed_tier"]))
    print("6. 入口点入表：%s" % ("OK" if c["entry_present"] else "FAIL"))
    print("7. 旧 call 档未保留：%d 个（旧清单共 %d 条）"
          % (len(c["old_call_missing"]), stats["old_call_total"]))
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
    for a, b, n, hexs in stats["top_unknown"][:10]:
        print("   %s..%s  %5d 字节  %s" % (a, b, n, hexs))

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
