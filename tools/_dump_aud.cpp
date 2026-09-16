// _dump_aud.cpp -- 用现有 MixFileSystem 提取 AUD 文件分析头
#include <cstdio>
#include <vector>
#include <string>

#include "io/FileSystem.h"
#include "io/MixCrypto.h"

using namespace ra2;

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("usage: _dump_aud.exe <mix_path> <aud_name>\n");
        return 1;
    }
    MixFileSystem fs;
    if (!fs.Mount(argv[1])) {
        printf("[x] failed to mount %s\n", argv[1]);
        return 1;
    }
    std::vector<uint8_t> d = fs.Read_Deep(argv[2]);
    if (d.empty()) {
        printf("[x] %s not found\n", argv[2]);
        return 1;
    }
    printf("size=%zu\n", d.size());
    if (d.size() >= 14) {
        // Westwood AUD header (14 bytes):
        // 0: u8  always 0x00
        // 1: u16 data size (LE, raw PCM bytes after this)
        // 3: u16 sample rate (LE, Hz)
        // 5: u8  compression (0=raw, 1=Westwood ADPCM)
        // 6: u8  channels - 1 (0=mono, 1=stereo)
        // 7: u16 reserved
        // 9+: audio data
        uint8_t magic = d[0];
        uint16_t data_size = d[1] | (d[2] << 8);
        uint16_t sample_rate = d[3] | (d[4] << 8);
        uint8_t comp = d[5];
        uint8_t ch = d[6] + 1;
        printf("AUD header: magic=0x%02X data_size=%u rate=%u comp=%u channels=%u\n",
            magic, data_size, sample_rate, comp, ch);
        printf("first 32 bytes: ");
        for (int i = 0; i < 32 && i < (int)d.size(); i++) {
            printf("%02X ", d[i]);
        }
        printf("\n");
    }
    return 0;
}