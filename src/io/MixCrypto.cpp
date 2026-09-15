// MixCrypto.cpp
//
// 参考实现是 tools/mixdump.py（已跑通真实文件）；这里是它的 C++ 直译。
// 两边任何一处不一致，ra2md.mix 的 (文件数, 数据区长度) 就会解错，
// 所以 src/main.cpp 里挂了一条 static_assert 之外的运行时回归测试。

#include "io/MixCrypto.h"

#include "io/BlowfishTables.h"

#include <cstring>

namespace ra2 {
namespace {

// ---------------------------------------------------------------------------
// 固定宽度大数：1024 位，小端 uint32 limb
// ---------------------------------------------------------------------------
// 只为一次 320 位模幂服务，所以用最朴素的算法：
//   乘法 = 教科书逐位乘；取模 = 移位-比较-减法。
// 规模小（e=65537 只要 17 次平方 + 少量乘法），正确性优先于速度。

constexpr int kLimbs = 32;  // 1024 bit

struct Big {
    uint32_t w[kLimbs] = {};
};

int Bit_Len(const Big& a) {
    for (int i = kLimbs - 1; i >= 0; --i) {
        if (a.w[i]) {
            uint32_t v = a.w[i];
            int n = 0;
            while (v) {
                v >>= 1;
                ++n;
            }
            return i * 32 + n;
        }
    }
    return 0;
}

bool Is_Zero(const Big& a) {
    for (uint32_t v : a.w) {
        if (v) {
            return false;
        }
    }
    return true;
}

/// a >= b ?
bool Ge(const Big& a, const Big& b) {
    for (int i = kLimbs - 1; i >= 0; --i) {
        if (a.w[i] != b.w[i]) {
            return a.w[i] > b.w[i];
        }
    }
    return true;
}

/// a -= b（假设 a >= b）
void Sub(Big& a, const Big& b) {
    uint64_t borrow = 0;
    for (int i = 0; i < kLimbs; ++i) {
        const uint64_t cur = static_cast<uint64_t>(a.w[i]) - b.w[i] - borrow;
        a.w[i] = static_cast<uint32_t>(cur);
        borrow = (cur >> 32) & 1;
    }
}

/// a <<= 1
void Shl1(Big& a) {
    uint32_t carry = 0;
    for (int i = 0; i < kLimbs; ++i) {
        const uint32_t v = a.w[i];
        a.w[i] = (v << 1) | carry;
        carry = v >> 31;
    }
}

/// a >>= 1
void Shr1(Big& a) {
    uint32_t carry = 0;
    for (int i = kLimbs - 1; i >= 0; --i) {
        const uint32_t v = a.w[i];
        a.w[i] = (v >> 1) | carry;
        carry = v << 31;
    }
}

/// out = a * b（两个操作数都必须 < 2^512，否则溢出）
void Mul(const Big& a, const Big& b, Big& out) {
    Big t;
    for (int i = 0; i < kLimbs / 2; ++i) {
        if (!a.w[i]) {
            continue;
        }
        uint64_t carry = 0;
        for (int j = 0; j < kLimbs / 2; ++j) {
            const uint64_t cur = static_cast<uint64_t>(a.w[i]) * b.w[j] +
                                 t.w[i + j] + carry;
            t.w[i + j] = static_cast<uint32_t>(cur);
            carry = cur >> 32;
        }
        int k = i + kLimbs / 2;
        while (carry && k < kLimbs) {
            const uint64_t cur = static_cast<uint64_t>(t.w[k]) + carry;
            t.w[k] = static_cast<uint32_t>(cur);
            carry = cur >> 32;
            ++k;
        }
    }
    out = t;
}

/// num %= mod（移位-比较-减法；num 可以比 mod 长得多）
void Mod(Big& num, const Big& mod) {
    const int nb = Bit_Len(num);
    const int mb = Bit_Len(mod);
    if (mb == 0 || nb < mb) {
        return;
    }
    Big shifted = mod;
    for (int i = 0; i < nb - mb; ++i) {
        Shl1(shifted);
    }
    for (int i = 0; i <= nb - mb; ++i) {
        if (Ge(num, shifted)) {
            Sub(num, shifted);
        }
        Shr1(shifted);
    }
}

/// out = base^exp mod mod
void Mod_Pow(const Big& base, uint32_t exp, const Big& mod, Big& out) {
    Big result;
    result.w[0] = 1;
    Big cur = base;
    Mod(cur, mod);
    while (exp) {
        if (exp & 1u) {
            Big t;
            Mul(result, cur, t);
            Mod(t, mod);
            result = t;
        }
        Big t;
        Mul(cur, cur, t);
        Mod(t, mod);
        cur = t;
        exp >>= 1;
    }
    out = result;
}

// ---------------------------------------------------------------------------
// RSA 公钥
// ---------------------------------------------------------------------------
// base64 "AihRvNoIbTn85FZRYNZRcT+i6KpU+maCsEqr3Q5q+LDB5tH7Tz2qQ38V"
//   -> 42 字节，前两字节是 DER 头 02 28（INTEGER, 40 字节），剩下 40 字节是模数。
// 字节值由 base64 直接解码得到（勿手抄）。校验：末字节 0x15，模数 319 bit。
constexpr uint8_t kPublicKeyDER[42] = {
    0x02, 0x28, 0x51, 0xBC, 0xDA, 0x08, 0x6D, 0x39, 0xFC, 0xE4, 0x56, 0x51,
    0x60, 0xD6, 0x51, 0x71, 0x3F, 0xA2, 0xE8, 0xAA, 0x54, 0xFA, 0x66, 0x82,
    0xB0, 0x4A, 0xAB, 0xDD, 0x0E, 0x6A, 0xF8, 0xB0, 0xC1, 0xE6, 0xD1, 0xFB,
    0x4F, 0x3D, 0xAA, 0x43, 0x7F, 0x15,
};
static_assert(sizeof(kPublicKeyDER) == 42, "公钥应为 42 字节");
static_assert(kPublicKeyDER[0] == 0x02 && kPublicKeyDER[1] == 0x28,
              "DER 头应为 02 28（INTEGER, 40 字节）");

void Load_Modulus(Big& n) {
    // DER 之后是 40 字节**大端**整数。
    for (int i = 0; i < 40; ++i) {
        const int byte_index = 2 + 39 - i;
        const int limb = i / 4;
        const int shift = (i % 4) * 8;
        n.w[limb] |= static_cast<uint32_t>(kPublicKeyDER[byte_index]) << shift;
    }
}

/// 把 40 字节**小端**字节串读成大数。
void Load_LE(const uint8_t* src, int bytes, Big& out) {
    for (int i = 0; i < bytes; ++i) {
        const int limb = i / 4;
        const int shift = (i % 4) * 8;
        out.w[limb] |= static_cast<uint32_t>(src[i]) << shift;
    }
}

/// 把大数写成 bytes 字节**小端**（高位截断）。
void Store_LE(const Big& v, int bytes, uint8_t* out) {
    for (int i = 0; i < bytes; ++i) {
        out[i] = static_cast<uint8_t>((v.w[i / 4] >> ((i % 4) * 8)) & 0xFF);
    }
}

// ---------------------------------------------------------------------------
// CRC 表（反射 CRC32，多项式 0xEDB88320）
// ---------------------------------------------------------------------------
uint32_t g_crc_table[256];
bool g_crc_ready = false;

void Init_CRC() {
    if (g_crc_ready) {
        return;
    }
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int k = 0; k < 8; ++k) {
            c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        g_crc_table[i] = c;
    }
    g_crc_ready = true;
}

}  // namespace

// ---------------------------------------------------------------------------
// Blowfish
// ---------------------------------------------------------------------------
Blowfish::Blowfish(const uint8_t* key, size_t key_len) {
    if (key_len == 0) {
        return;  // 空密钥：保持初始值（调用方不应走到这里）
    }
    for (int i = 0; i < 18; ++i) {
        p_[i] = detail::kBlowfishP[i];
    }
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 256; ++j) {
            s_[i][j] = detail::kBlowfishS[i][j];
        }
    }

    const size_t kl = key_len;
    for (int i = 0; i < 18; ++i) {
        uint32_t k = 0;
        for (int j = 0; j < 4; ++j) {
            k = (k << 8) | key[(static_cast<size_t>(i) * 4 + j) % kl];
        }
        p_[i] ^= k;
    }

    uint32_t l = 0, r = 0;
    for (int i = 0; i < 18; i += 2) {
        Encrypt_Block(l, r);
        p_[i] = l;
        p_[i + 1] = r;
    }
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 256; j += 2) {
            Encrypt_Block(l, r);
            s_[i][j] = l;
            s_[i][j + 1] = r;
        }
    }
}

uint32_t Blowfish::F(uint32_t x) const {
    const uint32_t a = s_[0][(x >> 24) & 0xFF];
    const uint32_t b = s_[1][(x >> 16) & 0xFF];
    const uint32_t c = s_[2][(x >> 8) & 0xFF];
    const uint32_t d = s_[3][x & 0xFF];
    return ((a + b) ^ c) + d;
}

void Blowfish::Encrypt_Block(uint32_t& l, uint32_t& r) const {
    for (int i = 0; i < 16; ++i) {
        l ^= p_[i];
        r ^= F(l);
        uint32_t t = l;
        l = r;
        r = t;
    }
    uint32_t t = l;
    l = r;
    r = t;
    r ^= p_[16];
    l ^= p_[17];
}

void Blowfish::Decrypt(const uint8_t* in, uint8_t* out, size_t len) const {
    for (size_t i = 0; i + 8 <= len; i += 8) {
        uint32_t l = (static_cast<uint32_t>(in[i]) << 24) |
                     (static_cast<uint32_t>(in[i + 1]) << 16) |
                     (static_cast<uint32_t>(in[i + 2]) << 8) |
                     static_cast<uint32_t>(in[i + 3]);
        uint32_t r = (static_cast<uint32_t>(in[i + 4]) << 24) |
                     (static_cast<uint32_t>(in[i + 5]) << 16) |
                     (static_cast<uint32_t>(in[i + 6]) << 8) |
                     static_cast<uint32_t>(in[i + 7]);
        for (int k = 17; k > 1; --k) {
            l ^= p_[k];
            r ^= F(l);
            uint32_t t = l;
            l = r;
            r = t;
        }
        uint32_t t = l;
        l = r;
        r = t;
        r ^= p_[1];
        l ^= p_[0];
        out[i] = static_cast<uint8_t>(l >> 24);
        out[i + 1] = static_cast<uint8_t>(l >> 16);
        out[i + 2] = static_cast<uint8_t>(l >> 8);
        out[i + 3] = static_cast<uint8_t>(l);
        out[i + 4] = static_cast<uint8_t>(r >> 24);
        out[i + 5] = static_cast<uint8_t>(r >> 16);
        out[i + 6] = static_cast<uint8_t>(r >> 8);
        out[i + 7] = static_cast<uint8_t>(r);
    }
}

void Blowfish::Decrypt_In_Place(uint8_t* buf, size_t len) const {
    std::vector<uint8_t> tmp(len);
    Decrypt(buf, tmp.data(), len);
    std::memcpy(buf, tmp.data(), len);
}

// ---------------------------------------------------------------------------
// 密钥派生
// ---------------------------------------------------------------------------
std::vector<uint8_t> Derive_Mix_Key(const uint8_t* header84) {
    constexpr int kSourceLen = 80;
    constexpr int kChunk = 40;
    constexpr int kOut = 39;   // (bitlen(n)-1)/8
    constexpr int kKeyLen = 56;

    Big n;
    Load_Modulus(n);

    std::vector<uint8_t> key;
    key.reserve(kKeyLen);
    for (int i = 0; i < kSourceLen; i += kChunk) {
        Big c;
        Load_LE(header84 + 4 + i, kChunk, c);
        if (Ge(c, n)) {
            return {};  // 密文 >= 模数，不是真的加密 MIX
        }
        Big m;
        Mod_Pow(c, 65537, n, m);
        uint8_t buf[kOut];
        Store_LE(m, kOut, buf);
        for (int j = 0; j < kOut && key.size() < static_cast<size_t>(kKeyLen); ++j) {
            key.push_back(buf[j]);
        }
    }
    return key;
}

// ---------------------------------------------------------------------------
// Westwood CRC
// ---------------------------------------------------------------------------
uint32_t Westwood_CRC(const char* name) {
    Init_CRC();

    // 转大写并做 4 字节对齐填充。
    uint8_t buf[512];
    size_t len = 0;
    for (const char* p = name; *p && len + 8 < sizeof(buf); ++p) {
        unsigned char c = static_cast<unsigned char>(*p);
        if (c >= 'a' && c <= 'z') {
            c = static_cast<unsigned char>(c - 'a' + 'A');
        }
        buf[len++] = c;
    }
    const size_t rem = len % 4;
    if (rem != 0) {
        const uint8_t pad = buf[len - rem];  // 末组首字节
        buf[len++] = static_cast<uint8_t>(rem);
        for (size_t i = 0; i < 3 - rem; ++i) {
            buf[len++] = pad;
        }
    }

    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc = g_crc_table[(crc ^ buf[i]) & 0xFFu] ^ (crc >> 8);
    }
    return ~crc;
}

}  // namespace ra2
