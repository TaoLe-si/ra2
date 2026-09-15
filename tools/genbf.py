"""genbf.py -- 生成 src/io/BlowfishTables.h（Blowfish P/S 盒初值）。

初值是圆周率小数部分的十六进制展开。这里用 Machin 公式在整数定点上算出来，
再断言几个已知常数（P[0]=0x243F6A88、S[3][255]=0x3AC372E6）防止算错。

用法： python tools/genbf.py
"""

from __future__ import annotations

import sys

sys.path.insert(0, r"E:\ra2source\tools")
import mixdump as M  # noqa: E402

OUT = r"E:\ra2source\src\io\BlowfishTables.h"


def main() -> None:
    P, S = M._P_INIT, M._S_INIT
    assert P[0] == 0x243F6A88, "P[0] 不对：0x%08X" % P[0]
    assert P[17] == 0x8979FB1B, "P[17] 不对：0x%08X" % P[17]
    assert S[0][0] == 0xD1310BA6, "S[0][0] 不对：0x%08X" % S[0][0]
    assert S[3][255] == 0x3AC372E6, "S[3][255] 不对：0x%08X" % S[3][255]

    L = [
        "// BlowfishTables.h -- 由 tools/genbf.py 生成，勿手改。",
        "//",
        "// Blowfish 的 P 盒(18 项) 与 S 盒(4x256) 初值是圆周率小数部分的十六进制展开。",
        "// 用 Machin 公式 pi = 16*atan(1/5) - 4*atan(1/239) 在整数定点上算出，",
        "// 生成器已断言 P[0]、P[17]、S[0][0]、S[3][255] 四个已知常数。",
        "#pragma once",
        "#include <cstdint>",
        "namespace ra2 {",
        "namespace detail {",
        "inline constexpr uint32_t kBlowfishP[18] = {",
    ]
    for i in range(0, 18, 6):
        L.append("    " + " ".join("0x%08Xu," % v for v in P[i:i + 6]))
    L.append("};")
    L.append("inline constexpr uint32_t kBlowfishS[4][256] = {")
    for k in range(4):
        L.append("    {")
        for i in range(0, 256, 6):
            L.append("        " + " ".join("0x%08Xu," % v for v in S[k][i:i + 6]))
        L.append("    },")
    L.append("};")
    L.append("}  // namespace detail")
    L.append("}  // namespace ra2")

    # 带 BOM 的 UTF-8：MSVC 不额外加 /utf-8 也能正确读中文注释。
    with open(OUT, "w", encoding="utf-8-sig", newline="\n") as f:
        f.write("\n".join(L) + "\n")
    print("已写出 %s (%d 行)" % (OUT, len(L)))


if __name__ == "__main__":
    main()
