"""
sizeofscan.py -- 从 gamemd.exe 里把关键类的 sizeof 挖出来。

思路（纯静态、不运行目标进程）：
  1) MSVC 的 `new Class` 编译成
         push  <sizeof(Class)>     ; 68 xx xx xx xx
         call  operator_new
         ...
         mov   dword ptr [eax], offset ??_7Class@@6B@     ; 写虚表
     所以：同一个函数里同时出现「push 常量 + call 同一个目标」和「写某张虚表」
     这两件事时，那个常量就是该类的 sizeof。

  2) operator new 是 CRT 静态链进来的，不在导入表里。这里用「被
     `push 立即数` 紧跟其后调用得最多的那个函数」来识别它 —— 分配器是唯一
     一个几乎到处被这么调用的函数，命中数会远远高于其他函数。

为什么必须先拿到 sizeof：
  还原出的 C++ 结构体如果大小不对，字段偏移就全错，后面所有按类型批量化
  （多核改造的前提）都无从谈起。这是最硬的一个前置数据。

输出：db/sizes.json + 打印结果
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

# 必须先 import peimage：它会把 tools/pylibs 加进 sys.path，capstone 才能找到。
from peimage import PEImage, DEFAULT_IMAGE  # noqa: E402

from capstone import Cs, CS_ARCH_X86, CS_MODE_32  # noqa: E402
from capstone.x86 import (  # noqa: E402
    X86_OP_IMM, X86_OP_MEM, X86_OP_REG, X86_INS_CALL, X86_INS_MOV,
)

MIN_SIZE = 8
MAX_SIZE = 65536


def cpp_str(s: str) -> str:
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def load_functions(path: str) -> dict:
    return json.load(open(path, encoding="utf-8"))


def find_operator_new(img: PEImage, funcs: dict, md: Cs) -> int:
    """被调用前紧跟 `push <常量>` 次数最多的函数 —— 就是 operator new。"""
    hits: Counter = Counter()
    for k, f in funcs.items():
        lo = f["rva"]
        hi = f["end"]
        if hi - lo > 0x20000:
            continue
        blob = img.data[img.rva_to_off(lo):img.rva_to_off(hi)]
        base = img.image_base + lo
        prev = None
        for ins in md.disasm(blob, base):
            if ins.id == X86_INS_CALL:
                tgt = ins.operands[0]
                if tgt.type == X86_OP_IMM:
                    if prev is not None and prev.mnemonic == "push" \
                            and prev.operands[0].type == X86_OP_IMM:
                        v = prev.operands[0].imm
                        if MIN_SIZE <= v <= MAX_SIZE:
                            hits[tgt.imm] += 1
            prev = ins
    if not hits:
        return 0
    return hits.most_common(1)[0][0]


def find_constructors(img: PEImage, funcs: dict, vt_to_class: dict, md: Cs) -> dict:
    """构造函数特征：函数开头若干条指令内出现 `mov [ecx], <虚表 VA>`。

    MSVC thiscall 用 ecx 传 this，所以构造函数第一件事就是把虚表写进
    对象首字段（多重继承时后面还会写 [ecx+n]）。
    """
    ctors: dict[int, int] = {}      # ctor_va -> vtable VA（取最早出现的那张）
    for k, f in funcs.items():
        lo, hi = f["rva"], f["end"]
        if hi - lo > 0x20000:
            continue
        blob = img.data[img.rva_to_off(lo):img.rva_to_off(hi)]
        base = img.image_base + lo
        n = 0
        for ins in md.disasm(blob, base):
            if n >= 32:
                break
            n += 1
            if ins.id != X86_INS_MOV or len(ins.operands) != 2:
                continue
            dst, src = ins.operands
            if src.type != X86_OP_IMM or src.imm not in vt_to_class:
                continue
            if dst.type == X86_OP_MEM:
                # 形态 1：mov dword ptr [<this>], <虚表> —— 直接写对象首字段。
                # this 不一定还在 ecx：MSVC 常常先 `mov esi, ecx` 再用 esi 当基址
                # （AircraftClass 的构造函数就是这样），所以除 esp/ebp 外都接受。
                if dst.mem.index == 0:
                    reg = ins.reg_name(dst.mem.base) if dst.mem.base else ""
                    if reg not in ("", "esp", "ebp"):
                        ctors.setdefault(base, src.imm)
                        break
            elif dst.type == X86_OP_REG:
                # 形态 2：mov <reg>, <虚表> —— 先取地址，随后再存进对象。
                # 类型类的构造函数（0x006Cxxxx 那批工厂的目标）就是这个写法，
                # 只认形态 1 会把它们全漏掉。
                ctors.setdefault(base, src.imm)
                break
    return ctors


def scan(img: PEImage, funcs: dict, vtables: dict, md: Cs, new_va: int):
    """配对「push 常量 + call new + call 构造函数」与构造函数写的虚表。"""
    # 反向索引：虚表 VA -> 类名
    vt_to_class = {}
    for va, name in vtables.items():
        vt_to_class.setdefault(int(va, 16) if isinstance(va, str) else va, name)

    ctors = find_constructors(img, funcs, vt_to_class, md)

    results = defaultdict(Counter)   # class -> Counter(size)  证据：new 的大小
    strides = defaultdict(Counter)   # class -> Counter(size)  证据：数组步长
    sites = defaultdict(list)        # class -> [(size, func_va)]

    for k, f in funcs.items():
        lo, hi = f["rva"], f["end"]
        if hi - lo > 0x20000:
            continue
        blob = img.data[img.rva_to_off(lo):img.rva_to_off(hi)]
        base = img.image_base + lo

        # 逐条扫，记录两类事件的位置：
        #   alloc  : push <常量>; call operator_new
        #   write  : mov [ecx|eax+n], <虚表 VA>   —— 构造函数内联在本函数时的情形
        #   invoke : call <已知构造函数>
        allocs: list[tuple[int, int]] = []
        writes: list[tuple[int, int]] = []
        invokes: list[tuple[int, int]] = []
        insns: list = []
        idx = 0
        prev = None
        for ins in md.disasm(blob, base):
            insns.append(ins)
            if ins.id == X86_INS_CALL and ins.operands[0].type == X86_OP_IMM:
                tgt = ins.operands[0].imm
                if tgt == new_va and prev is not None and prev.mnemonic == "push" \
                        and prev.operands[0].type == X86_OP_IMM:
                    v = prev.operands[0].imm
                    if MIN_SIZE <= v <= MAX_SIZE:
                        allocs.append((idx, v))
                elif tgt in ctors:
                    invokes.append((idx, tgt))
            elif ins.id == X86_INS_MOV and len(ins.operands) == 2:
                dst, src = ins.operands
                if dst.type == X86_OP_MEM and src.type == X86_OP_IMM:
                    v = src.imm
                    # 只认「写进刚分配出来的那块内存」：基址寄存器 + 小偏移。
                    # 写全局变量的形态是 mem.base==0 且 disp 很大，已被排除。
                    if v in vt_to_class and dst.mem.base != 0 and dst.mem.index == 0:
                        writes.append((idx, v))
            prev = ins
            idx += 1

        if not allocs and not invokes:
            continue

        # 形态 C：静态数组里的对象。构造函数被逐个调用，循环体末尾用
        #       add <reg>, <sizeof>   推进到下一个元素。
        #   这解释了为什么 BuildingClass / AircraftClass 这些类抓不到 ——
        #   它们不是 new 出来的，而是静态数组（RA2 用固定上限的对象池）。
        STRIDE_WIN = 8
        for ii, tgt in invokes:
            cls = vt_to_class[ctors[tgt]]
            for j in range(ii + 1, min(ii + 1 + STRIDE_WIN, len(insns))):
                ins = insns[j]
                if ins.mnemonic not in ("add", "sub"):
                    continue
                ops = ins.operands
                if len(ops) != 2 or ops[1].type != X86_OP_IMM:
                    continue
                v = ops[1].imm
                if v % 4 or not (16 <= v <= 8192):
                    continue
                reg = ins.reg_name(ops[0].reg) if ops[0].type == X86_OP_REG else ""
                if reg in ("esp", "ebp", ""):
                    continue
                strides[cls][v] += 1
                sites[cls].append((v, base))
                break

        # 约束：分配和「拿到类型信息」之间不能隔太远。
        # 两种合法形态：
        #   A) ctor 被内联：  call new ... mov [eax], <vt>
        #   B) ctor 是独立函数：call new ... mov ecx,eax; call <ctor>
        # 不设这个约束时，大函数里的 new 会和不相干的虚表乱配
        # （实测 CommandClass 一族会被全部配成 sizeof=8）。
        WINDOW = 16
        for ai, size in allocs:
            best = None
            for wi, v in writes:
                d = wi - ai
                if 0 < d <= WINDOW and (best is None or d < best[0]):
                    best = (d, v)
            if best is not None:
                cls = vt_to_class[best[1]]
                results[cls][size] += 1
                sites[cls].append((size, base))
                continue
            for ii, tgt in invokes:
                d = ii - ai
                if 0 < d <= WINDOW and (best is None or d < best[0]):
                    best = (d, tgt)
            if best is not None and isinstance(best[1], int) and best[1] in ctors:
                cls = vt_to_class[ctors[best[1]]]
                results[cls][size] += 1
                sites[cls].append((size, base))

        # 宽松档：本函数只有一处分配、且只调用了一个可识别的构造函数。
        # 典型是 `new XTypeClass(ini_entry)` 这类工厂函数（0x006Cxxxx 一族），
        # 分配和构造之间隔着参数入栈等十几条指令，紧邻约束抓不到。
        # 同一个类可能有多个构造函数（默认构造 / 拷贝构造 / 代理构造），
        # 这里按「类」去重，否则 AircraftClass 这种会被判成"多个 ctor"而漏掉。
        classes_invoked = {vt_to_class[ctors[t]] for _, t in invokes}
        if len(allocs) == 1 and len(classes_invoked) == 1:
            size = allocs[0][1]
            cls = classes_invoked.pop()
            results[cls][size] += 1
            sites[cls].append((size, base))

    return results, strides, sites


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", default=DEFAULT_IMAGE)
    ap.add_argument("--funcs", default=os.path.join(ROOT, "db", "functions.json"))
    ap.add_argument("--virtuals", default=os.path.join(ROOT, "db", "virtuals.json"))
    a = ap.parse_args()

    img = PEImage(a.image)
    funcs = load_functions(a.funcs)
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True

    new_va = find_operator_new(img, funcs, md)
    print("operator new = 0x%08X" % new_va)
    if not new_va:
        sys.exit("[x] 没能定位 operator new")

    # --virtuals 是历史遗留参数：本工具真正要的「虚表 VA -> 类名」映射是从
    # rtti.json 现算的（见下），virtuals.json 读了从来没用上，而且这个文件
    # 既不在仓库里也生成不出来。别让它卡住整条流程，只提示一句。
    if not os.path.exists(a.virtuals):
        print("[i] %s 不存在（历史遗留输入，本工具用不到），跳过" % a.virtuals)
    # virtuals.json 的键是类名，值是槽位数组；这里需要「虚表 VA -> 类名」，
    # 所以再从 rtti.json 取一遍（主虚表地址）。
    rtti = json.load(open(os.path.join(ROOT, "db", "rtti.json"), encoding="utf-8"))
    vtables = {}
    for name, c in rtti["classes"].items():
        for v in c["vtables"]:
            vtables[v["va"]] = name

    results, strides, sites = scan(img, funcs, vtables, md, new_va)

    out = {}
    for cls in set(results) | set(strides):
        item = {}
        if cls in results:
            sz, n = results[cls].most_common(1)[0]
            item = {
                "sizeof": sz,
                "votes": n,
                "evidence": "new",   # new 的分配大小，最强证据
                "candidates": {str(k): v for k, v in results[cls].most_common(6)},
            }
        else:
            sz, n = strides[cls].most_common(1)[0]
            item = {
                "sizeof": sz,
                "votes": n,
                "evidence": "stride",  # 静态数组的遍历步长，次强
                "candidates": {str(k): v for k, v in strides[cls].most_common(6)},
            }
        out[cls] = item

    # ------------------------------------------------------------------
    # 用「字段末端」给 sizeof 兜底校准。
    #
    # 为什么需要：投票选出的候选可能是**配对错了**的分配 —— 构造函数里还
    # 嵌着别的分配（子对象、临时缓冲），`push N; call new` 的"最近配对"
    # 会把那个 N 记到外层类头上。实测 `CCFileClass` 就是这样：投票选出 36
    # （4 票），而 `db/fields.json` 说这个类的构造函数在 `+0x68` 上写过 4
    # 字节，**对象至少 108 字节**。108 也在候选里（2 票），只是票少。
    #
    # 判据是硬的：字段偏移是"对象内偏移"，它必须落在 sizeof 之内。
    # 两条数据来自完全不同的机制（分配常量配对 vs 指令流里 this 偏移跟踪），
    # 所以谁也不能回头改谁 —— 这里只做「候选里挑一个跟字段不矛盾的」。
    #
    # 反过来也要守规矩：如果**所有**候选都小于字段末端，那说明其中一条数据
    # 本身就错了，这时不改，只记 `note` 留给人看。乱猜比不改更坏。
    fields_end: dict[str, int] = {}
    fpath = os.path.join(ROOT, "db", "fields.json")
    if os.path.exists(fpath):
        for c, d in json.load(open(fpath, encoding="utf-8"))["classes"].items():
            fields_end[c] = int(d.get("max_end") or 0)

    adj = []
    for cls, item in out.items():
        end = fields_end.get(cls, 0)
        item["fields_end"] = end
        cand = {int(k): v for k, v in item["candidates"].items()}
        if end <= 0 or item["sizeof"] >= end:
            continue
        keep = {k: v for k, v in cand.items() if k >= end}
        if not keep:
            item["note"] = ("字段末端 %d 超过所有候选 %s —— 两者必有一错，"
                            "留给人看" % (end, sorted(cand)))
            continue
        old = item["sizeof"]
        sz, n = max(keep.items(), key=lambda kv: (kv[1], -kv[0]))
        adj.append((cls, old, sz, end, cand, keep))
        item["sizeof"] = sz
        item["votes"] = n
        item["calibrated"] = True
        item["note"] = ("原选 %d 与字段末端 %d 矛盾（%d < %d），"
                        "改成候选里合法且票数最高的 %d"
                        % (old, end, old, end, sz))

    with open(os.path.join(ROOT, "db", "sizes.json"), "w", encoding="utf-8") as f:
        json.dump({"operator_new": new_va, "classes": out}, f, ensure_ascii=False, indent=1)

    if adj:
        print("按字段末端校准 sizeof 的类 %d 个：" % len(adj))
        for cls, old, sz, end, cand, keep in adj:
            print("  %-34s %d -> %-5d（末端 %d；候选 %s；可用 %s）"
                  % (cls, old, sz, end, cand, keep))
    else:
        print("按字段末端校准 sizeof：本次没有需要改的")

    # 输出人类可读的文档
    ml = [
        "# gamemd.exe 类大小实测值",
        "",
        "由 `tools/sizeofscan.py` 静态分析得出，未运行目标进程。",
        "",
        "## 方法",
        "",
        "1. 先定位 `operator new`：被调用前紧跟 `push <常量>` 次数最多的函数。",
        "   实测为 `0x%08X`，其函数体是 `push 1; push [esp+8]; call _nh_malloc`，"
        "确认无误。" % new_va,
        "2. 找构造函数：函数开头 32 条指令内出现 `mov [<this>], <虚表 VA>`",
        "   （this 可能在 ecx/esi/eax 等任意寄存器里），或 `mov <reg>, <虚表 VA>`。",
        "3. 配对三种形态：",
        "   - `push N; call new` 之后紧邻写虚表（构造函数被内联）",
        "   - `push N; call new` 之后 16 条指令内 `call <构造函数>`",
        "   - 工厂函数：整个函数只有一处分配、只调用一个类的构造函数",
        "4. **用字段末端给候选做硬过滤**（见下）。",
        "",
        "`evidence=new` 表示来自分配大小（直接证据）；`evidence=stride` 表示来自",
        "静态数组遍历步长（间接证据，置信度低一些）。",
        "",
        "### 为什么需要第 4 步",
        "",
        "「最近配对」会配错：构造函数里往往还嵌着**别的**分配（子对象、临时",
        "缓冲），那些 `push N; call new` 会被记到外层类头上。实测 `CCFileClass`",
        "就是 —— 投票选出 36（4 票），而字段偏移扫描说这个类的构造函数在",
        "`+0x68` 上写过 4 字节，**对象至少 108 字节**；108 也在候选里，只是票少。",
        "",
        "判据是硬的：字段偏移是『对象内偏移』，必须落在 sizeof 之内。两条数据",
        "来自完全不同的机制（分配常量配对 vs 指令流里 this 偏移跟踪），谁也不能",
        "回头改谁。所以规则是：**在候选里挑一个与字段不矛盾的**（优先票数，同票",
        "取小）；如果连一个都不剩，就不改，只记 `note` 留给人看 —— 那说明两条",
        "数据里有一条本身错了，乱猜比不改更坏。",
        "",
        "被这条规则改过的条目带 `calibrated` 标记，无论票数多少都进 C++ 表。",
        "",
        "## 结果（共 %d 个类）" % len(out),
        "",
        "| 类 | sizeof | 证据 | 票数 | 字段末端 | 备注 |",
        "|---|---:|---|---:|---:|---|",
    ]
    for cls in sorted(out, key=lambda c: (-out[c]["sizeof"], c)):
        e = out[cls]
        ml.append("| `%s` | %d | %s | %d | %s | %s |" % (
            cls, e["sizeof"], e["evidence"], e["votes"],
            e.get("fields_end") or "", e.get("note", "")))
    open(os.path.join(ROOT, "docs", "sizes.md"), "w",
         encoding="utf-8", newline="\n").write("\n".join(ml) + "\n")

    # 生成 C++ 表。只收票数 >= MIN_VOTES 的，避免把单次巧合当成事实。
    # 例外：被字段末端校准过的条目**不管票数**都收 —— 它的证据比投票硬，
    # 「对象的字段延伸到 108」是硬下界，而 36 票是从错的配对上来的。
    MIN_VOTES = 4
    keep = [(c, out[c]) for c in sorted(out, key=lambda c: (-out[c]["sizeof"], c))
            if out[c]["votes"] >= MIN_VOTES or out[c].get("calibrated")]
    hl = [
        "// 自动生成文件，请勿手改。生成工具：tools/sizeofscan.py",
        "// 数据来源：gamemd.exe 的 `push <size>; call operator new` 与构造函数写虚表的配对。",
        "// 只保留票数 >= %d 的条目（同 size 被多个独立调用点证实）；" % MIN_VOTES,
        "// 另外无条件保留被『字段末端』校准过的条目（证据比投票硬）。",
        "// 完整数据见 db/sizes.json，说明见 docs/sizes.md。",
        "#pragma once",
        "#include <cstdint>",
        "",
        "namespace ra2 {",
        "namespace re {",
        "",
        "struct SizeInfo {",
        "    const char* name;",
        "    uint32_t    size;",
        "    uint8_t     from_new;  // 1 = 来自 new 的分配大小；0 = 来自数组步长（弱）",
        "    uint8_t     votes;",
        "};",
        "",
        "inline constexpr int kSizeCount = %d;" % len(keep),
        "",
        "inline constexpr SizeInfo kSizeTable[kSizeCount] = {",
    ]
    for c, e in keep:
        hl.append("    {%s, %d, %d, %d}," % (
            cpp_str(c), e["sizeof"], 1 if e["evidence"] == "new" else 0, e["votes"]))
    hl += [
        "};",
        "",
        "// 按类名查 sizeof；查不到返回 0",
        "inline uint32_t SizeOf(const char* name) {",
        "    for (int i = 0; i < kSizeCount; ++i) {",
        "        const char* a = kSizeTable[i].name;",
        "        const char* b = name;",
        "        while (*a && *a == *b) { ++a; ++b; }",
        "        if (*a == *b) return kSizeTable[i].size;",
        "    }",
        "    return 0;",
        "}",
        "",
        "}  // namespace re",
        "}  // namespace ra2",
        "",
    ]
    open(os.path.join(ROOT, "src", "re", "ObjectSizes.h"), "w",
         encoding="utf-8", newline="\n").write("\n".join(hl))

    print("命中 %d 个类（new 证据 %d，仅步长证据 %d），写入表 %d 条（票数>=%d）" % (
        len(out), len(results), len(set(strides) - set(results)), len(keep), MIN_VOTES))
    for cls in sorted(out, key=lambda c: -out[c]["sizeof"])[:45]:
        e = out[cls]
        print("  %-34s sizeof=%-6d %-6s 票数 %d" % (
            cls, e["sizeof"], e["evidence"], e["votes"]))


if __name__ == "__main__":
    main()
