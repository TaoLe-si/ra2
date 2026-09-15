"""
mapsec.py -- 把 .map 正文按段切开，报告每段的长度和内容特征。

RA2 的 .map 是"看起来像 INI"的二进制容器：文本段真的就是 INI，
但 IsoMapPack5 / OverlayPack 这些段是 base64 编码的压缩流。
先认清每段是什么，才能逐个击破。

用法：
  python tools/mapsec.py <file.map>
"""

from __future__ import annotations

import re
import sys

_SEC_RE = re.compile(rb"(?m)^\[([A-Za-z0-9_]+)\][ \t]*\r?$")


def main() -> None:
    path = sys.argv[1]
    d = open(path, "rb").read()
    print("%s  %d 字节" % (path, len(d)))

    marks = [(m.start(), m.group(1).decode("ascii")) for m in _SEC_RE.finditer(d)]
    print("段数 %d" % len(marks))
    if not marks:
        return

    for i, (off, name) in enumerate(marks):
        end = marks[i + 1][0] if i + 1 < len(marks) else len(d)
        body = d[off:end]
        # 只看段体（去掉段名那一行）
        nl = body.find(b"\n")
        payload = body[nl + 1:] if nl >= 0 else b""
        # 判断是不是纯 base64 大段
        stripped = payload.replace(b"\r", b"").replace(b"\n", b"")
        b64ish = len(stripped) > 200 and all(
            (0x30 <= c <= 0x39) or (0x41 <= c <= 0x5A) or (0x61 <= c <= 0x7A)
            or c in (0x2B, 0x2F, 0x3D) for c in stripped[:4096])
        kind = "BASE64" if b64ish else "文本/其它"
        print("  %-16s 偏移 %-8d 段体 %-8d 字节  %s" % (name, off, len(payload), kind))
        if not b64ish and len(payload) < 3000:
            txt = payload.decode("ascii", "ignore")
            for line in txt.splitlines()[:40]:
                print("      | " + line)


if __name__ == "__main__":
    main()
