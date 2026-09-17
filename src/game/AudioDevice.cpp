// AudioDevice.cpp -- AUD 解析 + WAV 合成 + PlaySound
#include "game/AudioDevice.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <mmreg.h>
#include <msacm.h>

#include "io/Format80.h"

#include <cstdio>
#include <cstring>

namespace ra2 {

// ---------------------------------------------------------------------------
// Westwood AUD —— 从 gamemd.exe 逆向的**真格式**（0x40AA70 解码器一族）：
//
//   文件头 12 字节：
//     +0  u16 采样率（实测 22050）
//     +2  u32 音频数据字节数（= 文件大小 - 12，NSWEEP 实测吻合）
//     +6  s16 初始样本（ADPCM 状态起点，NSWEEP = -8832）
//     +8  u8  初始步进索引（0..88）
//     +9  u8  ?（0）
//     +10 u8  ?（2）
//     +11 u8  ?（0x63 = 99）
//   块（紧排到数据尾）：
//     {u16 data_size; u16 flags; u32 0x0000DEAF; data[data_size]}
//     NSWEEP 实测：每块 512B 数据 + 8B 头，DEAF 间隔恰 520。
//   数据 = 4-bit ADPCM nibble（低半在前），算法/表格抄自 exe：
//     步进表  @0x816558（u32[89]，与标准 IMA 完全一致）
//     索引表  @0x816518（{-1,-1,-1,-1,2,4,6,8,×2}，同标准 IMA）
//     解码    @0x40ACD0：diff = step>>3 + (n&1?step>>2:0) + (n&2?step>>1:0)
//                        + (n&4?step:0)，n&8 取负；样本 clamp ±32767，
//                        索引 clamp 0..88（0x58）。
//   ADPCM 状态**跨块连续**（NSWEEP 验证：按此解码出 1kHz→0Hz 的核弹警报
//   扫频音，正是该音效的语义）。0x40AABF 另有一条"每声道块头
//   {s16, u8 idx, u8=0}"的路径（另一压缩模式的块内状态），
//   用 rsv==0 判别；本工程素材实测全部走连续模式。
// ---------------------------------------------------------------------------

bool Parse_Aud_Header(const uint8_t* data, size_t size, AudHeader* out) {
    if (data == nullptr || out == nullptr || size < 12) {
        return false;
    }
    const uint16_t rate = static_cast<uint16_t>(data[0] | (data[1] << 8));
    const uint32_t dsz = static_cast<uint32_t>(data[2]) |
                         (static_cast<uint32_t>(data[3]) << 8) |
                         (static_cast<uint32_t>(data[4]) << 16) |
                         (static_cast<uint32_t>(data[5]) << 24);
    if (rate == 0) {
        return false;
    }
    if (static_cast<size_t>(dsz) + 12 > size + 8) {   // 容 8B/块的头开销
        return false;
    }
    out->sample_rate = rate;
    out->channels = 1;      // AUD 音效实测单声道；立体声变体待样本
    out->pcm_size = 0;      // 解码后才知道
    out->compression = 2;
    return true;
}

namespace {
constexpr int kAudSteps[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41,
    45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209,
    230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876,
    963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749,
    3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630,
    9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
    27086, 29794, 32767};
constexpr int kAudIndex[16] = {-1, -1, -1, -1, 2, 4, 6, 8,
                               -1, -1, -1, -1, 2, 4, 6, 8};

inline uint16_t RdU16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
inline uint32_t RdU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}
}  // namespace

std::vector<uint8_t> Aud_To_Wav(const uint8_t* data, size_t size) {
    if (data == nullptr || size < 12) {
        return {};
    }
    const uint16_t rate = RdU16(data);
    int32_t sample = static_cast<int16_t>(RdU16(data + 6));
    int idx = data[8];
    if (idx > 88) idx = 88;

    std::vector<int16_t> pcm;
    pcm.push_back(static_cast<int16_t>(sample));   // 头里的初始样本也是第 0 个样本
    size_t p = 12;
    while (p + 8 <= size) {
        const uint16_t dsz = RdU16(data + p);
        const uint32_t deaf = RdU32(data + p + 4);
        if (deaf != 0x0000DEAFu) {
            // 不是块头：可能到尾了。扫下一个 DEAF（容错，防半块）。
            const uint8_t* q = data + p;
            const uint8_t* end = data + size;
            while (q + 4 <= end && RdU32(q) != 0x0000DEAFu) {
                ++q;
            }
            if (q + 4 > end) {
                break;
            }
            p = static_cast<size_t>(q - data) - 4;
            continue;
        }
        size_t take = dsz;
        if (take > size - p - 8) {
            take = size - p - 8;
        }
        for (size_t i = 0; i < take; ++i) {
            const uint8_t b = data[p + 8 + i];
            const int nibs[2] = {b & 0xF, b >> 4};
            for (int n : nibs) {
                const int step = kAudSteps[idx];
                int diff = step >> 3;
                if (n & 1) diff += step >> 2;
                if (n & 2) diff += step >> 1;
                if (n & 4) diff += step;
                sample += (n & 8) ? -diff : diff;
                if (sample > 32767) sample = 32767;
                if (sample < -32768) sample = -32768;
                idx += kAudIndex[n];
                if (idx < 0) idx = 0;
                if (idx > 88) idx = 88;
                pcm.push_back(static_cast<int16_t>(sample));
            }
        }
        p += 8 + take;
    }
    if (pcm.empty()) {
        return {};
    }

    const size_t pcm_bytes = pcm.size() * 2;
    std::vector<uint8_t> out(44 + pcm_bytes);
    auto w32 = [&](size_t o, uint32_t v) {
        out[o] = v & 0xFF; out[o + 1] = (v >> 8) & 0xFF;
        out[o + 2] = (v >> 16) & 0xFF; out[o + 3] = (v >> 24) & 0xFF;
    };
    auto w16 = [&](size_t o, uint16_t v) {
        out[o] = v & 0xFF; out[o + 1] = (v >> 8) & 0xFF;
    };
    std::memcpy(out.data(), "RIFF", 4);
    w32(4, 36 + static_cast<uint32_t>(pcm_bytes));
    std::memcpy(out.data() + 8, "WAVEfmt ", 8);
    w32(16, 16);
    w16(20, 1);   // PCM
    w16(22, 1);   // mono
    w32(24, rate);
    w32(28, rate * 2);
    w16(32, 2);
    w16(34, 16);
    std::memcpy(out.data() + 36, "data", 4);
    w32(40, static_cast<uint32_t>(pcm_bytes));
    std::memcpy(out.data() + 44, pcm.data(), pcm_bytes);
    return out;
}

void Play_Wav_Memory(const std::vector<uint8_t>& wav, const char* snd_alias) {
    if (wav.empty()) {
        return;
    }
    // PlaySound 需要 buffer 常驻到播放结束。这里做一份静态缓冲：
    // 多个音效同时触发会覆盖，足够"点一下响一声"的场景。
    // 注：原版 @0x750920 是 DirectSound 多缓冲混音；这里走 SND_MEMORY 单缓冲
    //     是为了让"音频设备已接"这件事有落地，不再是纯桩。
    static std::vector<uint8_t> s_buf;
    s_buf = wav;  // 持有引用
    const BOOL ok = ::PlaySoundA(reinterpret_cast<LPCSTR>(s_buf.data()), nullptr,
                                 SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
    if (!ok) {
        std::fprintf(stderr, "[x] PlaySound 失败 alias=%s\n",
                     snd_alias ? snd_alias : "?");
    }
}

void Play_Music_Loop(const std::vector<uint8_t>& wav, const char* name) {
    if (wav.empty()) {
        return;
    }
    static std::vector<uint8_t> s_music;
    s_music = wav;
    const BOOL ok = ::PlaySoundA(reinterpret_cast<LPCSTR>(s_music.data()), nullptr,
                                  SND_MEMORY | SND_ASYNC | SND_LOOP | SND_NODEFAULT);
    if (!ok) {
        std::fprintf(stderr, "[x] PlaySound(music) 失败 %s\n", name ? name : "?");
    } else {
        std::printf("  [music] 循环播放 %s（%zu 字节）\n", name ? name : "?", wav.size());
    }
}

// ---------------------------------------------------------------------------
// IMA ADPCM（WAVE_FORMAT tag 0x11）→ 16-bit PCM。
//
// THEME.MIX 的曲目全是这种（22050Hz / 立体声 / 4-bit / 块 1024 字节）。
// 解码走 Windows ACM（见 Ima_Adpcm_To_Pcm_Wav 里的注释），
// 这里只留 PCM → WAV 的封装。
// ---------------------------------------------------------------------------
namespace {
std::vector<uint8_t> Wrap_Pcm_Wav(const uint8_t* pcm, size_t pcm_bytes,
                                  uint16_t channels, uint32_t rate) {
    std::vector<uint8_t> out(44 + pcm_bytes);
    auto w32 = [&](size_t o, uint32_t v) {
        out[o] = v & 0xFF; out[o + 1] = (v >> 8) & 0xFF;
        out[o + 2] = (v >> 16) & 0xFF; out[o + 3] = (v >> 24) & 0xFF;
    };
    auto w16 = [&](size_t o, uint16_t v) {
        out[o] = v & 0xFF; out[o + 1] = (v >> 8) & 0xFF;
    };
    std::memcpy(out.data(), "RIFF", 4);
    w32(4, 36 + static_cast<uint32_t>(pcm_bytes));
    std::memcpy(out.data() + 8, "WAVEfmt ", 8);
    w32(16, 16);
    w16(20, 1);   // PCM
    w16(22, channels);
    w32(24, rate);
    w32(28, rate * channels * 2);
    w16(32, static_cast<uint16_t>(channels * 2));
    w16(34, 16);
    std::memcpy(out.data() + 36, "data", 4);
    w32(40, static_cast<uint32_t>(pcm_bytes));
    std::memcpy(out.data() + 44, pcm, pcm_bytes);
    return out;
}
}  // namespace

std::vector<uint8_t> Ima_Adpcm_To_Pcm_Wav(const uint8_t* wav, size_t size) {
    if (wav == nullptr || size < 44 || std::memcmp(wav, "RIFF", 4) != 0 ||
        std::memcmp(wav + 8, "WAVE", 4) != 0) {
        return {};
    }
    // 找 fmt / data 子块
    const uint8_t* fmt = nullptr;
    uint32_t fmt_len = 0;
    const uint8_t* data = nullptr;
    size_t data_len = 0;
    size_t p = 12;
    while (p + 8 <= size) {
        const uint32_t chunk = (static_cast<uint32_t>(wav[p]) |
                               (wav[p + 1] << 8) | (wav[p + 2] << 16) |
                               (static_cast<uint32_t>(wav[p + 3]) << 24));
        const uint32_t clen = (static_cast<uint32_t>(wav[p + 4]) |
                              (wav[p + 5] << 8) | (wav[p + 6] << 16) |
                              (static_cast<uint32_t>(wav[p + 7]) << 24));
        if (chunk == 0x20746D66 && p + 8 + clen <= size) {   // "fmt "
            fmt = wav + p + 8;
            fmt_len = clen;
        } else if (chunk == 0x61746164) {                     // "data"
            data = wav + p + 8;
            data_len = (clen <= size - p - 8) ? clen : size - p - 8;
        }
        p += 8 + clen + (clen & 1);
    }
    if (fmt == nullptr || fmt_len < 16 || data == nullptr || data_len == 0) {
        return {};
    }

    // 【为什么走 ACM 而不是手解 nibble】THEME.MIX 曲目是立体声 IMA（tag 0x11），
    // 双声道 nibble 的交错规则文档稀烂、实测三种排布全部对不上（声道相关性 ~0，
    // 解出来贴满轨）。ACM（msacm32）是 Windows 自带的官方解码路径，
    // 原版 DirectSound 播 ADPCM 也是靠它 —— 用它就是贴着原版走。
    WAVEFORMATEX src{};
    std::memcpy(&src, fmt, (fmt_len < sizeof(src)) ? fmt_len : sizeof(src));
    if (src.wFormatTag == WAVE_FORMAT_IMA_ADPCM) {
        // IMA 的 fmt 比 WAVEFORMATEX 多 cbSize(2)+samples_per_block(2)，
        // ACM 要原始字节（含扩展）才认 —— 直接把 fmt 块内容当源格式描述。
        WAVEFORMATEX* src_ext = reinterpret_cast<WAVEFORMATEX*>(
            const_cast<uint8_t*>(fmt));
        (void)src_ext;
        WAVEFORMATEX dst{};
        dst.wFormatTag = WAVE_FORMAT_PCM;
        dst.nChannels = src.nChannels;
        dst.nSamplesPerSec = src.nSamplesPerSec;
        dst.wBitsPerSample = 16;
        dst.nBlockAlign = static_cast<WORD>(src.nChannels * 2);
        dst.nAvgBytesPerSec = dst.nSamplesPerSec * dst.nBlockAlign;
        dst.cbSize = 0;
        HACMSTREAM hs = nullptr;
        if (acmStreamOpen(&hs, nullptr, reinterpret_cast<LPWAVEFORMATEX>(
                                          const_cast<uint8_t*>(fmt)),
                          &dst, 0, 0, 0, 0) == 0 &&
            hs != nullptr) {
            // ACM 要求 dst 缓冲按 dst 块对齐的倍数给；一次给 4MB。
            const size_t chunk_in = 4 << 20;
            std::vector<uint8_t> pcm;
            size_t done = 0;
            while (done < data_len) {
                const size_t in_n = (data_len - done < chunk_in)
                                        ? (data_len - done) : chunk_in;
                ACMSTREAMHEADER hdr{};
                hdr.cbStruct = sizeof(hdr);
                hdr.pbSrc = const_cast<uint8_t*>(data + done);
                hdr.cbSrcLength = static_cast<DWORD>(in_n);
                // 输出上限：输入字节 × 4（4-bit→16-bit × 2 声道）+ 头部开销。
                std::vector<uint8_t> out(in_n * 4 + 65536);
                hdr.pbDst = out.data();
                hdr.cbDstLength = static_cast<DWORD>(out.size());
                if (acmStreamPrepareHeader(hs, &hdr, 0) != 0) {
                    break;
                }
                const MMRESULT mr = acmStreamConvert(hs, &hdr, 0);
                acmStreamUnprepareHeader(hs, &hdr, 0);
                if (mr != 0 || hdr.cbDstLengthUsed == 0) {
                    break;
                }
                pcm.insert(pcm.end(), out.begin(),
                           out.begin() + hdr.cbDstLengthUsed);
                if (hdr.cbSrcLengthUsed == 0) {
                    break;
                }
                done += hdr.cbSrcLengthUsed;
            }
            acmStreamClose(hs, 0);
            if (!pcm.empty()) {
                return Wrap_Pcm_Wav(pcm.data(), pcm.size(), dst.nChannels,
                                    dst.nSamplesPerSec);
            }
        }
        std::fprintf(stderr,
                     "[music] ACM 解码失败（msacm32 不认这个 IMA 头），跳过\n");
        return {};
    }
    if (src.wFormatTag == WAVE_FORMAT_PCM) {
        // 已是 PCM：原样包回 WAV。
        return Wrap_Pcm_Wav(data, data_len, src.nChannels, src.nSamplesPerSec);
    }
    return {};
}

}  // namespace ra2