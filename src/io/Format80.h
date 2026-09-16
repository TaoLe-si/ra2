// Format80.h -- Westwood LCW / Format80 解压
//
// IsoMapPack5 / PreviewPack 是分块 LZO；OverlayPack / OverlayDataPack 外壳
// 同样是 [u16 in][u16 out][payload]，但 payload 是 Format80，不是 LZO。
// Arena.mmx 实测：首块 `09 00 00 20 81 FF FE FE 1F FF 81 FF 80` = 8192 个 0xFF，
// 整段解出 262144 = 512×512 字节。用 LZO 解会 input overrun。
//
// 命令字节（OpenRA Format80 / XCC 同一套）：
//   0xxxxxxx           短回抄：(cmd>>4)+3 字节，偏移 ((cmd&15)<<8 | next)
//   10cccccc c!=0      字面量拷贝 c 字节
//   10000000           结束
//   11111110           填充：u16 长度 + 1 字节色值
//   11111111           绝对回抄：u16 长度 + u16 dest 下标
//   11cccccc c<0x3E    相对回抄：c+3 字节，偏移 u16

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ra2 {

int Format80_Decompress(const uint8_t* in, size_t in_len,
                        uint8_t* out, size_t out_cap, size_t* out_len);

/// 与 Lzo1x_Decompress_Chunks 相同的分块外壳，块内走 Format80。
bool Format80_Decompress_Chunks(const uint8_t* blob, size_t size,
                                std::vector<uint8_t>* out,
                                const char** err = nullptr);

}  // namespace ra2
