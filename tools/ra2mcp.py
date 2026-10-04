#!/usr/bin/env python3
"""ra2mcp —— RA2/YR 还原工程的调试用 MCP 服务器（纯标准库，无第三方依赖）。

为什么自己写而不装 `mcp` 包：这个仓库的原则是"能用标准库就别拖依赖"，
而且 MCP 的 stdio 传输就是**换行分隔的 JSON-RPC 2.0**，几十行就能实现。
少一个 pip 依赖 = 少一个"在我机器上能跑"的坑。

暴露的工具就是逆向过程中**反复手敲的那些命令**：
  build / selftest / run_game       —— 构建、自检、离屏出图
  image_info / image_diff           —— 出图后的像素统计与 A/B 比对
  mix_read / mix_list               —— 从 MIX 归档取文件（判据来源）
  disasm / find_refs / find_strings —— gamemd.exe 反汇编与字符串检索
  ledger_read / ledger_append       —— 逆向总账读写

用法（由 ZCode 的 ~/.zcode/cli/config.json 拉起，一般不用手敲）：
  python tools/ra2mcp.py
"""

from __future__ import annotations

import json
import os
import re
import struct
import subprocess
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(ROOT, "tools")
GAMEMD = r"D:\westwood\RA2YR\gamemd.exe"
RA2_MIX = r"D:\westwood\RA2YR\ra2.mix"
RA2MD_MIX = r"D:\westwood\RA2YR\ra2md.mix"

sys.path.insert(0, TOOLS)

PROTOCOL_VERSION = "2024-11-05"


# ---------------------------------------------------------------- 不抢前台
def _quiet_kwargs(hide=False):
    """子进程一律**不抢前台、不动鼠标**（ZCode 拉起本服务时没有控制台）。

    两条默认行为会打断正在用电脑的人：
      · 控制台子进程（build.py / tasklist）会被新建一个控制台窗口 —— 窗口闪一下并抢焦点；
      · GUI 子进程（ra2game.exe 等）默认 SW_SHOWNORMAL 显示**并激活**。
    所以：CREATE_NO_WINDOW（不建控制台）+ STARTF_USESHOWWINDOW/SW_SHOWNOACTIVATE（显示但不激活）。
    本机实测见 C:\\Users\\Administrator\\.zcode\\mcp-servers\\_shared\\README-quiet-launch.md
    （对照组：不加参数的 python 子进程会多出 ConsoleWindowClass 窗口并抢走前台）。
    调试时若要恢复旧行为：设 RA2_MCP_ALLOW_FOCUS=1。
    """
    if os.name != "nt" or os.environ.get("RA2_MCP_ALLOW_FOCUS") == "1":
        return {}
    si = subprocess.STARTUPINFO()
    si.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    si.wShowWindow = 0 if hide else 4        # SW_HIDE / SW_SHOWNOACTIVATE
    return {"startupinfo": si, "creationflags": 0x08000000}   # CREATE_NO_WINDOW


# ---------------------------------------------------------------- 工具实现
def _run(cmd, cwd=ROOT, timeout=900):
    p = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True,
                       errors="replace", timeout=timeout, **_quiet_kwargs())
    return p.returncode, (p.stdout or "") + (p.stderr or "")


def tool_build(target=""):
    cmd = [sys.executable, os.path.join(TOOLS, "build.py")]
    if target:
        cmd.append(target)
    rc, out = _run(cmd)
    errs = [l for l in out.splitlines() if re.search(r"error C|error LNK|构建失败", l)]
    body = "".join(errs[:40]) if errs else out[-4000:]
    return "rc=%d\n%s" % (rc, body)


def tool_selftest(map_name="build/Arena.map", extra=""):
    cmd = [os.path.join(ROOT, "build", "ra2game.exe"), RA2_MIX,
           "--addmix", RA2MD_MIX, "--map", map_name, "--selftest"]
    if extra:
        cmd += extra.split()
    rc, out = _run(cmd)
    keep = [l for l in out.splitlines()
            if re.search(r"\[OK\]|\[FAIL\]|自检|== |能解码|影子|srv ", l)]
    fails = [l for l in keep if "FAIL" in l]
    tail = out.splitlines()[-3:]
    return "rc=%d  失败 %d\n%s\n--- 尾部 ---\n%s" % (
        rc, len(fails), "\n".join(keep), "\n".join(tail))


def tool_run_game(map_name="build/Arena.map", frames=60, out="build/mcp_shot.png",
                  env="", menu=False, extra=""):
    raw = os.path.join(ROOT, "build", "mcp_shot.raw")
    cmd = [os.path.join(ROOT, "build", "ra2game.exe"), RA2_MIX, "--addmix", RA2MD_MIX]
    if menu:
        cmd += ["--offscreen", "--menu", "--frames", str(frames), "--out", raw]
    else:
        cmd += ["--offscreen", "--map", map_name, "--frames", str(frames), "--out", raw]
    if extra:
        cmd += extra.split()
    e = dict(os.environ)
    for kv in env.split():
        if "=" in kv:
            k, v = kv.split("=", 1)
            e[k] = v
    p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True,
                       errors="replace", timeout=1200, env=e, **_quiet_kwargs())
    log = (p.stdout or "") + (p.stderr or "")
    if os.path.exists(raw):
        tool_raw_to_png(raw, 1024, 768, out)
    keep = [l for l in log.splitlines()
            if re.search(r"预热精灵|Trigger fire|存档|影片|FAIL|离屏|error", l)][:40]
    return "rc=%d  图=%s\n%s" % (p.returncode, out, "\n".join(keep))


# ---- 极简 PNG 读写（只用 zlib/struct，和仓库里其它脚本一致）----
def _read_png(path):
    """读 PNG 成 **RGBA** 行（低层只支持 8 位 RGB/RGBA —— 本仓库自己写的图就这两种）。

    注意 rawdump.py 出的是 colortype=2（RGB，3 字节/像素），而 dx12 那边可能出
    RGBA。三字节的图按四字节步长去读会直接下标越界，所以这里按 IHDR 里的
    颜色类型定 stride，统一补成 RGBA。
    """
    d = open(path, "rb").read()
    w, h = struct.unpack(">II", d[16:24])
    ctype = d[25]
    bpp = 4 if ctype == 6 else 3 if ctype == 2 else 0
    if bpp == 0:
        raise ValueError("只支持 colortype 2/6（这是 %d）" % ctype)
    idat = b""
    i = 8
    while i < len(d):
        ln = struct.unpack(">I", d[i:i + 4])[0]
        if d[i + 4:i + 8] == b"IDAT":
            idat += d[i + 8:i + 8 + ln]
        i += 12 + ln
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev, pos = [], bytearray(stride), 0
    for _ in range(h):
        f = raw[pos]
        pos += 1
        line = bytearray(raw[pos:pos + stride])
        pos += stride
        if f == 1:
            for x in range(bpp, stride):
                line[x] = (line[x] + line[x - bpp]) & 255
        elif f == 2:
            for x in range(stride):
                line[x] = (line[x] + prev[x]) & 255
        elif f == 3:
            for x in range(stride):
                a = line[x - bpp] if x >= bpp else 0
                line[x] = (line[x] + ((a + prev[x]) >> 1)) & 255
        elif f == 4:
            for x in range(stride):
                a = line[x - bpp] if x >= bpp else 0
                b = prev[x]
                c = prev[x - bpp] if x >= bpp else 0
                pp = a + b - c
                pa, pb, pc = abs(pp - a), abs(pp - b), abs(pp - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 255
        if bpp == 3:      # RGB → RGBA
            out = bytearray(w * 4)
            for x in range(w):
                out[x * 4:x * 4 + 3] = line[x * 3:x * 3 + 3]
                out[x * 4 + 3] = 255
            line = out
        rows.append(bytes(line))
        prev = line
    return w, h, rows


def _write_png(path, w, h, rgba):
    raw = b"".join(b"\x00" + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))

    def chunk(t, d):
        return (struct.pack(">I", len(d)) + t + d +
                struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF))
    open(path, "wb").write(
        b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
        chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def tool_raw_to_png(raw, w=1024, h=768, out="build/mcp_shot.png"):
    if not os.path.isabs(raw):
        raw = os.path.join(ROOT, raw)
    if not os.path.isabs(out):
        out = os.path.join(ROOT, out)
    d = open(raw, "rb").read()
    if len(d) < w * h * 4:
        return "raw 太小：%d < %d" % (len(d), w * h * 4)
    _write_png(out, w, h, d[:w * h * 4])
    return "写出 %s（%dx%d）" % (out, w, h)


def _crop_info(path, crop=None):
    w, h, rows = _read_png(path)
    if crop:
        x0, y0, x1, y1 = [int(v) for v in crop.split(",")]
        rows = [r[x0 * 4:x1 * 4] for r in rows[y0:y1]]
        w, h = x1 - x0, y1 - y0
    n = 0
    nonblack = 0
    for r in rows:
        for x in range(0, len(r), 4):
            if r[x + 3]:
                n += 1
                if max(r[x], r[x + 1], r[x + 2]) > 16:
                    nonblack += 1
    return w, h, n, nonblack


def tool_image_info(path, crop=""):
    if not os.path.isabs(path):
        path = os.path.join(ROOT, path)
    w, h, n, nb = _crop_info(path, crop or None)
    return ("%s  %dx%d  不透明 %d (%.1f%%)  非黑 %d (%.1f%%)"
            % (os.path.basename(path), w, h, n, 100.0 * n / max(1, w * h),
               nb, 100.0 * nb / max(1, w * h)))


def tool_image_diff(a, b, crop="", out=""):
    for k in ("a", "b"):
        pass
    pa = a if os.path.isabs(a) else os.path.join(ROOT, a)
    pb = b if os.path.isabs(b) else os.path.join(ROOT, b)
    wa, ha, ra = _read_png(pa)
    wb, hb, rb = _read_png(pb)
    if (wa, ha) != (wb, hb):
        return "尺寸不同：%dx%d vs %dx%d" % (wa, ha, wb, hb)
    x0, y0, x1, y1 = (0, 0, wa, ha)
    if crop:
        x0, y0, x1, y1 = [int(v) for v in crop.split(",")]
    diff = 0
    tot = 0
    worst = (0, None)
    mask = bytearray((x1 - x0) * (y1 - y0) * 4)
    for y in range(y0, y1):
        for x in range(x0, x1):
            o = x * 4
            d = max(abs(ra[y][o + i] - rb[y][o + i]) for i in range(3))
            # alpha 也要算（影子只看 alpha 差异）
            if not ra[y][o + 3] and not rb[y][o + 3]:
                d = 0
            else:
                d = max(d, abs(ra[y][o + 3] - rb[y][o + 3]))
            tot += d
            if d:
                diff += 1
            if d > worst[0]:
                worst = (d, (x, y))
            m = ((y - y0) * (x1 - x0) + (x - x0)) * 4
            v = min(255, d * 4)
            mask[m:m + 3] = bytes((v, v, v))
            mask[m + 3] = 255
    n = (x1 - x0) * (y1 - y0)
    if out:
        p = out if os.path.isabs(out) else os.path.join(ROOT, out)
        _write_png(p, x1 - x0, y1 - y0, bytes(mask))
    return ("差异像素 %d/%d  平均通道差 %.3f/255  最大 %d @ %s  掩膜=%s"
            % (diff, n, tot / max(1, n), worst[0], worst[1], out or "(未写)"))


def _mix():
    import mixdump
    import uidump
    from remapprobe import crc_name
    tops = []
    for p in (RA2_MIX, RA2MD_MIX):
        if os.path.exists(p):
            tops.append(mixdump.MixFile(p))
    return mixdump, uidump, crc_name, tops


def tool_mix_read(name, out=""):
    mixdump, uidump, crc_name, tops = _mix()
    for top in tops:
        d = uidump.read_entry(top, crc_name(name))
        if d is not None:
            if out:
                p = out if os.path.isabs(out) else os.path.join(ROOT, out)
                open(p, "wb").write(d)
                return "%s 命中 %d 字节 → %s" % (name, len(d), p)
            return "%s 命中 %d 字节，前 32: %s" % (name, len(d), d[:32].hex())
    return "%s 未命中（ra2.mix / ra2md.mix 深层）" % name


def tool_disasm(va, count=30):
    from peimage import PEImage
    from capstone import Cs, CS_ARCH_X86, CS_MODE_32
    pe = PEImage(GAMEMD)
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True
    try:
        start = int(va, 0)
    except ValueError:
        return "va 要十六进制，如 0x6A5090"
    off = pe.rva_to_off(start - pe.image_base)
    out = []
    for ins in md.disasm(pe.data[off:off + count * 16], start):
        note = ""
        for op in ins.operands:
            if op.type == 3 and op.mem.base == 0 and op.mem.index == 0:
                note = "   ; [%#x]" % op.mem.disp
        out.append("%#010x: %-8s %s%s" % (ins.address, ins.mnemonic, ins.op_str, note))
        if len(out) >= count:
            break
    return "\n".join(out) or "该地址不在 .text"


def tool_find_refs(va):
    from peimage import PEImage
    pe = PEImage(GAMEMD)
    tlo_rva, thi_rva = pe.text_range()
    tlo, thi = pe.rva_to_off(tlo_rva), pe.rva_to_off(thi_rva)
    try:
        target = int(va, 0)
    except ValueError:
        return "va 要十六进制"
    b = struct.pack("<I", target)
    hits = []
    pos = tlo
    while len(hits) < 60:
        i = pe.data.find(b, pos, thi)
        if i < 0:
            break
        pos = i + 1
        hits.append(pe.image_base + pe.off_to_rva(i))
    return "%#x 被引用 %d 处：%s" % (target, len(hits),
                                    " ".join(hex(x) for x in hits))


def tool_find_strings(pattern, limit=40):
    p = pattern
    if not os.path.isabs(p):
        p = os.path.join(ROOT, "db", "strings.json")
    s = json.load(open(p, encoding="utf-8"))
    rx = re.compile(pattern, re.I)
    hits = [(int(k, 16), v) for k, v in s.items() if rx.search(v or "")]
    hits.sort()
    return "\n".join("%#x %r" % (a, v) for a, v in hits[:limit]) or "无命中"


def tool_ledger_read():
    p = os.path.join(ROOT, "docs", "re-ledger.md")
    return open(p, encoding="utf-8").read()[-6000:]


def tool_ledger_append(text):
    p = os.path.join(ROOT, "docs", "re-ledger.md")
    with open(p, "a", encoding="utf-8") as f:
        f.write("\n" + text.rstrip() + "\n")
    return "已追加 %d 字节" % len(text)


# ---------------------------------------------------------------- 工具表
TOOLS_SPEC = [
    ("ra2_build", "构建三个目标（ra2core/ra2view/ra2game），只回错误行。",
     {"type": "object", "properties": {
         "target": {"type": "string", "description": "可选，只构建某个目标"}}},
     lambda a: tool_build(a.get("target", ""))),
    ("ra2_selftest", "跑 --selftest，回断言汇总 + 尾部日志 + 失败数。",
     {"type": "object", "properties": {
         "map": {"type": "string", "description": "地图路径，默认 build/Arena.map"}}},
     lambda a: tool_selftest(a.get("map", "build/Arena.map"))),
    ("ra2_run", "离屏跑游戏并出图（战场或菜单），回关键日志 + PNG 路径。",
     {"type": "object", "properties": {
         "map": {"type": "string"}, "frames": {"type": "integer"},
         "out": {"type": "string", "description": "输出 PNG，默认 build/mcp_shot.png"},
         "menu": {"type": "boolean", "description": "true = 跑菜单而不是战场"},
         "env": {"type": "string",
                 "description": "额外环境变量，空格分隔，如 RA2_MENU_PAGE=4"}}},
     lambda a: tool_run_game(a.get("map", "build/Arena.map"), int(a.get("frames", 60)),
                             a.get("out", "build/mcp_shot.png"), a.get("env", ""),
                             bool(a.get("menu", False)))),
    ("ra2_image_info", "报 PNG 的尺寸/不透明像素/非黑像素（可裁切矩形 crop=x0,y0,x1,y1）。",
     {"type": "object", "properties": {
         "path": {"type": "string"}, "crop": {"type": "string"}},
      "required": ["path"]},
     lambda a: tool_image_info(a["path"], a.get("crop", ""))),
    ("ra2_image_diff", "逐像素比两张 PNG（含 alpha），回差异数/平均差/最大值 + 掩膜图。",
     {"type": "object", "properties": {
         "a": {"type": "string"}, "b": {"type": "string"},
         "crop": {"type": "string"}, "out": {"type": "string"}},
      "required": ["a", "b"]},
     lambda a: tool_image_diff(a["a"], a["b"], a.get("crop", ""), a.get("out", ""))),
    ("ra2_mix_read", "从 ra2.mix/ra2md.mix 深层按名取文件（可落盘）。",
     {"type": "object", "properties": {
         "name": {"type": "string"}, "out": {"type": "string"}},
      "required": ["name"]},
     lambda a: tool_mix_read(a["name"], a.get("out", ""))),
    ("ra2_disasm", "反汇编 gamemd.exe 指定 VA。",
     {"type": "object", "properties": {
         "va": {"type": "string"}, "count": {"type": "integer"}},
      "required": ["va"]},
     lambda a: tool_disasm(a["va"], int(a.get("count", 30)))),
    ("ra2_find_refs", "找 gamemd.exe 里引用某个绝对地址（4 字节小端）的指令位置。",
     {"type": "object", "properties": {"va": {"type": "string"}}, "required": ["va"]},
     lambda a: tool_find_refs(a["va"])),
    ("ra2_find_strings", "在 db/strings.json 里按正则找字符串（回 VA + 文本）。",
     {"type": "object", "properties": {
         "pattern": {"type": "string"}, "limit": {"type": "integer"}},
      "required": ["pattern"]},
     lambda a: tool_find_strings(a["pattern"], int(a.get("limit", 40)))),
    ("ra2_ledger_read", "读 docs/re-ledger.md 尾部（逆向总账）。",
     {"type": "object", "properties": {}},
     lambda a: tool_ledger_read()),
    ("ra2_ledger_append", "往 docs/re-ledger.md 追加一节。",
     {"type": "object", "properties": {"text": {"type": "string"}},
      "required": ["text"]},
     lambda a: tool_ledger_append(a["text"])),
]


def spec_tools():
    out = []
    for name, desc, schema, _ in TOOLS_SPEC:
        out.append({"name": name, "description": desc, "inputSchema": schema})
    return out


def call_tool(name, args):
    for n, _, _, fn in TOOLS_SPEC:
        if n == name:
            try:
                return fn(args or {}), False
            except Exception as exc:                                  # noqa: BLE001
                return "%s: %s" % (type(exc).__name__, exc), True
    return "未知工具 %s" % name, True


# ---------------------------------------------------------------- 传输
def send(obj):
    sys.stdout.write(json.dumps(obj, ensure_ascii=False) + "\n")
    sys.stdout.flush()


def main():
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            msg = json.loads(line)
        except json.JSONDecodeError:
            continue
        mid = msg.get("id")
        method = msg.get("method", "")
        if method == "initialize":
            send({"jsonrpc": "2.0", "id": mid, "result": {
                "protocolVersion": PROTOCOL_VERSION,
                "capabilities": {"tools": {}},
                "serverInfo": {"name": "ra2mcp", "version": "1.0.0"}}})
        elif method in ("notifications/initialized", "initialized"):
            continue
        elif method == "ping":
            send({"jsonrpc": "2.0", "id": mid, "result": {}})
        elif method == "tools/list":
            send({"jsonrpc": "2.0", "id": mid, "result": {"tools": spec_tools()}})
        elif method == "tools/call":
            params = msg.get("params") or {}
            text, is_err = call_tool(params.get("name", ""), params.get("arguments"))
            send({"jsonrpc": "2.0", "id": mid, "result": {
                "content": [{"type": "text", "text": text}], "isError": is_err}})
        elif mid is not None:
            send({"jsonrpc": "2.0", "id": mid,
                  "error": {"code": -32601, "message": "未实现：" + method}})


if __name__ == "__main__":
    main()