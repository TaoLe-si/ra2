"""
lzo1x.py -- LZO1X 解压（miniLZO 解码器的 Python 直译）。

为什么要它：RA2/YR 地图的 [IsoMapPack5] / [OverlayPack] 段是
**分块 LZO + base64**（modenc 的 IsoMapPack5 页面写明用 lzo1x_decompress）。
不解压就看不到任何一个瓦片。

直译自 lib/lzo/lzo1x_decompress_safe.c（Linux 内核里的 miniLZO 移植版），
但去掉了两样 RA2 用不到的东西：
  1. bitstream_version（LZO 2.05+ 才有的零游走码），RA2 是 LZO 1.x/2.02 时代，恒为 0
  2. 各种 unaligned 快速路径 —— 只留语义正确的慢路径

状态机说明（内核版用 state 表示，原版用 goto 位置表示）：
  state == 0  上一次是"匹配"，本轮 token<16 => 字面量跑
  state == 4  上一次是"字面量跑"，token<16 => 3 字节 M1 匹配
  state 其它  token<16 => 2 字节匹配 + next 个尾随字面量
"""

from __future__ import annotations

M1_MAX_OFFSET = 0x0400
M2_MAX_OFFSET = 0x0800
M3_MAX_OFFSET = 0x4000
M4_MAX_OFFSET = 0xBFFF


class LzoError(Exception):
    pass


def decompress(src: bytes, out_max: int) -> bytes:
    """把 src（一个 LZO1X 块）解压，最多写 out_max 字节。"""
    out = bytearray()
    ip = 0
    ip_end = len(src)

    def need_ip(n: int):
        if ip_end - ip < n:
            raise LzoError("input overrun")

    def need_op(n: int):
        if out_max - len(out) < n:
            raise LzoError("output overrun")

    state = 0
    t = 0
    next_ = 0

    if ip_end >= 1 and src[0] > 17:
        t = src[ip] - 17
        ip += 1
        if t < 4:
            next_ = t
            # 直接跳到 match_next 的语义
            need_ip(t + 1)
            need_op(t)
            out += src[ip:ip + t]
            ip += t
        else:
            need_ip(t + 1)
            need_op(t)
            out += src[ip:ip + t]
            ip += t
            state = 4

    while ip < ip_end:
        t = src[ip]
        ip += 1
        if t < 16:
            if state == 0:
                if t == 0:
                    ip_last = ip
                    while src[ip] == 0:
                        ip += 1
                        need_ip(1)
                    offset = (ip - ip_last) * 255
                    t += offset + 15 + src[ip]
                    ip += 1
                t += 3
                # copy_literal_run
                need_ip(t + 1)
                need_op(t)
                out += src[ip:ip + t]
                ip += t
                state = 4
                continue
            elif state != 4:
                next_ = t & 3
                m_pos = len(out) - 1 - (t >> 2) - (src[ip] << 2)
                ip += 1
                if m_pos < 0:
                    raise LzoError("lookbehind overrun")
                need_op(2)
                out += out[m_pos:m_pos + 2]
            else:
                next_ = t & 3
                m_pos = len(out) - (1 + M2_MAX_OFFSET) - (t >> 2) - (src[ip] << 2)
                ip += 1
                t = 3
        elif t >= 64:
            next_ = t & 3
            m_pos = len(out) - 1 - ((t >> 2) & 7) - (src[ip] << 3)
            ip += 1
            t = ((t >> 5) - 1) + 2
        elif t >= 32:
            t = (t & 31) + 2
            if t == 2:
                ip_last = ip
                while src[ip] == 0:
                    ip += 1
                    need_ip(1)
                offset = (ip - ip_last) * 255
                t += offset + 31 + src[ip]
                ip += 1
                need_ip(2)
            next_ = src[ip] | (src[ip + 1] << 8)      # le16
            ip += 2
            m_pos = len(out) - 1 - (next_ >> 2)
            next_ &= 3
        else:
            need_ip(2)
            next_ = src[ip] | (src[ip + 1] << 8)      # le16
            m_pos = len(out) - ((t & 8) << 11)
            t = (t & 7) + 2
            if t == 2:
                ip_last = ip
                while src[ip] == 0:
                    ip += 1
                    need_ip(1)
                offset = (ip - ip_last) * 255
                t += offset + 7 + src[ip]
                ip += 1
                need_ip(2)
                next_ = src[ip] | (src[ip + 1] << 8)
            ip += 2
            m_pos -= next_ >> 2
            next_ &= 3
            if m_pos == len(out):
                break                                  # eof_found
            m_pos -= 0x4000

        if m_pos < 0:
            raise LzoError("lookbehind overrun")
        need_op(t)
        # 逐字节拷贝（匹配允许重叠，不能用切片一次性拷）
        for _ in range(t):
            out.append(out[m_pos])
            m_pos += 1

        # match_next
        state = next_
        t = next_
        need_ip(t + 1)
        need_op(t)
        if t:
            out += src[ip:ip + t]
            ip += t

    return bytes(out)


def decompress_chunks(blob: bytes) -> bytes:
    """RA2 的分块格式：重复 [u16 输入长度][u16 输出长度][输入数据]。"""
    out = bytearray()
    p = 0
    n = len(blob)
    while p + 4 <= n:
        in_sz = blob[p] | (blob[p + 1] << 8)
        out_sz = blob[p + 2] | (blob[p + 3] << 8)
        p += 4
        if in_sz == 0 or out_sz == 0:
            break
        if p + in_sz > n:
            raise LzoError("块输入越界: p=%d in_sz=%d n=%d" % (p, in_sz, n))
        chunk = decompress(blob[p:p + in_sz], out_sz)
        if len(chunk) != out_sz:
            raise LzoError("块解压长度不符: 得到 %d，期望 %d" % (len(chunk), out_sz))
        out += chunk
        p += in_sz
    return bytes(out)
