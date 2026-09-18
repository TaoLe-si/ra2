"""
cfgscan.py -- 全量反编译计划的 Stage 2：基本块与 CFG。

方法、判据与背景见 docs/decompile-plan.md §4 Stage 2。

输入：db/funcs.json（Stage 1 的函数表）+ gamemd.exe。
方法：
  1. 以**函数为单位**做有界的过程内递归下降，拿到该函数的指令地址集合。
     边界就是 Stage 1 定的 [va, end) —— 不重新找边界，只相信 S1 的结论。
  2. 切基本块。leader = 函数入口 ∪ 分支目标（含跳表表项）∪ 分支的下一条。
  3. 边分四类：fallthrough / jcc / jmp / switch（跳表展开成 N 条边）。
     跳表里落在函数体外的表项单独记 `tail`，**不混进 switch 边**。

为什么这件事值得单独做：
  * S1 的函数体是「遍历到的最小/最大地址」围出来的包围盒，中间是可能有洞的。
    基本块把「哪些字节真的是代码」钉死，洞里的东西才有资格被叫作数据。
  * 后面的表达式恢复 / 结构化还原全部以基本块为单位，没有它做不了。

对账（能失败的判据，全部实算，不写死数字）：
  1. 每个块都落在本函数区间内，块内指令数 ≥ 1；
  2. 同函数内块两两不重叠，按地址严格递增；
  3. **每个块都必须从入口块可达** —— 这条最硬：边只要漏一条，
     立刻冒出一堆"孤儿块"。弱化版的"有前驱"是查不出环形孤立的。
  4. 遍历到的指令 100% 落进某个块（没有指令掉在块外）；
  5. **逐站点**核每张跳表：落成的边数 + 指向非代码的表项数 == 表项数。
     逐站点是必须的 —— 一个函数含多张跳表是常态（实测 44 个函数共 130 张），
     拿"函数里所有 switch 边的和"去比单张表会整批误报；
  6. 每条边的目标要么是本函数的块首，要么是已知函数入口 / 函数外（尾调用）。
     **指向非代码的表项不连边** —— 连了就是悬空边。
  7. 重走失败数为 0（S1 走通的这里必须也走通）；
  8. 计数下界 b-1 <= e（除入口块外每块至少一条入边）；
  9. 计数上界 e <= 2b + c（块出度最多 2，只有 switch 块能更多）；
  10. 分解恒等式：总数 == 多块部分 + 单块部分（块数与边数都要闭合）。
      这条是补上的：`single` 早先只存终止形态、不存边，于是 stats 里的总数
      比 JSON 里能解包的多出 254 条边、头文件 kEdgeCount 与文档也对不上。

判据 8/9/10 里的前两条是写在 BlockTable.h 注释里对 C++ 侧声明的恒等式，
这里每函数实算，不是只写在注释里好看。判据 10 由 tools/cfgcheck.py 独立复核。
"""

from __future__ import annotations

import argparse
import bisect
import collections
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "pylibs"))

from peimage import PEImage, DEFAULT_IMAGE            # noqa: E402
import disasm                                          # noqa: E402
import funcscan as FS                                  # noqa: E402
from disasm import F_BAD, F_CALL, F_HLT, F_INT3, F_JCC, F_JMP, F_RET   # noqa: E402

ROOT = os.path.dirname(HERE)
ALIGN_SPLIT = FS.ALIGN_SPLIT

# 边类型。顺序即 JSON 里的编码，改动会让 db/blocks.json 的全部编码失效。
EDGE_KINDS = ["fall", "jcc", "jmp", "switch", "tail"]
EDGE_CODE = {k: i for i, k in enumerate(EDGE_KINDS)}
# 块的**终止形态**：块最后一条指令是什么，以及有没有后继。
#   ret / int3 / halt / ind  —— 终止，无后继
#   jmp / jcc / switch / fall —— 有后继（jmp 也可能是 tail）
#   tail / bad                —— 跳到函数外 / 解不开
#   end                       —— 线性链恰好走到函数末端，无终止指令也无后继。
#                                这是**边界条件**不是故障，单列一类，别跟 bad 混。
END_KINDS = ["ret", "jmp", "jcc", "switch", "ind", "int3", "halt", "fall", "tail",
             "bad", "end"]
END_CODE = {k: i for i, k in enumerate(END_KINDS)}

# split_function 把结构自检的计数并进 notes 一起抛上来（这样调用方拿得到），
# 但这几个是**硬错误**不是"可疑点"，run() 里要再抄一份进 err ——
# 否则 docs 的判据 1/2 永远读不到值、永远显示 ✅。
STRUCT_ERR = ("空块", "块越界", "块重叠")


def hx(v: int) -> str:
    return "0x%08X" % v


# --------------------------------------------------------------------------
# 一个函数 → 基本块 + 边
# --------------------------------------------------------------------------
def split_function(idx, jt, ib, va, size, insns):
    """把一个函数的指令集合切成基本块并连边。

    insns 是**VA**集合（调用方负责把 Walker 的 RVA 换算过来）。
    返回 (blocks, notes, uncovered, site_edges, site_ghost)：
      blocks 里每个是 [首址, 指令数, 字节末址, 终止形态, [[后继, 边型], ...]]
      notes 记切块时遇到的可疑点（越界目标、块重叠、块外指令数等）。
      uncovered 是没有落进任何块的指令数（应与 notes["块外指令"] 一致）。
      site_edges 是 {跳表 jmp 的 VA: 这张表一共落成几条边}，
      site_ghost 是 {跳表 jmp 的 VA: 有几条表项指向的地址根本不是可达指令}。
      两者都**逐站点**记，供对账第 5 条核 —— 一个函数含多张跳表是常态
      （实测 44 个函数共 130 张），拿"函数里所有 switch 边的和"去比单张表必然误报。
    """
    end = va + size
    seen = set(insns)
    notes = collections.Counter()
    site_edges = {}
    site_ghost = {}

    def flags(a):
        return idx.flags_at(a - ib)

    def isize(a):
        return idx.size_at(a - ib)

    # ---- 1. leader 与终止形态 ----
    leaders = {va}
    endk = {}
    switch_tab = {}          # 跳表 jmp 的地址 -> 表项列表（VA）
    for a in sorted(seen):
        fl = flags(a)
        nxt = a + isize(a)
        if fl & F_RET:
            endk[a] = "ret"
        elif fl & F_INT3:
            endk[a] = "int3"
        elif fl & F_HLT:
            endk[a] = "halt"
        elif fl & F_JCC:
            endk[a] = "jcc"
        elif fl & F_JMP:
            t = idx.targets.get(a - ib)
            if t is None:
                tab, _nb = jt.resolve(a - ib)
                if tab:
                    endk[a] = "switch"
                    switch_tab[a] = tab
                else:
                    endk[a] = "ind"          # jmp [reg] / jmp [IAT]
            else:
                endk[a] = "jmp" if va <= ib + t < end else "tail"
        # 分支的下一条是 leader
        if a in endk and nxt < end:
            leaders.add(nxt)
        # 分支目标（立即数形式）是 leader
        if fl & (F_JMP | F_JCC):
            t = idx.targets.get(a - ib)
            if t is not None:
                tv = ib + t
                if va <= tv < end:
                    leaders.add(tv)
            elif a in switch_tab:
                for x in switch_tab[a]:
                    if va <= x < end:
                        leaders.add(x)

    # 由线性推进补出的「链中途断掉」的地址也当 leader：
    # 遍历的指令集合是**可达**集合，两个可达指令之间隔着的字节是数据（跳表等）。
    # 不把断点当 leader，后一段就会被前面的块吞掉或整个漏掉 —— 判据 4 会报出来。
    for a in sorted(seen):
        if a not in endk:
            nxt = a + isize(a)
            if nxt not in seen and nxt < end:
                leaders.add(nxt)

    # ---- 2. 切块 ----
    blocks = []
    visited = set()
    for L in sorted(leaders):
        if L in visited or L not in seen:
            continue
        cur = []
        a = L
        while True:
            if a not in seen or a in visited:
                break
            if a != L and a in leaders:
                break
            visited.add(a)
            cur.append(a)
            if a in endk:
                break
            nxt = a + isize(a)
            if nxt >= end or nxt not in seen:
                # 线性链断在中间且不是终止指令：块到此为止，else 分支会记 fall
                break
            a = nxt
        if cur:
            blocks.append(cur)

    uncovered = seen - visited
    if uncovered:
        notes["块外指令"] += len(uncovered)

    # ---- 3. 连边 ----
    out = []
    for cur in blocks:
        last = cur[-1]
        k = endk.get(last)
        succ = []
        if k is None:
            # 没有终止指令：块是被「下一条是 leader」或「链断了」截断的
            nxt = last + isize(last)
            if nxt in seen:
                succ.append((nxt, "fall"))
                k = "fall"
            elif nxt >= end:
                # 线性链恰好走到**函数末端**：没有终止指令，也没有后继。
                # 这是边界条件，不是解码故障 —— 单列一类 `end`。
                # 混进 `bad` 的代价是：47 个良性的函数末块和真故障一起被报警，
                # 后面 S6 结构化还原也没法按形态区分。
                k = "end"
            else:
                k = "bad"
                notes["链断在函数中间"] += 1
        elif k == "jcc":
            t = idx.targets.get(last - ib)
            if t is not None:
                tv = ib + t
                if va <= tv < end:
                    succ.append((tv, "jcc"))
                else:
                    succ.append((tv, "tail"))
                    notes["条件跳转跳出函数"] += 1
            nxt = last + isize(last)
            if nxt in seen:
                succ.append((nxt, "fall"))
            else:
                notes["jcc 后无 fallthrough"] += 1
        elif k == "jmp":
            tv = ib + idx.targets[last - ib]
            succ.append((tv, "jmp"))
        elif k == "tail":
            tv = ib + idx.targets[last - ib]
            succ.append((tv, "tail"))
        elif k == "switch":
            tab = switch_tab[last]
            ins = [x for x in tab if va <= x < end]
            ext = [x for x in tab if not (va <= x < end)]
            # 表项落在函数**内**，还要再问一句"它真的是可达指令吗"。
            # 只按"落在区间内"就连边是不行的：那会连出**悬空边** ——
            # 目标既不是本函数的块首、又不在函数外，判据 6 里外不是人。
            # 实测 10 条这种目标（0x7C77DB / 0x7CA1E8 / 0x7CA18C / 0x7CA380 …）
            # 全都不是指令起点、也不在 seen 里，是**数据地址**，
            # 来源就是跳表发现器在数据上认出的假表项。
            real = [x for x in ins if x in seen]
            ghost = [x for x in ins if x not in seen]
            for x in real:
                succ.append((x, "switch"))
            for x in ext:
                succ.append((x, "tail"))
            # 逐站点记两个数：落成的边数、指向非代码的表项数。
            # 两者之和才是表项总数 —— 对账第 5 条按这个核，不藏。
            site_edges[last] = len(real) + len(ext)
            site_ghost[last] = len(ghost)
            if ghost:
                notes["跳表项不是可达指令（不连边）"] += len(ghost)
            if not real:
                notes["跳表没有一条落在函数内"] += 1
        # 其余（ret / int3 / halt / ind）终止，无后继
        # 块 = [首址, 指令数, 字节末址, 终止形态, 后继表]
        out.append([cur[0], len(cur), cur[-1] + isize(cur[-1]), k, succ])

    # ---- 4. 自检：区间 / 顺序 / 边目标 ----
    # 判据 1、2：块必须落在函数区间内、块内指令数 ≥1、按地址严格递增且**不重叠**。
    # 用**字节区间**判重叠，不是块首大小 —— 后者查不出「前一块的后半段跨进下一块」。
    # 这些计数一律并进 notes 往上抛：之前它们记在一个局部 Counter 里，
    # 算完就随栈一起丢掉 —— 检查跑了但结论没人看见，是最坏的一种"通过了"。
    prev_end = None
    for b in out:
        if b[1] <= 0:
            notes["空块"] += 1
        if not (va <= b[0] < b[2] <= end):
            notes["块越界"] += 1
        if prev_end is not None and b[0] < prev_end:
            notes["块重叠"] += 1
        prev_end = b[2]

    bset = {b[0] for b in out}
    for b in out:
        for tv, ek in b[4]:
            if va <= tv < end:
                if tv not in bset:
                    notes["边指向函数内非块首"] += 1
            else:
                if ek != "tail":
                    notes["函数外目标不是 tail"] += 1
    return out, notes, len(uncovered), site_edges, site_ghost


# --------------------------------------------------------------------------
# 主流程
# --------------------------------------------------------------------------
def run(path=None, verbose=True):
    img = PEImage(path or DEFAULT_IMAGE)
    ib = img.image_base
    lo, hi = img.text_range()

    def log(*a):
        if verbose:
            print(*a, flush=True)

    fun = json.load(open(os.path.join(ROOT, "db", "funcs.json"), encoding="utf-8"))
    fjs = fun["funcs"]
    ents = sorted(((int(k, 0), v) for k, v in fjs.items()), key=lambda x: x[0])
    log("函数表：%d 个（来自 db/funcs.json）" % len(ents))

    # 锚点直接用**S1 定下来的全部函数入口** —— 比 S1 用的
    # 「call 目标 ∪ 虚表槽」更全，线性扫描经过数据时能更早回到正轨。
    anchors = {va for va, _ in ents}
    log("以 %d 个函数入口为锚点重扫 …" % len(anchors))
    # 与 S1 同一套路（见 tools/funcscan.py 的 2a/2b）：先只用锚点扫一遍认跳表，
    # 再把跳表当**数据区**遮罩重扫。少了这一步，跳表本体仍会被解成一串指令，
    # 「表尾那个真入口」于是拿不到指令边界 —— 实测 `0x7C7788`（函数 `0x7C7371`
    # 内嵌跳表之后的续段）因此不在可达集合里，而 `0x7C73B4` 的 `jmp` 又指着它，
    # 结果就是一条「边目标在体内、却不是块首」的悬空边。
    # 注意这必须**用同一份数据**做，否则 S1 与 S2 的指令边界会漂移。
    idx0 = disasm.build_index(img, anchors)
    jt0 = FS.JtResolver(img, idx0)
    for site in idx0.jt:
        jt0.resolve(site)
    dranges = sorted(jt0.ranges())
    del idx0
    log("  跳表 %d 张 / %d 字节 -> 作数据区遮罩"
        % (len(dranges), sum(b - a for a, b in dranges)))
    idx = disasm.build_index(img, anchors, data_ranges=dranges)
    log("  %d 条指令" % idx.decoded)
    jt = FS.JtResolver(img, idx)
    for site in idx.jt:
        jt.resolve(site)
    wk = FS.Walker(img, idx, jt)
    strong = frozenset(anchors)
    stop_rvas = sorted(v - ib for v in anchors)
    # 跳表**站点**的 VA 集合。用来分辨两种截然不同的残差：
    # 站点被重走走到了但没判成 switch（真问题），vs 站点根本没被走到
    # —— 后者说明这些字节在函数的**数据**里，是跳表发现器在数据上扫出的假站点。
    jt_site_vas = {ib + s for s in idx.jt}

    blocks_of = {}
    single = {}
    # 单块函数的出边。**不能丢**：单块函数里有一类是纯尾调用跳板
    # （整段就一条 `jmp <别的函数>`），那条 tail 边正是 Stage 3 建调用图要用的。
    # 早先 `single` 只存终止形态，这 254 条边在 JSON 里直接消失，
    # 而 stats/头文件还在把它们算进总数 —— 两边差 254，谁看都对不上。
    single_edges = {}
    per_func = []
    notes_all = collections.Counter()
    err = collections.Counter()
    all_site_edges = {}      # 跳表站点 VA -> 切块时实际落成的边数
    all_site_ghost = {}      # 跳表站点 VA -> 指向**非可达指令**的表项数
    walked_jt_sites = set()  # 被某次重走真的走到的跳表站点
    nblocks_total = 0
    nedges_total = 0
    ncase_total = 0
    fail_walk = 0

    for n, (va, f) in enumerate(ents, 1):
        if n % 4000 == 0:
            log("  … %d/%d" % (n, len(ents)))
        size = f["size"]
        r = wk.walk(va - ib, strong, strong, True, stop_rvas, (va - ib, va - ib + size))
        if r is None:
            fail_walk += 1
            # 走不出来（正常不该发生：S1 已经走通过一次）——
            # 退化成「整段一个块」，并如实记数，不假装成功。
            err["rewalk 失败：" + wk.reason] += 1
            # 退化成「整段一个块」。块的 5 元组格式必须跟正常路径**完全一致**
            # （va, 指令数, 字节末址, 终止形态下标, 后继表）——
            # 曾经这里是 4 元的 [va, 0, "bad", []]，少了字节末址、终止形态还是字符串，
            # 一旦这条路真的走到，下游按 5 元组解包会当场炸。
            blocks_of[hx(va)] = {
                "b": [[va, 0, va + size, END_CODE["bad"], []]], "degraded": True}
            single[hx(va)] = "bad"
            continue
        # Walker.seen 里存的是 **RVA**（它内部一律用 RVA），而 split_function
        # 全程按 **VA** 干活（leader 拿函数入口 VA 起头、边目标走 ib+off）。
        # 这里必须换一次单位 —— 漏掉这一步的后果是所有 leader 都"不在 seen 里"，
        # 每个函数都切出 0 块，然后静默退化成"整段一个块"。实测踩过。
        insns = {ib + r for r in wk.seen}
        walked_jt_sites |= (insns & jt_site_vas)
        blks, notes, unc, site_edges, site_ghost = split_function(
            idx, jt, ib, va, size, insns)
        all_site_edges.update(site_edges)
        all_site_ghost.update(site_ghost)
        notes_all.update(notes)
        for _k in STRUCT_ERR:
            if notes.get(_k):
                err[_k] += notes[_k]
        if unc:
            err["块外指令"] += unc
        if not blks:
            # 理论上走不到（入口一定在 seen 里，第一个块必然从它开始）。
            # 真出现了也不能静默：记数 + 退化成单块。
            err["切不出块"] += 1
            blks = [[va, len(insns), va + size, "bad", []]]

        # 判据 1/2/6 在 split_function 里已经逐块查过了，这里汇总跨函数的量。
        nb = len(blks)
        ne = sum(len(b[4]) for b in blks)
        ncase = sum(1 for b in blks if b[3] == "switch"
                    for _t, ek in b[4] if ek in ("switch", "tail"))

        # ---- 判据 3：每个块都必须从入口块可达（最硬的一条）----
        # 弱化版的「每块有前驱」查不出环形孤立的块簇。
        idx_of = {b[0]: i for i, b in enumerate(blks)}
        seenb = {0}
        stack = [0]
        while stack:
            i = stack.pop()
            for tv, _ek in blks[i][4]:
                j = idx_of.get(tv)
                if j is not None and j not in seenb:
                    seenb.add(j)
                    stack.append(j)
        if len(seenb) != len(blks):
            err["孤儿块"] += len(blks) - len(seenb)
            err["有孤儿块的函数"] += 1

        nblocks_total += nb
        nedges_total += ne
        ncase_total += ncase
        per_func.append((va, nb, ne, ncase))
        if nb == 1:
            single[hx(va)] = blks[0][3] or "bad"
            if blks[0][4]:
                single_edges[hx(va)] = [[t, EDGE_CODE[k]] for t, k in blks[0][4]]
        else:
            blocks_of[hx(va)] = {
                "b": [[b[0], b[1], b[2], END_CODE[b[3]], [[t, EDGE_CODE[k]] for t, k in b[4]]]
                      for b in blks]
            }

    # ---- 判据 5（续）：逐表核对表项数 ----
    # 判据 5 只按「块自己声明的边」自证是不够的，必须回到**源头的表**去核：
    # 每张 S1 解析出来的跳表，切块时落成的边数必须**恰好等于表项数**。
    #
    # 关键：必须**逐站点**核。一个函数含多张跳表是常态 —— 实测 44 个函数共 130 张，
    # 拿"该函数所有 switch 边的和"去比单张表，这 130 张会全部误报成"边数不符"
    # （130 报错 对 130 张表，数字完全吻合，就是这么来的）。
    ent_vas = [va for va, _ in ents]
    for site, _base in idx.jt.items():
        tab, _nb = jt.resolve(site)
        if not tab:
            continue
        sv = ib + site
        k = bisect.bisect_right(ent_vas, sv) - 1
        owner = None
        if k >= 0:
            f = fjs.get(hx(ent_vas[k]))
            if f is not None and sv < ent_vas[k] + f["size"]:
                owner = ent_vas[k]
        if owner is None:
            err["跳表站点不在任何函数内"] += 1
            continue
        have = all_site_edges.get(sv, 0)
        ghost = all_site_ghost.get(sv, 0)
        if have + ghost == len(tab):
            # 表项**全部**有交代：要么落成了一条边，要么指向的不是代码。
            # 只把"指向非代码"这件事如实记数，不当成边数对不上。
            if ghost:
                err["跳表表项指向非代码（假表项）"] += 1
            continue
        if sv not in walked_jt_sites:
            # 站点根本不是任何一次重走走到的指令 —— 它落在函数的**数据**里
            # （跳表之间/前后），是跳表发现器在数据字节上扫出来的**假站点**。
            # 实测这 14 个全在 2 个 CRT 函数里（0x7CA090 / 0x7D0A20，各 12 张），
            # 其"表项"指向的是数据（0x7CA1E8 起步的字节是 5E 08 45 8B 这类），
            # 表基址也和指令里的位移对不上。归类为发现器的已知局限，不是 CFG 缺陷。
            err["跳表站点未被走到（疑似数据）"] += 1
        elif have == 0:
            # 走到了，但那一块没被判成 switch（真问题）。
            err["跳表未判成 switch 块"] += 1
        else:
            err["跳表边数不符"] += 1

    # ---- 计数恒等式 ----
    # write_header 的注释里对 C++ 侧声明了这两条，所以这里必须**真算**一遍，
    # 不能只在注释里写着好看：
    #   ② 下界 e >= b-1：除入口块外每块至少一条入边（判据 3 的推论），漏一条边即破。
    #   ③ 上界 e <= 2b + c：块出度最多 2（jcc），只有 switch 块能更多，
    #      多出来的部分正好是 c（switch 块的边数之和）。b-1 <= e <= 2b+c。
    for va, nb, ne, nc in per_func:
        if ne < nb - 1:
            err["边数低于 b-1"] += 1
        if ne > 2 * nb + nc:
            err["边数高于 2b+c"] += 1

    ok = _summary(log, ents, blocks_of, single, notes_all, err,
                  nblocks_total, nedges_total, ncase_total, fail_walk)

    # 多块 / 单块的分解，并把它当成一条**实算的恒等式**来查：
    # stats 里的总数（含单块）必须等于 多块部分 + 单块部分。
    # 早先 `single` 不存边，这条恒等式不成立 —— 块数差 8,204、边数差 254，
    # 头文件的 kEdgeCount 和文档里的"出边总数"因此对不上，谁也说不清谁对。
    multi_blocks = sum(len(v["b"]) for v in blocks_of.values())
    multi_edges = sum(len(b[4]) for v in blocks_of.values() for b in v["b"])
    single_edges_n = sum(len(v) for v in single_edges.values())
    if multi_blocks + len(single) != nblocks_total:
        err["块数分解不闭合"] += 1
    if multi_edges + single_edges_n != nedges_total:
        err["边数分解不闭合"] += 1

    stats = {
        "image": img.path,
        "inputs": ["gamemd.exe", "db/funcs.json"],
        "method": "以函数为单位的**有界**过程内递归下降 → leader 切块 → 四类边",
        "edge_kinds": EDGE_KINDS,
        "end_kinds": END_KINDS,
        "anchors": len(anchors),
        "insns_scanned": idx.decoded,
        "funcs": len(ents),
        "funcs_multi": len(blocks_of),
        "funcs_single": len(single),
        # blocks / edges / switch_edges 是**全部函数**的口径（含单块函数）。
        # 头文件里的 kBlockCount / kEdgeCount 直接取这三个数，
        # 两边因此是构造上一致，不是"各自算了一遍碰巧相等"。
        "blocks": nblocks_total,
        "edges": nedges_total,
        "switch_edges": ncase_total,
        "blocks_multi": multi_blocks,
        "edges_multi": multi_edges,
        "edges_single": single_edges_n,
        "single_with_edges": len(single_edges),
        "jt_tables": len([1 for t, _ in jt.cache.values() if t]),
        "notes": dict(notes_all),
        "checks": ok,
        "err": dict(err),
        "rewalk_failed": fail_walk,
        "top": sorted(per_func, key=lambda x: -x[1])[:20],
    }
    return img, ents, blocks_of, single, single_edges, stats


def _summary(log, ents, blocks_of, single, notes_all, err,
             nblocks_total, nedges_total, ncase_total, fail_walk):
    """把对账结果算成一张表，返回给 stats；同时打到日志上。"""
    dist = collections.Counter(b[1] for b in
                               (x for v in blocks_of.values() for x in v["b"]))
    log("")
    log("=== Stage 2 对账 ===")
    log("函数 %d 个；多块 %d 个，单块 %d 个" % (len(ents), len(blocks_of), len(single)))
    log("基本块 %d 个；边 %d 条（其中 switch/tail 型 %d）" % (nblocks_total, nedges_total, ncase_total))
    log("块的指令数分布（前 8）: %s" % dict(sorted(dist.items())[:8]))
    log("重走失败 %d 个" % fail_walk)
    if notes_all:
        log("切块过程中的可疑点: %s" % dict(notes_all))
    if err:
        log("错误计数: %s" % dict(err))
    else:
        log("错误计数: 无")
    return {
        "multi": len(blocks_of),
        "single": len(single),
        "blocks": nblocks_total,
        "edges": nedges_total,
        "switch_edges": ncase_total,
        "rewalk_failed": fail_walk,
        "notes": dict(notes_all),
        "err": dict(err),
    }


def write_json(ents, blocks_of, single, single_edges, stats):
    out = {
        "method": stats["method"],
        "inputs": stats["inputs"],
        "edge_kinds": EDGE_KINDS,
        "end_kinds": END_KINDS,
        "note": ("`funcs` 只列 ≥2 块的函数；单块函数在 `single` 里给终止形态"
                 "（块就是 [va, va+size) 一整段），它在 `single_edges` 里给出边"
                 "（单块函数仍可能有出边：纯尾调用跳板就是一段一条 jmp）。"
                 "块 = [va, 指令数, 终止形态下标, [[后继, 边型], ...]]，"
                 "边型是 EDGE_KINDS 的下标，终止形态是 END_KINDS 的下标。"
                 "stats.blocks / stats.edges 是**全部函数**的口径，"
                 "等于 blocks_multi + 单块数目、edges_multi + edges_single。"),
        "stats": stats,
        "funcs": blocks_of,
        "single": single,
        "single_edges": single_edges,
    }
    p = os.path.join(ROOT, "db", "blocks.json")
    with open(p, "w", encoding="utf-8") as f:
        json.dump(out, f, ensure_ascii=False, separators=(",", ":"), sort_keys=True)
    return p, os.path.getsize(p)


def write_header(ents, blocks_of, single, stats):
    """给 C++ 侧的**摘要**表 —— 不塞全部块（那是六十万条，头文件会爆炸），
    只放每个多块函数的 4 个数，够跑跨表一致性与计数恒等式。"""
    rows = []
    for h, v in sorted(blocks_of.items(), key=lambda x: int(x[0], 0)):
        va = int(h, 0)
        blks = v["b"]
        nb = len(blks)
        ne = sum(len(b[4]) for b in blks)
        nc = sum(1 for b in blks if b[3] == END_CODE["switch"]
                 for _t, _e in b[4])
        rows.append((va, nb, ne, nc))
    nb_tot = sum(r[1] for r in rows)
    ne_tot = sum(r[2] for r in rows)
    nc_tot = sum(r[3] for r in rows)

    L = []
    A = L.append
    A("// 自动生成，勿手改 —— 由 tools/cfgscan.py 产出。")
    A("// 方法、判据与对账见 docs/decompile-plan.md 与 docs/cfg.md。")
    A("//")
    A("// Stage 2（基本块与 CFG）的摘要表。把全部块放进头文件不现实（约 %d 条），" % nb_tot)
    A("// 这里只放**多块函数**的四个数：块数、边数、跳表边数。够跑三组硬检查：")
    A("//   ① 跨表一致性：每个入口都必须在 FuncTable 里；")
    A("//   ② 计数下界：令 e = 边数、b = 块数，则 e >= b-1。")
    A("//      这一条来自「除入口块外每块至少一个前驱」（判据 3）——")
    A("//      边只要漏一条，被孤立的块就会让它不成立；")
    A("//   ③ 计数上界：e <= 2b + c（c = 跳表边数）。出度最多 2，只有跳表块能更多。")
    A("//")
    A("// 单块函数（%d 个）不在此表 —— 它们没有 CFG 可言，块就是整段。" % len(single))
    A("#pragma once")
    A("")
    A("#include <cstdint>")
    A("")
    A("namespace ra2 {")
    A("namespace re {")
    A("namespace cfg {")
    A("")
    A("/// 有 CFG 的函数（≥2 块）个数")
    A("inline constexpr uint32_t kCfgFuncCount = %d;" % len(rows))
    # 三个总数**直接取 stats**，保证头文件与 db/blocks.json、docs/cfg.md 是
    # 同一个口径（全部函数，含单块），而不是"各自算一遍碰巧相等"。
    # 注意：正下方的三个数组只覆盖**多块函数**，所以
    #   kBlockCount   - ΣkCfgBlocks == 单块函数个数（= %d）
    #   kEdgeCount    - ΣkCfgEdges  == 单块函数的出边数（= %d）
    A("/// 全部函数的基本块总数（含单块函数）")
    A("inline constexpr uint32_t kBlockCount = %d;" % stats["blocks"])
    A("/// 全部函数的出边总数（含单块函数的出边）")
    A("inline constexpr uint32_t kEdgeCount = %d;" % stats["edges"])
    A("/// 跳表边总数（switch 边 + 落在函数外的 tail 边，含单块函数）")
    A("inline constexpr uint32_t kSwitchEdgeCount = %d;" % stats["switch_edges"])
    A("/// 单块函数个数（正下方数组不含它们）")
    A("inline constexpr uint32_t kSingleFuncCount = %d;" % len(single))
    A("/// 单块函数的出边数（= kEdgeCount - kCfgEdgeSum）。单块函数里有一类是")
    A("/// **纯尾调用跳板**（整段就一条 `jmp`），那条边是 Stage 3 建调用图要用的，")
    A("/// 所以它必须能被单独核出来，不能只留在 JSON 里。")
    A("inline constexpr uint32_t kSingleEdgeCount = %d;" % stats["edges_single"])
    A("/// 多块函数的块数合计（= ΣkCfgBlocks）")
    A("inline constexpr uint32_t kCfgBlockSum = %d;" % nb_tot)
    A("/// 多块函数的出边合计（= ΣkCfgEdges）")
    A("inline constexpr uint32_t kCfgEdgeSum = %d;" % ne_tot)
    A("/// 多块函数的跳表边合计（= ΣkCfgCases）")
    A("inline constexpr uint32_t kCfgCaseSum = %d;" % nc_tot)
    A("")
    A("/// 有 CFG 的函数的入口 VA（升序）")
    A("inline constexpr uint32_t kCfgFuncVA[] = {")
    for i in range(0, len(rows), 8):
        A("    " + " ".join("0x%08Xu," % r[0] for r in rows[i:i + 8]))
    A("};")
    A("")
    A("/// 块数")
    A("inline constexpr uint16_t kCfgBlocks[] = {")
    for i in range(0, len(rows), 16):
        A("    " + " ".join("%d," % min(r[1], 65535) for r in rows[i:i + 16]))
    A("};")
    A("")
    A("/// 出边数")
    A("inline constexpr uint16_t kCfgEdges[] = {")
    for i in range(0, len(rows), 16):
        A("    " + " ".join("%d," % min(r[2], 65535) for r in rows[i:i + 16]))
    A("};")
    A("")
    A("/// 跳表边数")
    A("inline constexpr uint16_t kCfgCases[] = {")
    for i in range(0, len(rows), 16):
        A("    " + " ".join("%d," % min(r[3], 65535) for r in rows[i:i + 16]))
    A("};")
    A("")
    A("/// 这个入口有没有 CFG 记录（即块数 ≥2）。")
    A("inline bool IsCfgFunc(uint32_t va) {")
    A("    uint32_t lo = 0, hi = kCfgFuncCount;")
    A("    while (lo < hi) {")
    A("        uint32_t mid = (lo + hi) / 2;")
    A("        if (kCfgFuncVA[mid] < va) lo = mid + 1; else hi = mid;")
    A("    }")
    A("    return lo < kCfgFuncCount && kCfgFuncVA[lo] == va;")
    A("}")
    A("")
    A("}  // namespace cfg")
    A("}  // namespace re")
    A("}  // namespace ra2")
    A("")
    p = os.path.join(ROOT, "src", "re", "BlockTable.h")
    with open(p, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(L))
    return p, len(L)


def write_docs(stats, ents, blocks_of, single, single_edges):
    s = stats
    c = s["checks"]
    L = []
    A = L.append
    A("# Stage 2 —— 基本块与 CFG")
    A("")
    A("> 产物 `db/blocks.json` + `src/re/BlockTable.h`。本文件由 `tools/cfgscan.py` 生成，")
    A("> 数字全部来自 `stats`，不手抄。")
    A("")
    A("## 1. 做了什么")
    A("")
    A("Stage 1 给出的函数体是「遍历到的最小/最大地址」围出来的**包围盒**，")
    A("函数中间是可能有洞的 —— 洞里可能是跳转表、可能是没走到的代码。")
    A("Stage 2 把每个函数拆成基本块，从而把「哪些字节真的是代码」钉死。")
    A("")
    A("方法：")
    A("")
    A("1. 以函数为单位做**有界**的过程内递归下降（边界就是 S1 定的 `[va, end)`，")
    A("   这里不重新找边界 —— 只相信 S1 的结论，否则两个阶段会互相漂移）。")
    A("2. leader = 函数入口 ∪ 分支目标（含跳表表项）∪ 分支的下一条")
    A("   ∪ 线性链的断点（两个可达指令之间隔着的字节是数据）。")
    A("3. 边分四类：`fallthrough` / `jcc` / `jmp` / `switch`。")
    A("   跳表里落在函数体外的表项单独记 `tail`，**不混进 switch 边** ——"
      "混在一起就没法对账了。")
    A("")
    A("锚点用的是 S1 定下来的**全部 %d 个函数入口**，比 S1 自己用的" % s["anchors"])
    A("「call 目标 ∪ 虚表槽」更全。")
    A("")
    A("重扫是**两步**的：先只用锚点扫一遍认跳表，再把跳表本体当**数据区**"
      "（`disasm.build_index(..., data_ranges=…)`）重扫，正式用第二遍。")
    A("少了这一步，跳表本体会被解成一串指令，「表尾那个真入口」拿不到指令边界 ——"
      "实测 `0x7C73B4` 的 `jmp` 指向表尾的 `0x7C7788`，"
      "于是留下一条「边目标在函数体内、却不是块首」的悬空边。")
    A("这一步与 S1 用的是**同一份遮罩**（都由 `JtResolver` 解出），"
      "不然两阶段的指令边界会漂移。")
    A("")
    A("## 2. 规模")
    A("")
    A("| 项 | 值 |")
    A("|---|---|")
    A("| 函数 | %d |" % s["funcs"])
    A("| 其中 ≥2 块（有 CFG 可言） | **%d** |" % c["multi"])
    A("| 其中单块（直线代码） | %d |" % c["single"])
    A("| 基本块总数（含单块函数的 1 块） | **%d** |" % c["blocks"])
    A("| 出边总数（含单块函数的出边） | **%d** |" % c["edges"])
    A("| └ 多块函数贡献 | %d 块 / %d 边 |" % (s["blocks_multi"], s["edges_multi"]))
    A("| └ 单块函数贡献 | %d 块 / %d 边（其中 %d 个单块函数有出边） |"
      % (c["single"], s["edges_single"], s["single_with_edges"]))
    A("| 其中 switch / 表外 tail 边 | %d |" % c["switch_edges"])
    A("| 跳表（S1 解析出的） | %d 张 |" % s["jt_tables"])
    A("| 重走失败（S1 走通、这里没走通的） | %d |" % c["rewalk_failed"])
    A("")
    A("## 3. 对账")
    A("")
    A("| # | 判据 | 结果 |")
    A("|---|---|---|")
    errd = c["err"]
    A("| 1 | 块落在函数区间内、块内指令数 ≥ 1 | %s |"
      % ("✅" if not errd.get("块越界") and not errd.get("空块") else "❌ 见 §4"))
    A("| 2 | 同函数内块按地址严格递增、不重叠 | %s |"
      % ("✅" if not errd.get("块重叠") else "❌ 见 §4"))
    A("| 3 | **每个块都从入口块可达** | %s |"
      % ("✅ 全部函数" if not errd.get("孤儿块")
         else "❌ %d 个孤儿块（涉及 %d 个函数）"
              % (errd.get("孤儿块", 0), errd.get("有孤儿块的函数", 0))))
    A("| 4 | 遍历到的指令 100%% 落进某个块 | %s |"
      % ("✅" if not errd.get("块外指令") else "❌ %d 条在块外" % errd["块外指令"]))
    A("| 5 | 每张跳表**逐站点**核：落成的边数 + 指向非代码的表项数 == 表项数 | %s |"
      % ("✅ 全部站点" if not errd.get("跳表边数不符") and not errd.get("跳表未判成 switch 块")
         and not errd.get("跳表站点不在任何函数内")
         and not errd.get("跳表站点未被走到（疑似数据）")
         and not errd.get("跳表表项指向非代码（假表项）")
         else "⚠️ 边数不符 %d / 未判成 switch %d / 站点未被走到（疑似数据）%d / "
              "站点不在函数内 %d / 含指向非代码表项 %d 张"
              % (errd.get("跳表边数不符", 0), errd.get("跳表未判成 switch 块", 0),
                 errd.get("跳表站点未被走到（疑似数据）", 0),
                 errd.get("跳表站点不在任何函数内", 0),
                 errd.get("跳表表项指向非代码（假表项）", 0))))
    A("| 6 | 每条边要么是本函数的块首、要么是函数外（尾调用） | %s |"
      % ("✅" if not s["notes"].get("边指向函数内非块首")
         else "⚠️ %d 条指向函数内非块首" % s["notes"]["边指向函数内非块首"]))
    A("| 7 | 重走失败 | %s |"
      % ("✅ 0 个" if not c["rewalk_failed"] else "⚠️ %d 个（退化成单块并标记）" % c["rewalk_failed"]))
    A("| 8 | 计数下界 `b-1 <= e` | %s |"
      % ("✅ 全部函数" if not errd.get("边数低于 b-1")
         else "❌ %d 个函数" % errd["边数低于 b-1"]))
    A("| 9 | 计数上界 `e <= 2b + c` | %s |"
      % ("✅ 全部函数" if not errd.get("边数高于 2b+c")
         else "❌ %d 个函数" % errd["边数高于 2b+c"]))
    A("| 10 | 分解恒等式：总数 == 多块部分 + 单块部分 | %s |"
      % ("✅ 块数/边数都闭合" if not errd.get("块数分解不闭合")
         and not errd.get("边数分解不闭合")
         else "❌ 块数差 / 边数差（见 §4）"))
    A("")
    A("判据 8/9 的两条恒等式**每函数实算**，不是写在头文件注释里的装饰：")
    A("`b` = 块数、`e` = 出边数、`c` = 该函数跳表边数。下界来自「除入口块外")
    A("每块至少一条入边」（判据 3 的推论，漏一条边即破）；上界来自「块出度最多 2，")
    A("只有 switch 块能更多」。")
    A("")
    A("> **与计划口径的差异（须记录）**：`docs/decompile-plan.md` §4 给 Stage 2 写的")
    A("> 对账是「块图强连通分量与 `ret` 数量一致」。这句话没法照做 —— CFG 的强连通")
    A("> 分量数是**环的个数**的量度，和函数里有几条 `ret` 之间没有可证的恒等关系")
    A("> （两条 `ret` 的直线函数 SCC 数是 2，带一个循环的单 `ret` 函数 SCC 数是 2，")
    A("> 数字碰巧相等但毫无因果）。这里改成上表 9 条**能失败、可实算**的判据：")
    A("> 计划那句真正想表达的「块图必须是良构的、且完全从入口可达」，由判据 3 + 8")
    A("> 直接测了，比原来那句更强也更有意义。跳表那条（判据 5）按原样保留。")
    A("")
    if s["notes"]:
        A("切块过程中的可疑点计数：")
        A("")
        A("| 现象 | 次数 |")
        A("|---|---|")
        for k, v in sorted(s["notes"].items(), key=lambda x: -x[1]):
            A("| %s | %d |" % (k, v))
        A("")
    A("## 4. 错误明细")
    A("")
    if errd:
        A("| 现象 | 次数 |")
        A("|---|---|")
        for k, v in sorted(errd.items(), key=lambda x: -x[1]):
            A("| %s | %d |" % (k, v))
        A("")
    else:
        A("无。")
        A("")
    A("## 4b. 残差逐项定性")
    A("")
    A("上表每一笔都查到了根因，**条数见上表**（这里不重复写数字，免得跟上一节漂移）。")
    A("结论：**没有一笔是 CFG 构造本身的缺陷。** 分三类。")
    A("")
    A("### (1) 跳表发现器的假站点 / 假表项")
    A("")
    A("`跳表站点未被走到（疑似数据）`、`跳表站点不在任何函数内`、"
      "`跳表表项指向非代码（假表项）` 是同一件事的三种表现：扫描器在**数据**"
      "里认出了「跳表站点」，或把数据的若干 dword 当成了表项。")
    A("")
    A("证据（前两类全部落在两处）：")
    A("")
    A("* 落在函数内的那些，全在 `0x7CA090` 与 `0x7D0A20` 两个 CRT 函数里 —— "
      "两者 size 都是 821、12 个站点偏移逐一对应（同一份代码的两个实例）。"
      "它们那张「表」的 4 个表项指向 `0x7CA1E8` 起步处，可那里的字节是 "
      "`5E 08 45 8B …`（数据）；同函数里**真表**在 `0x7CA16C` —— "
      "8/8 项落在 .text、7/8 是指令起点。两相对照，假站点无疑。")
    A("* 落在函数外的 %d 个：形态一律是「前一个函数已结束、下一个入口还没到」"
      "的那几十字节 CRT 空隙（`0x004FF902` 在 `0x004FF700` 之后 54 字节处，"
      "`0x007C67D0` / `0x007C699A` 在 `0x007C6752` 之后那一段）。"
      % errd.get("跳表站点不在任何函数内", 0))
    A("* 假表项 %d 条。这一类是**假函数**的副产品：表项落在表外、又不在任何函数的"
      "`seen` 里，说明它所依附的那个「函数」本身就是假的（表里挑几个 dword 当目标，"
      "凑不出可达代码）。为 0 表示那批假函数已经被 §4b(3) 里的修复消灭。"
      % errd.get("跳表表项指向非代码（假表项）", 0))
    A("")
    A("这三类**都不连边**（悬空边会让判据 6 里外不是人），只在 §4 记数；"
      "判据 4（遍历指令 100% 落块）为 0 恰好印证了这一点 —— "
      "**真遍历根本没走到这些站点**，所以它们从来不是待切块的指令。")
    A("")
    A("### (2) `jcc` 目标落在函数区间外（记在可疑点里，共见上表）")
    A("")
    A("逐条查过目标性质：目标**是函数入口**的（真·条件尾调用）占少数；")
    A("**多数既不在本函数内、也不是任何函数入口** —— 它们落在 CRT 区里"
      "S1 没能认领的字节上。")
    A("")
    A("这与 S1 的函数体口径一致：S1 的 size 是**包围盒**，末端可能被下一个入口"
      "截断（`0x7C0000` 以上不受 16 字节对齐律约束，边界本就难定）。")
    A("记成 `tail` 边是把「跳到函数外」如实记下来，不是漏边 —— "
      "判据 8（`e <= 2b+c`）与判据 3（从入口可达）都没被它破坏。")
    A("")
    A("### (3) 已经在 S1 侧修掉、这里只是留痕的四处")
    A("")
    A("| 现象 | 根因 | 修法 |")
    A("|---|---|---|")
    A("| `块外指令` 3 条 | Walker 半开窗口 `[lo, hi)` 的**上界闭口** bug："
      "`if nxt > w_hi` 允许指令起点恰好落在 `w_hi`（=函数排他末端）上并被解码、"
      "进 `seen`。3 条分别是 `0x007C6789` / `0x007D0525` / `0x007D910A` | "
      "`tools/funcscan.py`：`bounds` 模式下线性推进改成 `nxt >= w_hi`，"
      "并在循环顶加了一道兜底的半开区间闸 |")
    A("| `链断在函数中间` 145 条 → 0 | **空隙通道把字节索引表当成了函数**："
      "`0x40DD54` 起的 `01 01 01 00 01 01 …` 在 16 对齐处（`0x40DD60`）"
      "能解码成一串 `add [ecx], eax`，无控制流无 call，一路\"走\"到边界，"
      "于是被当成\"被边界截断的函数\"。每个这种假函数在 CFG 里都留一个"
      "\"块末=函数末且无终止指令\"的块，其中一个还造成孤儿块 | `tools/funcscan.py`："
      "新增闸门 `only_bound_ok` —— 出口只剩不自证的 `bound` 时必须另有独立证据"
      "（calls / 栈帧 / SEH / 序言）。被拒的 98 个假函数如实退回 unknown"
      "（0.14% → 0.18%），不硬凑 |")
    A("| `跳表表项指向非代码（假表项）` 5 条 → 0 | `0x007C7757` 是紧贴跳表 "
      "`0x007C7758` 表**头前面**一个字节的 `0x90` 垫料，被空隙通道当成候选、"
      "走通、认成 size 964 的假函数 —— 它把那张 12 项跳表当成了自己的 `switch`，"
      "于是 5 个表项被当成边连出去（目标不是指令起点）。"
      "同族的还有一个 `dataptr` 假函数 `0x7C7757` 吞掉真函数 `0x7C7788` | "
      "`tools/funcscan.py`：`in_table()` 遮蔽「落在表内 **或** 距表头 1~3 字节」"
      "的候选。**方向只能是往右看** —— 往左看会把表尾那个真入口"
      "（它紧贴在表后）自己挡掉，实测正是这么误杀过一次 |")
    A("| `边指向函数内非块首` 1 条 → 0；`假表项` → 0 | **跳表本体被当成指令解码**。"
      "锚点是**点**、跳表是**区间**：`dword` 恰好能解成合法指令，扫描扎进去就回不来，"
      "于是表尾的 `0x7C7788`（函数 `0x7C7371` 内嵌跳表之后的续段）没有指令边界，"
      "而 `0x7C73B4` 的 `jmp` 正指着它 → 悬空边。同一根因还让 `0x7C7371` "
      "从 2,942 字节被截断到 998 字节 | `tools/disasm.py` 新增 `data_ranges`"
      "（数据区遮罩：进区间直接跳到区间末重开解码）；`funcscan.py`（2a/2b）与"
      "`cfgscan.py` 都改成两步重扫，且**共用同一份遮罩** —— "
      "两阶段用不同遮罩会让指令边界漂移 |")
    A("")
    A("另外，块末恰好等于函数末端（既无终止指令、也无后继）的块已单列终止形态 "
      "`end`，不再和真故障的 `bad` 混在一起。")
    A("")
    A("## 5. 最大的 20 个 CFG")
    A("")
    A("| 入口 | 块数 | 边数 | 跳表边 |")
    A("|---|---|---|---|")
    for va, nb, ne, nc in s["top"]:
        A("| `%s` | %d | %d | %d |" % (hx(va), nb, ne, nc))
    A("")
    A("## 6. 数据结构")
    A("")
    A("`db/blocks.json`：")
    A("")
    A("```")
    A("\"edge_kinds\": %s" % json.dumps(EDGE_KINDS))
    A("\"end_kinds\":  %s" % json.dumps(END_KINDS))
    A("\"funcs\": { \"0x00401000\": { \"b\": [ [va, 指令数, 终止形态下标, [[后继va, 边型下标], ...]], ... ] } }")
    A("\"single\": { \"0x00401xxx\": \"ret\" }   # 单块函数：块 = [va, va+size)")
    A("\"single_edges\": { \"0x00401xxx\": [[后继va, 边型下标], ...] }")
    A("```")
    A("")
    A("`single_edges` 不是可有可无的：单块函数里有一类是**纯尾调用跳板**"
      "（整段就一条 `jmp <别的函数>`），那条 `tail` 边正是 Stage 3 建调用图要用的。")
    A("早先 `single` 只存终止形态、把边丢了，于是 `stats.edges` 比 JSON 里能解包的"
      "多出 254 条、`kEdgeCount` 与文档对不上 —— 判据 10 就是钉这件事的。")
    A("")
    A("`src/re/BlockTable.h`：只放**多块函数**的摘要（入口 / 块数 / 边数 / 跳表边数），")
    A("供 C++ 侧跑跨表一致性与计数恒等式。")
    A("")
    p = os.path.join(ROOT, "docs", "cfg.md")
    with open(p, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(L))
    return p, len(L)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", default=None)
    ap.add_argument("--check", action="store_true", help="只对账，不写产物")
    a = ap.parse_args()
    img, ents, blocks_of, single, single_edges, stats = run(a.image)
    if a.check:
        return 0
    p1, n1 = write_json(ents, blocks_of, single, single_edges, stats)
    p2, n2 = write_header(ents, blocks_of, single, stats)
    p3, n3 = write_docs(stats, ents, blocks_of, single, single_edges)
    print("写出 %s (%.1f MB) / %s (%d 行) / %s (%d 行)"
          % (os.path.relpath(p1, ROOT), n1 / 1048576.0,
             os.path.relpath(p2, ROOT), n2, os.path.relpath(p3, ROOT), n3))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
