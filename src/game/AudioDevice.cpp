// AudioDevice.cpp -- AUD 解析 + WAV 合成 + PlaySound
#include "game/AudioDevice.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>

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

}  // namespace ra2