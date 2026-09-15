// Lzo1x.cpp -- LZO1X 解压实现。规则说明见 Lzo1x.h。

#include "io/Lzo1x.h"

namespace ra2 {
namespace {

constexpr size_t kM2MaxOffset = 0x0800;

inline uint16_t Rd16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));   // 小端
}

}  // namespace

int Lzo1x_Decompress(const uint8_t* in, size_t in_len,
                     uint8_t* out, size_t out_cap, size_t* out_len) {
    if (in == nullptr || out == nullptr || out_len == nullptr || in_len < 3) {
        return kLzoInputOverrun;
    }

    size_t ip = 0;
    size_t op = 0;
    const size_t ip_end = in_len;

    // state: 0 = 刚消费完"尾随字面量"（下一轮 token<16 是字面量跑）
    //        4 = 刚消费完"字面量跑"（下一轮 token<16 是 3 字节 M1 匹配）
    //      其它 = 刚消费完匹配
    size_t state = 0;
    size_t t = 0;
    size_t next = 0;
    size_t m_pos = 0;

    auto need_ip = [&](size_t n) { return ip_end - ip >= n; };
    auto need_op = [&](size_t n) { return out_cap - op >= n; };

    // 开头特殊情形：第一个字节 > 17，说明起始就是一段字面量。
    if (in[0] > 17) {
        t = in[ip++] - 17;
        if (t < 4) {
            // 起手 1..3 个字面量，且没有前置匹配。
            if (!need_ip(t) || !need_op(t)) {
                *out_len = op;
                return kLzoInputOverrun;
            }
            for (size_t i = 0; i < t; ++i) out[op++] = in[ip++];
            // 与 match_next 一致：state 记成 t，下一轮按"匹配后"处理。
            state = t;
            next = t;
            t = next;
            // 直接进入主循环的 match_next 逻辑：这里没有匹配，只把尾随字面量吃掉。
            // 已经拷贝过了，所以跳过一次。
            goto after_match_next;
        }
        if (!need_ip(t) || !need_op(t)) {
            *out_len = op;
            return kLzoInputOverrun;
        }
        for (size_t i = 0; i < t; ++i) out[op++] = in[ip++];
        state = 4;
    }

    while (ip < ip_end) {
        t = in[ip++];
        if (t < 16) {
            if (state == 0) {
                if (t == 0) {
                    // 字面量跑长度扩展：连续 0 字节每段代表 +255。
                    size_t zeros = 0;
                    while (ip < ip_end && in[ip] == 0) {
                        ++zeros;
                        ++ip;
                    }
                    if (!need_ip(1)) {
                        *out_len = op;
                        return kLzoInputOverrun;
                    }
                    t += zeros * 255 + 15 + in[ip++];
                }
                t += 3;
                if (!need_ip(t) || !need_op(t)) {
                    *out_len = op;
                    return (need_ip(t) ? kLzoOutputOverrun : kLzoInputOverrun);
                }
                for (size_t i = 0; i < t; ++i) out[op++] = in[ip++];
                state = 4;
                continue;
            } else if (state != 4) {
                // 2 字节匹配 + 尾随字面量（M1）
                if (!need_ip(1)) {
                    *out_len = op;
                    return kLzoInputOverrun;
                }
                next = t & 3;
                m_pos = op - 1 - (t >> 2) - (static_cast<size_t>(in[ip++]) << 2);
                if (m_pos > op || !need_op(2)) {
                    *out_len = op;
                    return (m_pos > op ? kLzoLookbehindOverrun : kLzoOutputOverrun);
                }
                out[op++] = out[m_pos++];
                out[op++] = out[m_pos++];
            } else {
                // 3 字节匹配（紧跟字面量跑）
                if (!need_ip(1)) {
                    *out_len = op;
                    return kLzoInputOverrun;
                }
                next = t & 3;
                m_pos = op - (1 + kM2MaxOffset) - (t >> 2)
                        - (static_cast<size_t>(in[ip++]) << 2);
                t = 3;
            }
        } else if (t >= 64) {
            if (!need_ip(1)) {
                *out_len = op;
                return kLzoInputOverrun;
            }
            next = t & 3;
            m_pos = op - 1 - ((t >> 2) & 7) - (static_cast<size_t>(in[ip++]) << 3);
            t = ((t >> 5) - 1) + 2;
        } else if (t >= 32) {
            t = (t & 31) + 2;
            if (t == 2) {
                size_t zeros = 0;
                while (ip < ip_end && in[ip] == 0) {
                    ++zeros;
                    ++ip;
                }
                if (!need_ip(1)) {
                    *out_len = op;
                    return kLzoInputOverrun;
                }
                t += zeros * 255 + 31 + in[ip++];
            }
            if (!need_ip(2)) {
                *out_len = op;
                return kLzoInputOverrun;
            }
            next = Rd16(in + ip);
            ip += 2;
            m_pos = op - 1 - (next >> 2);
            next &= 3;
        } else {
            if (!need_ip(2)) {
                *out_len = op;
                return kLzoInputOverrun;
            }
            next = Rd16(in + ip);
            m_pos = op - (static_cast<size_t>(t & 8) << 11);
            t = (t & 7) + 2;
            if (t == 2) {
                size_t zeros = 0;
                while (ip < ip_end && in[ip] == 0) {
                    ++zeros;
                    ++ip;
                }
                if (!need_ip(1)) {
                    *out_len = op;
                    return kLzoInputOverrun;
                }
                t += zeros * 255 + 7 + in[ip++];
                if (!need_ip(2)) {
                    *out_len = op;
                    return kLzoInputOverrun;
                }
                next = Rd16(in + ip);
            }
            ip += 2;
            m_pos -= next >> 2;
            next &= 3;
            if (m_pos == op) {
                // M4 偏移为 0 == 流结束标记
                *out_len = op;
                return kLzoOk;
            }
            m_pos -= 0x4000;
        }

        if (m_pos > op || !need_op(t)) {
            *out_len = op;
            return (m_pos > op ? kLzoLookbehindOverrun : kLzoOutputOverrun);
        }
        // 逐字节拷贝：匹配区间与输出区间允许重叠，不能用 memcpy。
        for (size_t i = 0; i < t; ++i) out[op++] = out[m_pos++];

        // match_next：匹配后还有 next 个字面量，直接从输入搬。
        state = next;
        t = next;
        if (t > 0) {
            if (!need_ip(t) || !need_op(t)) {
                *out_len = op;
                return (need_ip(t) ? kLzoOutputOverrun : kLzoInputOverrun);
            }
            for (size_t i = 0; i < t; ++i) out[op++] = in[ip++];
        }
    after_match_next:;
    }

    *out_len = op;
    return kLzoOk;
}

bool Lzo1x_Decompress_Chunks(const uint8_t* blob, size_t size,
                             std::vector<uint8_t>* out, const char** err) {
    out->clear();
    size_t p = 0;
    while (p + 4 <= size) {
        const size_t in_sz = Rd16(blob + p);
        const size_t out_sz = Rd16(blob + p + 2);
        p += 4;
        if (in_sz == 0 || out_sz == 0) {
            break;
        }
        if (p + in_sz > size) {
            if (err) *err = "LZO 块输入越界";
            return false;
        }
        const size_t base = out->size();
        out->resize(base + out_sz);
        size_t got = 0;
        const int rc = Lzo1x_Decompress(blob + p, in_sz, out->data() + base,
                                        out_sz, &got);
        if (rc != kLzoOk || got != out_sz) {
            out->resize(base + got);
            if (err) *err = "LZO 块解压长度与声明不符";
            return false;
        }
        p += in_sz;
    }
    return true;
}

}  // namespace ra2
