"""
mixget.py -- 按 ID 链从（嵌套）MIX 里把条目 dump 到磁盘。

为什么需要：MIX 不存文件名，而嵌套 MIX 里还有嵌套 MIX。
想研究某个具体条目（比如某块地形瓦片），得先把字节抠出来。

用法：
  # 顶层 ra2.mix 里 id=0xB3080BD2 的第 0 个条目
  python tools/mixget.py D:\\westwood\\RA2YR\\ra2.mix 0xB3080BD2 -o build/tiles
  # 嵌套链：ra2.mix -> 0x0F5D1D99 -> 第 3 个条目
  python tools/mixget.py D:\\westwood\\RA2YR\\ra2.mix 0x0F5D1D99 3 -o build/tiles
  python tools/mixget.py D:\\westwood\\RA2YR\\ra2.mix 0x0F5D1D99 --all -o build/tiles
"""

from __future__ import annotations

import os
import sys

sys.path.insert(0, r"E:\ra2source\tools")
import mixdump  # noqa: E402


def resolve(top, ids):
    """沿着 ID 链一路下钻，返回 (owner_mix, off, size)。"""
    m = top
    cur = None
    for i, want in enumerate(ids):
        hit = [e for e in m.entries if e[0] == want]
        if not hit:
            raise SystemExit("[x] 第 %d 层没有 id=0x%08X" % (i, want))
        h, off, size = hit[0]
        cur = (m, off, size)
        if i + 1 < len(ids):
            sub = mixdump.open_nested(m, off, size)
            if sub is None:
                raise SystemExit("[x] 第 %d 层 id=0x%08X 不是 MIX" % (i, want))
            m = sub
    return cur


def main() -> None:
    args = sys.argv[1:]
    if len(args) < 2:
        print(__doc__)
        sys.exit(1)
    top_path = args[0]
    rest = args[1:]
    out_dir = "build/mixget"
    all_flag = False
    idx = None
    ids = []
    i = 0
    while i < len(rest):
        a = rest[i]
        if a in ("-o", "--out"):
            out_dir = rest[i + 1]
            i += 2
        elif a == "--all":
            all_flag = True
            i += 1
        elif a.startswith("0x") or a.startswith("0X"):
            ids.append(int(a, 16))
            i += 1
        else:
            idx = int(a)
            i += 1

    if not ids:
        print("[x] 至少给一个 0xID")
        sys.exit(1)

    m = mixdump.MixFile(top_path)
    os.makedirs(out_dir, exist_ok=True)

    if all_flag:
        if len(ids) != 1:
            print("[x] --all 只接受一个 ID（就是那个要整体导出的归档）")
            sys.exit(1)
        owner, off, size = resolve(m, ids)
        sub = mixdump.open_nested(owner, off, size)
        if sub is None:
            print("[x] 0x%08X 不是 MIX" % ids[0])
            sys.exit(1)
        n = 0
        for k, (h, so, ss) in enumerate(sub.entries):
            d = sub.read(so, ss)
            p = os.path.join(out_dir, "%04d_0x%08X.bin" % (k, h))
            open(p, "wb").write(d)
            n += 1
        print("[OK] 导出 %d 个条目 -> %s" % (n, out_dir))
        return

    if idx is None:
        # 只给了 ID 链，没给序号 -> 导出该条目本身
        owner, off, size = resolve(m, ids)
        d = owner.read(off, size)
        p = os.path.join(out_dir, "0x%08X.bin" % ids[-1])
        open(p, "wb").write(d)
        print("[OK] %d 字节 -> %s（头 32 字节: %s）" % (len(d), p, d[:32].hex(" ")))
        return

    # ID 链 + 序号：链的最后一层必须是 MIX，取它的第 idx 个条目
    owner, off, size = resolve(m, ids)
    sub = mixdump.open_nested(owner, off, size)
    if sub is None:
        print("[x] 0x%08X 不是 MIX，给不了序号" % ids[-1])
        sys.exit(1)
    if idx >= sub.count:
        print("[x] 序号 %d 越界（只有 %d 个）" % (idx, sub.count))
        sys.exit(1)
    h, so, ss = sub.entries[idx]
    d = sub.read(so, ss)
    p = os.path.join(out_dir, "%04d_0x%08X.bin" % (idx, h))
    open(p, "wb").write(d)
    print("[OK] 第 %d 个 id=0x%08X %d 字节 -> %s" % (idx, h, len(d), p))
    print("     头 32 字节: %s" % d[:32].hex(" "))


if __name__ == "__main__":
    main()
