// Lzo1x.h -- LZO1X 解压（对应原引擎的 LZOStraw / LZOPipe）
//
// 为什么必须自己写：RA2/YR 地图的 [IsoMapPack5] / [OverlayPack] /
// [OverlayDataPack] 三段是**分块 LZO + base64**，原引擎用 LZOStraw 边读边解。
// modenc 的 IsoMapPack5 页面写明了是 miniLZO 的 lzo1x_decompress。
// 不实现它就一个瓦片都拿不到 —— 地图是"跑起来"的地基。
//
// 算法（LZO1X-1 位流）：
//   首字节 > 17       => 开头就是一段字面量，长度 = 字节 - 17
//   之后循环取一个 token 字节 t：
//     t < 16 且上一次是"匹配后的尾随字面量"  => 字面量跑（t 再加 3）
//     t < 16 且上一次是"字面量跑"           => 3 字节的 M1 匹配
//     t < 16 且其它（上一次是匹配）         => 2 字节匹配 + next 个尾随字面量
//     t >= 64                              => M2 匹配（1 字节偏移，长 3..8）
//     t in [32,63]                         => M3 匹配（2 字节偏移，长 3..33+）
//     t in [16,31]                         => M4 匹配（2 字节偏移，长 3..9+，
//                                              偏移 0 视为流结束）
//   每个匹配后面跟 (token & 3) 个"尾随字面量"—— 这是 LZO 把短字面量塞进
//   匹配 token 低 2 位的做法，也是解压器里最容易写漏的一环。
//
// 本实现按上述规则从零写出，并用真实地图做过端到端校验：
//   ARENA.map 的 IsoMapPack5 解压出 139924 字节
//   = ((80*2-1) * 80) * 11 + 4，与地图尺寸严丝合缝。

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ra2 {

enum LzoStatus : int {
    kLzoOk = 0,
    kLzoError = -1,
    kLzoInputOverrun = -4,
    kLzoOutputOverrun = -5,
    kLzoLookbehindOverrun = -6,
};

/// 解一个 LZO1X 块。
///
/// out_cap 是 out 缓冲的容量；成功时 *out_len 写成实际产出字节数。
/// 返回 kLzoOk 表示正常结束（遇到 M4 的"偏移 0"结束标记）。
int Lzo1x_Decompress(const uint8_t* in, size_t in_len,
                     uint8_t* out, size_t out_cap, size_t* out_len);

/// 解 RA2 的"分块"容器：重复 [u16 输入长度][u16 输出长度][输入数据]。
/// 每块都要求解压长度恰好等于声明的输出长度，否则整段判失败。
bool Lzo1x_Decompress_Chunks(const uint8_t* blob, size_t size,
                             std::vector<uint8_t>* out, const char** err = nullptr);

}  // namespace ra2
