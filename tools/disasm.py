"""
disasm.py -- gamemd.exe .text 段的反汇编索引与函数边界识别。

设计要点：
  * 一次性线性扫描整个 .text，用三个 bytearray（起点/长度/标志）建立索引，
    避免为约 130 万条指令各建一个 Python 对象把内存吃光。
  * 函数识别采用「递归下降 + 线性补漏」：
      1) 从入口点出发跟随直接调用与分支；
      2) 扫描所有 call 目标；
      3) 扫描标准栈帧开场 55 8B EC (push ebp / mov ebp,esp)；
      4) 对仍未覆盖的指令起点做线性兜底。
  * 引用关联（调用出边、字符串引用）统一用「整段扫描一次 + 二分归属」，
    避免 函数数 x 字符串数 的笛卡尔积。

注意：本二进制为 2001 年 MSVC x86 构建，代码段中混有手写汇编、跳转表和内联数据，
线性扫描必然存在少量误判；函数边界以递归下降结果（src=entry/call/prologue）为准，
src=gap 的是兜底产物，仅供参考。
"""

from __future__ import annotations

import bisect
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from peimage import PEImage  # noqa: E402

try:
    from capstone import Cs, CS_ARCH_X86, CS_MODE_32
    from capstone.x86 import X86_OP_IMM, X86_INS_CALL, X86_INS_JMP, X86_INS_RET
except ImportError as e:  # pragma: no cover
    raise SystemExit("需要 capstone：python -m pip install --target tools/pylibs capstone") from e

# 指令标志位
F_CALL = 1 << 0
F_RET = 1 << 1
F_JMP = 1 << 2      # 无条件跳转
F_JCC = 1 << 3      # 条件跳转
F_INT3 = 1 << 4     # 对齐填充 / 死代码
F_HLT = 1 << 5
F_BAD = 1 << 6      # 无法解码

# 单个函数体的最大跨度：超过就判为垃圾（线性扫描把数据当代码时极易触发）
MAX_FUNC = 0x8000
# 允许向后回溯的窗口（处理循环与 switch 表）
BACK_WINDOW = 0x2000

JCC_MNEMONICS = {
    "je", "jne", "jz", "jnz", "ja", "jae", "jb", "jbe", "jg", "jge", "jl", "jle",
    "js", "jns", "jo", "jno", "jp", "jnp", "jpe", "jpo", "jcxz", "jecxz",
    "jnbe", "jna", "jnae", "jnb", "jnc", "jng", "jnge", "jnl", "jnle",
    "loop", "loope", "loopne", "loopz", "loopnz",
}


class CodeIndex:
    """线性扫描得到的指令索引（三个 bytearray，紧凑）"""

    def __init__(self, img: PEImage, md):
        lo, hi = img.text_range()
        self.lo, self.hi = lo, hi
        self.n = hi - lo
        self.starts = bytearray(self.n)   # 1 = 该 RVA 是指令起点
        self.sizes = bytearray(self.n)    # 指令长度（仅起点处有效）
        self.flags = bytearray(self.n)    # F_* 位掩码
        self.targets: dict[int, int] = {}  # 指令 RVA -> 调用/跳转目标 RVA
        self.decoded = 0
        self._build(img, md)

    def _build(self, img: PEImage, md) -> None:
        lo, hi = self.lo, self.hi
        off0 = img.rva_to_off(lo)
        code = img.data[off0:off0 + (hi - lo)]
        ib = img.image_base
        # capstone 的 disasm 生成器遇到无法解码的字节会直接终止，
        # 因此这里按窗口推进 + 逐字节重同步，保证扫完整段 .text。
        pos = 0
        n = len(code)
        WINDOW = 512
        while pos < n:
            decoded_any = False
            for ins in md.disasm(code[pos:pos + WINDOW], ib + lo + pos):
                rva = ins.address - ib
                if not (lo <= rva < hi):
                    break
                decoded_any = True
                pos += ins.size
                self.decoded += 1
                self._record(rva - lo, ins, ib, lo, hi)
            if not decoded_any:
                pos += 1

    def _record(self, i: int, ins, ib: int, lo: int, hi: int) -> None:
        if 0 <= i < self.n:
            self.starts[i] = 1
            self.sizes[i] = min(ins.size, 254)
            f = 0
            mn = ins.mnemonic
            if ins.id == X86_INS_CALL:
                f |= F_CALL
            elif ins.id == X86_INS_RET:
                f |= F_RET
            elif ins.id == X86_INS_JMP:
                f |= F_JMP
            elif mn in JCC_MNEMONICS:
                f |= F_JCC
            elif mn == "int3":
                f |= F_INT3
            elif mn == "hlt":
                f |= F_HLT
            elif mn == "(bad)":
                f |= F_BAD
            if f & (F_CALL | F_JMP | F_JCC):
                for op in ins.operands:
                    if op.type == X86_OP_IMM:
                        t = op.imm - ib
                        if lo <= t < hi:
                            self.targets[lo + i] = t
                        break
            self.flags[i] = f

    # ---------- 查询 ----------
    def is_start(self, rva: int) -> bool:
        i = rva - self.lo
        return 0 <= i < self.n and self.starts[i] == 1

    def size_at(self, rva: int) -> int:
        i = rva - self.lo
        return self.sizes[i] if 0 <= i < self.n else 0

    def flags_at(self, rva: int) -> int:
        i = rva - self.lo
        return self.flags[i] if 0 <= i < self.n else 0

    def next_addr(self, rva: int) -> int:
        return rva + self.size_at(rva)


def build_index(img: PEImage) -> CodeIndex:
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True
    return CodeIndex(img, md)


class FunctionFinder:
    def __init__(self, img: PEImage, idx: CodeIndex):
        self.img = img
        self.idx = idx
        self.funcs: dict[int, dict] = {}
        self.covered = bytearray(idx.n)

    # ---------- 过程内遍历 ----------
    def _walk(self, entry: int):
        """过程内遍历，返回 (start, end, insn_count, calls, body) 或 None。

        只跟随落在窗口 [entry-BACK, entry+MAX_FUNC] 内的分支：
        跳得更远的 jmp 视为尾调用（属于别的函数），不再并进本函数体，
        否则线性扫描的垃圾跳转会把整个 .text 串成一个"巨型函数"。
        """
        if not self.idx.is_start(entry):
            return None
        lo, hi = self.idx.lo, self.idx.hi
        win_lo = max(lo, entry - BACK_WINDOW)
        win_hi = min(hi, entry + MAX_FUNC)
        seen: set[int] = set()
        calls: set[int] = set()
        stack = [entry]
        ib = self.img.image_base
        while stack:
            a = stack.pop()
            while True:
                if a in seen or not (lo <= a < hi) or not self.idx.is_start(a):
                    break
                seen.add(a)
                f = self.idx.flags_at(a)
                if f & (F_BAD | F_RET):
                    break
                if f & (F_CALL | F_JMP | F_JCC):
                    t = self.idx.targets.get(a)
                    if t is not None and f & F_CALL:
                        calls.add(ib + t)
                    elif t is not None and win_lo <= t < win_hi:
                        stack.append(t)
                if f & F_JMP:
                    break
                a = self.idx.next_addr(a)
                if f & (F_INT3 | F_HLT):
                    break
        if not seen:
            return None
        s, e = min(seen), max(seen)
        if s < entry or e - entry > MAX_FUNC:
            return None
        n = len(seen)
        # 密度校验：真实代码约 3~4 字节/指令，即便含 switch 跳转表也很少超过 12。
        # 密度过低说明函数体跨度里混进了大片未解码的数据，判为误判。
        if (e - s) > 12 * n + 256:
            return None
        return s, e + self.idx.size_at(e), n, calls, seen

    def add(self, entry_va: int, src: str) -> bool:
        entry = entry_va - self.img.image_base
        if entry in self.funcs or not self.idx.is_start(entry):
            return False
        r = self._walk(entry)
        if r is None:
            return False
        start, end, ninsn, calls, body = r
        self.funcs[entry] = {
            "rva": entry,
            "va": self.img.image_base + entry,
            "start": start,
            "end": end,
            "size": end - start,
            "insns": ninsn,
            "src": src,
            "calls": calls,
            "strings": set(),
        }
        off = self.idx.lo
        for a in body:
            i = a - off
            if 0 <= i < self.idx.n:
                self.covered[i] = 1
        return True

    # ---------- 播种 ----------
    def seed(self) -> int:
        n = self.add(self.img.image_base + self.img.entry_rva, "entry")
        for a, t in self.idx.targets.items():
            if self.idx.flags_at(a) & F_CALL:
                n += self.add(self.img.image_base + t, "call")
        return n

    def seed_prologues(self) -> int:
        blob = self.img.data
        lo = self.idx.lo
        off0 = self.img.rva_to_off(lo)
        seg = blob[off0:off0 + self.idx.n]
        n = 0
        i = 0
        while True:
            i = seg.find(b"\x55\x8b\xec", i)
            if i < 0:
                break
            n += self.add(self.img.image_base + lo + i, "prologue")
            i += 1
        return n

    def seed_gaps(self) -> int:
        n = 0
        a = self.idx.lo
        while a < self.idx.hi:
            i = a - self.idx.lo
            if self.idx.starts[i] and not self.covered[i]:
                n += self.add(self.img.image_base + a, "gap")
                i = a - self.idx.lo
            a = self.idx.next_addr(a) if self.idx.starts[i] else a + 1
        return n

    # ---------- 字符串 / 数据引用归属 ----------
    def link_refs(self, wanted_vas, skip_src=("gap",)) -> None:
        """单次扫描 .text，把 4 字节立即数引用归属到所属函数。

        skip_src：默认跳过线性兜底产生的函数——它们经常与真实函数重叠，
        参与归属会把引用算到错误的函数头上。
        """
        pats = {struct.pack("<I", v): v for v in wanted_vas}
        lo = self.idx.lo
        off0 = self.img.rva_to_off(lo)
        blob = self.img.data[off0:off0 + self.idx.n]
        bounds = sorted((f for f in self.funcs.values() if f["src"] not in skip_src),
                        key=lambda f: f["start"])
        keys = [b["start"] for b in bounds]
        for k in range(0, len(blob) - 3):
            v = pats.get(blob[k:k + 4])
            if v is None:
                continue
            rva = lo + k
            j = bisect.bisect_right(keys, rva) - 1
            if j < 0:
                continue
            fn = bounds[j]
            if fn["start"] <= rva < fn["end"]:
                fn["strings"].add(v)

    def to_json(self) -> dict:
        out = {}
        for f in self.funcs.values():
            out["0x%08X" % f["va"]] = {
                "va": f["va"],
                "rva": f["rva"],
                "end": f["end"],
                "size": f["size"],
                "insns": f["insns"],
                "src": f["src"],
                "calls": sorted(f["calls"]),
                "strings": sorted(f["strings"]),
            }
        return out


def analyze(path: str | None = None, do_gaps: bool = True):
    img = PEImage(path) if path else PEImage()
    idx = build_index(img)
    ff = FunctionFinder(img, idx)
    ff.seed()
    ff.seed_prologues()
    if do_gaps:
        ff.seed_gaps()
    return img, idx, ff
