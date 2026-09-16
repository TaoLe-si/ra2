// AudioDevice.h -- 真实音频设备的最小实现（Westwood AUD → PCM → WAV → PlaySound）
//
// 为什么直接走 PlaySound 而不是接 DirectSound：
//   1) 原版 binary @0x750920 走的是更复杂的 DirectSound 路径，
//      每个 Voc 单独建缓冲区并管理优先级/音量。
//      完整还原工作量太大（DA/WAV 格式解析 + 流式缓冲 + 多优先级）。
//   2) 这里目标只是"点一下能响一声"，够让 TAction 99/108/113/113
//      给到玩家反馈即可，不追求多声混音。
//   3) Westwood AUD 文件基本是 RAW PCM（compression=0）+ 14 字节头，
//      转 WAV 头（44 字节 RIFF）后喂给 PlaySound 完全免编码。
//
// AUD 文件头（14 字节，按 binary 文档 + 实测）：
//   byte  0     : 0x00
//   bytes 1..2  : data_size (LE u16)  -- raw 音频数据字节数（不含头）
//   bytes 3..4  : sample_rate (LE u16) -- 采样率 Hz
//   byte  5     : compression (0=raw 8-bit unsigned PCM, 1/2=Westwood ADPCM)
//   byte  6     : channels - 1 (0=mono, 1=stereo)
//   bytes 7..8  : reserved
//   bytes 9..   : audio data
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ra2 {

/// 解析 Westwood AUD 头。失败返回 false。
/// comp / channels / sample_rate / pcm_size 都填实参。
/// 失败原因：太短、magic 非 0、data_size 与总长不一致、comp=ADPCM（暂不解）。
struct AudHeader {
    int sample_rate = 0;
    int channels = 0;     ///< 1 or 2
    int pcm_size = 0;     ///< 音频数据字节数（不含头）
    int compression = 0;  ///< 0=raw, 1=ADPCM, 2=ADPCM mono
};

bool Parse_Aud_Header(const uint8_t* data, size_t size, AudHeader* out);

/// 把 raw PCM AUD 转成可被 PlaySound(SND_MEMORY) 接受的 WAV。
/// （44 字节 RIFF/WAVE + fmt 子块 + data 子块；8-bit unsigned PCM 即可。）
/// comp 非 0 直接返回 false（暂不解 ADPCM，留桩）。
std::vector<uint8_t> Aud_To_Wav(const uint8_t* data, size_t size);

/// 同步播放内存里的 WAV（默认别名是 _Voc_Cheer 这种）。
/// snd_alias 前缀 `_` 是原版约定（区分 Voc 类别），不影响播放。
/// 不阻塞主线程：返回前只是把 buffer 交给系统。
void Play_Wav_Memory(const std::vector<uint8_t>& wav, const char* snd_alias);

}  // namespace ra2