"""
tmphash.py -- 全量回归：用参考实现把归档里每个 TMP 渲染一遍，和 C++ 的输出对账。

为什么值得单独做一个：
  "看几张图觉得对"是不可靠的验收。660 个模板、每个几万像素，随便哪个 cell 的
  切片偏移错一格都不会被肉眼发现，但哈希一定会炸。
  实测这套对账抓出过两个真问题：
    * 多 cell 模板按 (bx*cw, by*ch) 堆叠 —— 错，必须用 Tx/Ty 等倾铺排；
    * 对账脚本自己的底色 (24,24,32) 撞上了 TEMPERAT.PAL 索引 226。

用法：
  python tools/tmphash.py <顶层mix> <0x归档ID> [PAL名] [--iso]
  python tools/tmphash.py <顶层mix> <0x归档ID> --check <c++输出文件>   # 直接 diff

C++ 侧产出：
  build/ra2core.exe --tmphash <mix> <0x归档ID> <0x调色板CRC> [iso]
"""

from __future__ import annotations

import sys

sys.path.insert(0, r"E:\ra2source\tools")
import mixdump   # noqa: E402
import tmpdump   # noqa: E402
import tmppara   # noqa: E402


def fnv(mixv, vals) -> int:
    h = 2166136261
    for v in vals:
        for i in range(4):
            h = ((h ^ ((v >> (i * 8)) & 0xFF)) * 16777619) & 0xFFFFFFFF
    return h


def compute(top: str, arch: int, palname: str, with_extra: bool):
    pal = tmppara.load_pal(palname)
    m = mixdump.MixFile(top)
    hit = [e for e in m.entries if e[0] == arch][0]
    sub = mixdump.open_nested(m, hit[1], hit[2])
    lines = ["# arch=0x%08X flags=0x%08X entries=%d pal=%d extra=%d"
             % (arch, sub.flags, sub.count, 1 if pal else 0, 1 if with_extra else 0)]
    idx = 0
    for h, off, size in sub.entries:
        raw = sub.read(off, size)
        if len(raw) < 16:
            continue
        try:
            t = tmpdump.parse(raw)
        except ValueError:
            continue
        W, H, rows = tmppara.render_block(raw, t, pal, with_extra)
        # RGBA 打包要跟 C++ 完全一致：R 在低字节，透明 = 0。
        vals = [W, H]
        for r in rows:
            for p in r:
                if p == tmppara.BG:
                    vals.append(0)
                else:
                    vals.append(p[0] | (p[1] << 8) | (p[2] << 16) | (0xFF << 24))
        lines.append("%d 0x%08X %dx%d %d 0x%08X"
                     % (idx, h, W, H, W * H, fnv(None, vals)))
        idx += 1
    lines.append("# 共 %d 个模板" % idx)
    return lines


def main() -> None:
    args = sys.argv[1:]
    check = None
    with_extra = True
    palname = "TEMPERAT.PAL"
    if "--check" in args:
        i = args.index("--check")
        check = args[i + 1]
        del args[i:i + 2]
    if "--iso" in args:
        with_extra = False
        args.remove("--iso")
    non_flag = [a for a in args if not a.startswith("--")]
    top, arch = non_flag[0], int(non_flag[1], 16)
    if len(non_flag) > 2:
        palname = non_flag[2]

    got = compute(top, arch, palname, with_extra)
    print("\n".join(got[:4]) + ("\n..." if len(got) > 6 else ""))

    if check is None:
        out = r"E:\ra2source\build\tmphash_py.txt"
        open(out, "w", encoding="utf-8").write("\n".join(got) + "\n")
        print("已写出 %s（%d 行）" % (out, len(got)))
        return

    # 注释行（# 开头）不参与比对：C++ 的 flags 用 Describe_Flags() 的描述文本，
    # Python 打的是原始 0x00010000，二者本来就不同，比它没有意义。
    want = [l.strip() for l in open(check, encoding="utf-8")
            if l.strip() and not l.startswith("#")]
    got = [l for l in got if not l.startswith("#")]
    bad = 0
    for i in range(max(len(got), len(want))):
        a = want[i] if i < len(want) else "<缺>"
        b = got[i] if i < len(got) else "<缺>"
        if a != b:
            if bad < 10:
                print("差异 #%d:\n  C++    %s\n  Python %s" % (i, a, b))
            bad += 1
    if bad == 0:
        print("逐条对账通过：%d 行完全一致（含 %s）"
              % (len(want), "含 extra" if with_extra else "仅 iso"))
    else:
        print("共 %d 行不一致" % bad)
        sys.exit(1)


if __name__ == "__main__":
    main()
