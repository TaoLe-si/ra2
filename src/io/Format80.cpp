// Format80.cpp -- 见 Format80.h。

#include "io/Format80.h"

namespace ra2 {
namespace {

inline uint16_t Rd16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

}  // namespace

int Format80_Decompress(const uint8_t* in, size_t in_len,
                        uint8_t* out, size_t out_cap, size_t* out_len) {
    if (in == nullptr || out == nullptr || out_len == nullptr) {
        return -1;
    }
    size_t ip = 0;
    size_t op = 0;
    while (ip < in_len) {
        const uint8_t cmd = in[ip++];
        if ((cmd & 0x80) == 0) {
            if (ip >= in_len) {
                return -4;
            }
            const int count = (cmd >> 4) + 3;
            const int offset = ((cmd & 0x0F) << 8) | in[ip++];
            // offset=0 合法：输出缓冲已零填，等价于写 0（Arena OverlayPack 实测有）。
            if (offset > 0 && op < static_cast<size_t>(offset)) {
                return -6;
            }
            for (int k = 0; k < count; ++k) {
                if (op >= out_cap) {
                    *out_len = op;
                    return -5;
                }
                out[op] = (offset == 0) ? 0
                                        : out[op - static_cast<size_t>(offset)];
                ++op;
            }
        } else if ((cmd & 0x40) == 0) {
            const int count = cmd & 0x3F;
            if (count == 0) {
                *out_len = op;
                return 0;
            }
            if (ip + static_cast<size_t>(count) > in_len) {
                return -4;
            }
            for (int k = 0; k < count; ++k) {
                if (op >= out_cap) {
                    *out_len = op;
                    return -5;
                }
                out[op++] = in[ip++];
            }
        } else {
            int count = cmd & 0x3F;
            if (count == 0x3E) {
                if (ip + 3 > in_len) {
                    return -4;
                }
                count = Rd16(in + ip);
                ip += 2;
                const uint8_t color = in[ip++];
                for (int k = 0; k < count; ++k) {
                    if (op >= out_cap) {
                        *out_len = op;
                        return -5;
                    }
                    out[op++] = color;
                }
            } else if (count == 0x3F) {
                if (ip + 4 > in_len) {
                    return -4;
                }
                count = Rd16(in + ip);
                ip += 2;
                const size_t pos = Rd16(in + ip);
                ip += 2;
                for (int k = 0; k < count; ++k) {
                    if (op >= out_cap || pos + static_cast<size_t>(k) >= out_cap) {
                        *out_len = op;
                        return -5;
                    }
                    out[op++] = out[pos + static_cast<size_t>(k)];
                }
            } else {
                count += 3;
                if (ip + 2 > in_len) {
                    return -4;
                }
                const int offset = Rd16(in + ip);
                ip += 2;
                if (offset > 0 && op < static_cast<size_t>(offset)) {
                    return -6;
                }
                for (int k = 0; k < count; ++k) {
                    if (op >= out_cap) {
                        *out_len = op;
                        return -5;
                    }
                    out[op] = (offset == 0) ? 0
                                            : out[op - static_cast<size_t>(offset)];
                    ++op;
                }
            }
        }
    }
    *out_len = op;
    return 0;
}

bool Format80_Decompress_Chunks(const uint8_t* blob, size_t size,
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
            if (err) *err = "Format80 块输入越界";
            return false;
        }
        const size_t base = out->size();
        out->resize(base + out_sz);
        size_t got = 0;
        const int rc = Format80_Decompress(blob + p, in_sz, out->data() + base,
                                           out_sz, &got);
        if (rc != 0 || got != out_sz) {
            out->resize(base + got);
            if (err) *err = "Format80 块解压长度与声明不符";
            return false;
        }
        p += in_sz;
    }
    return true;
}

}  // namespace ra2
