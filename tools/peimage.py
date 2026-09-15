"""
peimage.py -- 针对 gamemd.exe 的 PE 只读封装。

提供 RVA <-> 文件偏移 换算、按节区的内存视图读取、以及整数读取助手。
gamemd.exe 重定位表已剥离，镜像基址固定为 0x00400000，因此 RVA+ImageBase == 运行时 VA，
静态分析无需考虑重基址。
"""

from __future__ import annotations

import os
import struct
import sys

# 让脚本能直接用到 tools/pylibs 里的 pefile / capstone，无需安装到系统环境
_PYLIBS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "pylibs")
if _PYLIBS not in sys.path:
    sys.path.insert(0, _PYLIBS)

import pefile  # noqa: E402

DEFAULT_IMAGE = r"D:\westwood\RA2YR\gamemd.exe"

# Data directory 索引
DIR_EXPORT = 0
DIR_IMPORT = 1
DIR_RESOURCE = 2
DIR_DEBUG = 6
DIR_IAT = 12

# 节区特征
MEM_EXECUTE = 0x20000000
MEM_WRITE = 0x80000000


class PEImage:
    def __init__(self, path: str = DEFAULT_IMAGE):
        self.path = path
        self.data = open(path, "rb").read()
        self.pe = pefile.PE(path, fast_load=False)
        self.image_base = self.pe.OPTIONAL_HEADER.ImageBase
        self.entry_rva = self.pe.OPTIONAL_HEADER.AddressOfEntryPoint
        self.sections = []
        for s in self.pe.sections:
            self.sections.append(
                {
                    "name": s.Name.rstrip(b"\0").decode("ascii", "replace"),
                    "rva": s.VirtualAddress,
                    "vsize": s.Misc_VirtualSize,
                    "raw_ptr": s.PointerToRawData,
                    "raw_size": s.SizeOfRawData,
                    "flags": s.Characteristics,
                }
            )

    # ---------- 地址换算 ----------
    def rva_to_off(self, rva: int) -> int | None:
        for s in self.sections:
            if s["rva"] <= rva < s["rva"] + max(s["vsize"], s["raw_size"]):
                delta = rva - s["rva"]
                if delta >= s["raw_size"]:
                    return None  # 仅存在于内存（BSS 类），文件中无数据
                return s["raw_ptr"] + delta
        return None

    def off_to_rva(self, off: int) -> int | None:
        for s in self.sections:
            if s["raw_ptr"] <= off < s["raw_ptr"] + s["raw_size"]:
                return s["rva"] + (off - s["raw_ptr"])
        return None

    def va(self, rva: int) -> int:
        """RVA -> 运行时 VA（无重定位，直接加基址）"""
        return self.image_base + rva

    def section_of(self, rva: int) -> str:
        for s in self.sections:
            if s["rva"] <= rva < s["rva"] + max(s["vsize"], s["raw_size"]):
                return s["name"]
        return "?"

    def is_exec(self, rva: int) -> bool:
        for s in self.sections:
            if s["rva"] <= rva < s["rva"] + max(s["vsize"], s["raw_size"]):
                return bool(s["flags"] & MEM_EXECUTE)
        return False

    # ---------- 读取助手 ----------
    def read(self, rva: int, n: int) -> bytes | None:
        off = self.rva_to_off(rva)
        if off is None:
            return None
        return self.data[off:off + n]

    def u8(self, rva: int) -> int | None:
        b = self.read(rva, 1)
        return b[0] if b else None

    def u16(self, rva: int) -> int | None:
        b = self.read(rva, 2)
        return struct.unpack("<H", b)[0] if b and len(b) == 2 else None

    def u32(self, rva: int) -> int | None:
        b = self.read(rva, 4)
        return struct.unpack("<I", b)[0] if b and len(b) == 4 else None

    def i32(self, rva: int) -> int | None:
        b = self.read(rva, 4)
        return struct.unpack("<i", b)[0] if b and len(b) == 4 else None

    def cstr(self, rva: int, limit: int = 4096) -> str | None:
        off = self.rva_to_off(rva)
        if off is None:
            return None
        end = self.data.find(b"\0", off, off + limit)
        if end < 0:
            end = off + limit
        return self.data[off:end].decode("ascii", "replace")

    def dwords(self, rva: int, count: int) -> list[int]:
        b = self.read(rva, 4 * count)
        if not b:
            return []
        return list(struct.unpack("<%dI" % (len(b) // 4), b))

    # ---------- 常用视图 ----------
    def text_range(self) -> tuple[int, int]:
        s = next(x for x in self.sections if x["name"] == ".text")
        return s["rva"], s["rva"] + s["vsize"]

    def imports(self) -> list[tuple[str, list[tuple[int, str]]]]:
        """返回 [(dll, [(iat_rva, name), ...]), ...]"""
        out = []
        for e in self.pe.DIRECTORY_ENTRY_IMPORT:
            items = []
            for imp in e.imports:
                if imp.name:
                    items.append((imp.address - self.image_base,
                                  imp.name.decode("ascii", "replace")))
                elif imp.ordinal:
                    items.append((imp.address - self.image_base, "ord#%d" % imp.ordinal))
            out.append((e.dll.decode("ascii", "replace"), items))
        return out

    def imports_flat(self) -> dict[int, str]:
        """IAT RVA -> "dll!name" 的扁平表"""
        flat = {}
        for dll, items in self.imports():
            for rva, name in items:
                flat[rva] = "%s!%s" % (dll, name)
        return flat


def load(path: str = DEFAULT_IMAGE) -> PEImage:
    return PEImage(path)
