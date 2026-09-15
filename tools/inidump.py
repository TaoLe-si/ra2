"""
inidump.py -- INI 参考解析实现 + 与 C++ 逐行对账。

为什么要单独写一份 Python：
  格式理解对不对，唯一可信的验收是"两边各算一遍、结果字节一致"。
  rulesmd.ini 有 31k 行、1478 段、29k 条目，肉眼抽查根本覆盖不到
  [Animations] 里那行少了等号的畸形数据、段头尾的 `;` 注释、
  段内重复键这类边角。

规范化规则（必须和 C++ 的 Dump_Ini 完全一致）：
    段名 = '[' 与第一个 ']' 之间（trim）
    键   = 第一个 '=' 之前（trim）
    值   = 第一个 '=' 之后、第一个 ';' 之前（trim）
  保留原始大小写 —— 这样对账顺带验出"大小写有没有被弄丢"。

用法：
  python tools/inidump.py <path.ini>                     # 打印统计 + 写规范化文本
  python tools/inidump.py <path.ini> --check <cpp输出>    # 逐行对账
"""

from __future__ import annotations

import sys

SPACE = " \t\r\n\f\v"


def parse(text: str):
    """返回 (sections, malformed)。sections = [(name, [(key, value), ...]), ...]"""
    sections = []
    index = {}
    cur = None
    malformed = 0
    for raw in text.split("\n"):
        s = raw.strip(SPACE)
        if not s:
            continue
        if s[0] == ";":
            continue
        if s[:2] == "//":
            continue
        if s[0] == "[":
            close = s.find("]", 1)
            name = (s[1:] if close < 0 else s[1:close]).strip(SPACE)
            if not name:
                malformed += 1
                continue
            key = name.lower()
            if key in index:
                cur = sections[index[key]][1]
                continue
            index[key] = len(sections)
            cur = []
            sections.append((name, cur))
            continue
        eq = s.find("=")
        if eq < 0:
            # 原始文件自带这种畸形行（[Animations] 里的 `842-GAWETH_ED`）
            malformed += 1
            continue
        k = s[:eq].strip(SPACE)
        if not k:
            malformed += 1
            continue
        v = s[eq + 1:]
        semi = v.find(";")
        if semi >= 0:
            v = v[:semi]
        v = v.strip(SPACE)
        if cur is None:
            malformed += 1
            continue
        cur.append((k, v))
    return sections, malformed


def to_lines(sections, malformed):
    n = sum(len(e) for _, e in sections)
    out = ["# sections=%d entries=%d malformed=%d" % (len(sections), n, malformed)]
    for name, entries in sections:
        out.append("[%s]" % name)
        for k, v in entries:
            out.append("%s=%s" % (k, v))
    return out


def main() -> None:
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return
    path = args[0]
    check = None
    if "--check" in args:
        i = args.index("--check")
        check = args[i + 1]

    raw = open(path, "rb").read()
    non_ascii = sum(1 for b in raw if b >= 0x80)
    sections, malformed = parse(raw.decode("latin-1"))
    lines = to_lines(sections, malformed)
    print("文件 %s：%d 字节（非 ASCII %d），%d 段，%d 条目，畸形行 %d"
          % (path, len(raw), non_ascii, len(sections),
             sum(len(e) for _, e in sections), malformed))

    if check is None:
        out = r"E:\ra2source\build\ini\_ref.txt"
        open(out, "w", encoding="utf-8", newline="\n").write("\n".join(lines) + "\n")
        print("已写出", out)
        return

    want = open(check, encoding="utf-8").read().splitlines()
    got = open(check, encoding="utf-8").read().splitlines() if False else lines
    bad = 0
    for i in range(max(len(got), len(want))):
        a = want[i] if i < len(want) else "<缺>"
        b = got[i] if i < len(got) else "<缺>"
        if a != b:
            if bad < 10:
                print("差异 #%d:\n  C++    %r\n  Python %r" % (i, a, b))
            bad += 1
    if bad == 0:
        print("逐行对账通过：%d 行完全一致" % len(want))
    else:
        print("共 %d 行不一致" % bad)
        sys.exit(1)


if __name__ == "__main__":
    main()
