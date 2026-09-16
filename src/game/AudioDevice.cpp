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

bool Parse_Aud_Header(const uint8_t* data, size_t size, AudHeader* out) {
    if (data == nullptr || out == nullptr || size < 14) {
        std::fprintf(stderr, "[aud] Parse fail: data=%p size=%zu < 14\n", data, size);
        return false;
    }
    if (data[0] != 0x00) {
        std::fprintf(stderr, "[aud] Parse fail: magic=0x%02X != 0x00\n", data[0]);
        return false;
    }
    const uint16_t data_size = static_cast<uint16_t>(data[1] | (data[2] << 8));
    const uint16_t sample_rate = static_cast<uint16_t>(data[3] | (data[4] << 8));
    const uint8_t comp =  data[5];
    const uint8_t ch = static_cast<uint8_t>(data[6] + 1);
    const uint16_t reserved = static_cast<uint16_t>(data[7] | (data[8] << 8));

    if (sample_rate == 0 || (ch != 1 && ch != 2)) {
        std::fprintf(stderr, "[aud] Parse fail: rate=%u ch=%u\n", sample_rate, ch);
        return false;
    }
    if (static_cast<size_t>(data_size) + 14 > size) {
        std::fprintf(stderr, "[aud] Parse fail: data_size=%u + 14 > size=%zu\n",
                     data_size, size);
        return false;
    }
    if (comp != 0) {
        std::fprintf(stderr, "[aud] Parse fail: comp=%u (ADPCM not impl)\n", comp);
        return false;
    }
    if (reserved != 0) {
        std::fprintf(stderr, "[aud] Parse fail: reserved=%u\n", reserved);
        return false;
    }

    out->sample_rate = sample_rate;
    out->channels = ch;
    out->pcm_size = data_size;
    out->compression = 0;
    return true;
}

std::vector<uint8_t> Aud_To_Wav(const uint8_t* data, size_t size) {
    std::vector<uint8_t> raw_aud;
    const char* err = nullptr;
    const bool f80 = Format80_Decompress_Chunks(data, size, &raw_aud, &err);
    std::fprintf(stderr, "[aud] Format80_Decompress_Chunks=%d raw_aud.size=%zu err=%s\n",
                 f80 ? 1 : 0, raw_aud.size(), err ? err : "(null)");
    if (f80 && !raw_aud.empty() && raw_aud.size() >= 14 && raw_aud[0] == 0x00) {
        // OK
    } else if (data != nullptr && size >= 14 && data[0] == 0x00) {
        raw_aud.assign(data, data + size);
    } else {
        // 兜底：直接当成裸 22050Hz mono unsigned 8-bit PCM。
        raw_aud.assign(data, data + size);
        std::vector<uint8_t> hdr(14, 0);
        hdr[0] = 0x00;
        const uint16_t pcm_size = static_cast<uint16_t>(raw_aud.size());
        hdr[1] = static_cast<uint8_t>(pcm_size & 0xFF);
        hdr[2] = static_cast<uint8_t>((pcm_size >> 8) & 0xFF);
        const uint16_t rate = 22050;
        hdr[3] = static_cast<uint8_t>(rate & 0xFF);
        hdr[4] = static_cast<uint8_t>((rate >> 8) & 0xFF);
        hdr[5] = 0x00;
        hdr[6] = 0x00;
        hdr[7] = 0x00;
        hdr[8] = 0x00;
        std::vector<uint8_t> with_hdr;
        with_hdr.reserve(14 + raw_aud.size());
        with_hdr.insert(with_hdr.end(), hdr.begin(), hdr.end());
        with_hdr.insert(with_hdr.end(), raw_aud.begin(), raw_aud.end());
        raw_aud = std::move(with_hdr);
        std::fprintf(stderr, "[aud] fallback: synthesized hdr+PCM total=%zu (pcm=%u)\n",
                     raw_aud.size(), pcm_size);
    }
    AudHeader h;
    if (!Parse_Aud_Header(raw_aud.data(), raw_aud.size(), &h)) {
        return {};
    }
    // 8-bit unsigned PCM mono/stereo → WAV 极简头：
    //   "RIFF" + 文件大小（LE u32）+ "WAVE"
    //   "fmt " + 16 (LE u32) + 1 (PCM) + channels + rate + byte_rate + block_align + 8 (bits/sample)
    //   "data" + 子块大小（LE u32）+ PCM 数据
    constexpr int kHdr = 44;
    std::vector<uint8_t> out;
    out.resize(kHdr + h.pcm_size);
    uint8_t* p = out.data();

    auto put = [&](size_t off, const char* s) {
        std::memcpy(p + off, s, 4);
    };
    auto put16 = [&](size_t off, uint16_t v) {
        p[off]     = static_cast<uint8_t>(v & 0xFF);
        p[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    };
    auto put32 = [&](size_t off, uint32_t v) {
        p[off]     = static_cast<uint8_t>(v & 0xFF);
        p[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
        p[off + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
        p[off + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
    };

    const uint32_t byte_rate = static_cast<uint32_t>(h.sample_rate) * h.channels;
    const uint16_t block_align = static_cast<uint16_t>(h.channels);

    put(0, "RIFF");
    put32(4, 36u + static_cast<uint32_t>(h.pcm_size));
    put(8, "WAVE");
    put(12, "fmt ");
    put32(16, 16u);          // fmt chunk size
    put16(20, 1u);           // PCM
    put16(22, static_cast<uint16_t>(h.channels));
    put32(24, static_cast<uint32_t>(h.sample_rate));
    put32(28, byte_rate);
    put16(32, block_align);
    put16(34, 8u);           // bits per sample
    put(36, "data");
    put32(40, static_cast<uint32_t>(h.pcm_size));
    std::memcpy(p + 44, raw_aud.data() + 14, h.pcm_size);
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