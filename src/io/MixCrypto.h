// MixCrypto.h -- 加密 MIX 的密钥派生与解密
//
// 这是整个还原工程的第一道门：ra2.mix / ra2md.mix / expandmd01.mix 全部带
// 0x00020000（加密）标志，索引是密文，拿不到密钥就等于看不到任何游戏素材。
//
// 【格式】（已用 tools/mixdump.py 在真实文件上验证）
//   偏移 0    4 字节   flags（明文，小端）
//   偏移 4    80 字节  RSA 加密的密钥源：两个 40 字节**小端**大整数
//   偏移 84           Blowfish 加密的索引：
//                       u16 文件数 + u32 数据区长度 + 文件数×12 字节条目
//                     尾部补零到 8 字节对齐
//   若 flags 带 0x00010000，文件末尾还有 20 字节 SHA1（data_size 不含它）
//
// 【RSA】m = c^65537 mod n。
//   n 来自 base64 公钥 "AihRvNoIbTn85FZRYNZRcT+i6KpU+maCsEqr3Q5q+LDB5tH7Tz2qQ38V"：
//   解码后 42 字节，去掉 DER 头 "02 28" 剩下 40 字节大端整数（319 bit）。
//   每个分组结果取 39 字节小端（= (bitlen(n)-1)/8），两个拼起来取前 56 字节。
//   这个私钥就放在每个 MIX 里的 [PublicKey]/[PrivateKey] 文件中（实测取出过，
//   id=0x763C81DD），是算法正确性的铁证。
//
// 【Blowfish】56 字节密钥，标准大端 ECB。
//
// 实现选择：没有引入任何第三方库，大数运算自己写（固定 1024 位、小端 limb），
// 因为整个工程只用到一次 320 位模幂，引入 OpenSSL/GMP 不值得。

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ra2 {

/// Blowfish（标准大端），只实现解密 —— MIX 只需要解索引。
///
/// P/S 盒初值来自 BlowfishTables.h。
class Blowfish {
public:
    explicit Blowfish(const uint8_t* key, size_t key_len);

    /// ECB 解密。len 必须是 8 的倍数。
    void Decrypt(const uint8_t* in, uint8_t* out, size_t len) const;

    /// 就地解密一段（同样要求 8 字节对齐）。
    void Decrypt_In_Place(uint8_t* buf, size_t len) const;

private:
    void Encrypt_Block(uint32_t& l, uint32_t& r) const;
    uint32_t F(uint32_t x) const;

    uint32_t p_[18];
    uint32_t s_[4][256];
};

/// 从 MIX 头部（至少 84 字节）推出 56 字节 Blowfish 密钥。
/// 失败（密文分组 >= 模数，说明不是真的加密 MIX）返回空。
std::vector<uint8_t> Derive_Mix_Key(const uint8_t* header84);

/// Westwood 文件名 CRC —— MIX 用 32 位 ID 代替文件名，这个 ID 就是它。
///
/// 反射 CRC32（多项式 0xEDB88320），初值 0xFFFFFFFF、末尾取反，
/// 且文件名在参与计算前要转大写并做 4 字节对齐填充：
///   余 rem 字节时，补 [rem] 再补 (3-rem) 个"本组首字节"。
/// 这条填充规则是靠实测站住的：用 gamemd.exe 里抽出的文件名去撞真实 MIX，
/// 392 个 ID 命中 178 个（见 tools/mixnames.py），算法错一个字节都不可能命中。
uint32_t Westwood_CRC(const char* name);

}  // namespace ra2
