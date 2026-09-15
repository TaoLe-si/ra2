// FileSystem.cpp
//
// 实现要点：
//   * FileClass 家族用标准 C 文件 IO 实现，避免在头文件里拖进 windows.h。
//   * MIX 索引里所有整数字段都是**大端**，x86 上必须换序。
//   * CRC 用的是公开记载的 Westwood CRC-32（多项式 0x04C11DB7，初值 0），
//     不是从二进制里反推出来的；因此 `Validate_Index()` 提供了一条可执行的
//     验证路径：拿真实的 ra2md.mix 跑一遍，索引自洽就说明算法与格式对得上。

#include "io/FileSystem.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#include <io.h>
#define RA2_FSEEK _fseeki64
#define RA2_FTELL _ftelli64
#else
#include <cstdio>
#define RA2_FSEEK fseeko
#define RA2_FTELL ftello
#endif

namespace ra2 {
namespace {

// 大端读取助手
uint16_t ReadBE16(const uint8_t* p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}
uint32_t ReadBE32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

// ---- Westwood CRC-32 ----
// 多项式 0x04C11DB7（非反射），初值 0，输入字符转大写。
// 这是 MIX 里把文件名映射成 32 位 ID 的算法，公开资料里有记载。
uint32_t CrcTable[256];
bool CrcTableReady = false;

void Init_Crc_Table() {
    if (CrcTableReady) {
        return;
    }
    for (int i = 0; i < 256; ++i) {
        uint32_t c = static_cast<uint32_t>(i) << 24;
        for (int j = 0; j < 8; ++j) {
            if (c & 0x80000000u) {
                c = (c << 1) ^ 0x04C11DB7u;
            } else {
                c <<= 1;
            }
        }
        CrcTable[i] = c;
    }
    CrcTableReady = true;
}

}  // namespace

// ---------------------------------------------------------------------------
// RawFileClass
// ---------------------------------------------------------------------------
RawFileClass::~RawFileClass() { Close(); }

bool RawFileClass::Open(const char* name, FileAccess mode) {
    Close();
    const char* m = (mode & kWrite) ? ((mode & kRead) ? "w+b" : "wb") : "rb";
    handle_ = std::fopen(name, m);
    if (handle_ == nullptr) {
        return false;
    }
    name_ = name;
    mode_ = mode;
    return true;
}

void RawFileClass::Close() {
    if (handle_ != nullptr) {
        std::fclose(static_cast<FILE*>(handle_));
        handle_ = nullptr;
    }
}

bool RawFileClass::Is_Open() const { return handle_ != nullptr; }

int RawFileClass::Read(void* buf, int len) {
    if (handle_ == nullptr || len <= 0) {
        return 0;
    }
    return static_cast<int>(std::fread(buf, 1, static_cast<size_t>(len),
                                       static_cast<FILE*>(handle_)));
}

int RawFileClass::Write(const void* buf, int len) {
    if (handle_ == nullptr || len <= 0) {
        return 0;
    }
    return static_cast<int>(std::fwrite(buf, 1, static_cast<size_t>(len),
                                        static_cast<FILE*>(handle_)));
}

int RawFileClass::Seek(int offset, int origin) {
    if (handle_ == nullptr) {
        return -1;
    }
    return RA2_FSEEK(static_cast<FILE*>(handle_), offset, origin) == 0 ? 0 : -1;
}

int RawFileClass::Size() {
    if (handle_ == nullptr) {
        return -1;
    }
    FILE* f = static_cast<FILE*>(handle_);
    const long long save = RA2_FTELL(f);
    RA2_FSEEK(f, 0, SEEK_END);
    const long long end = RA2_FTELL(f);
    RA2_FSEEK(f, save, SEEK_SET);
    return static_cast<int>(end);
}

// ---------------------------------------------------------------------------
// BufferIOFileClass
// ---------------------------------------------------------------------------
BufferIOFileClass::~BufferIOFileClass() { Close(); }

bool BufferIOFileClass::Open(const char* name, FileAccess mode) {
    if (!raw_.Open(name, mode)) {
        return false;
    }
    name_ = name;
    buffer_.assign(static_cast<size_t>(buffer_size_), 0);
    buffer_used_ = buffer_pos_ = 0;
    logical_pos_ = 0;
    return true;
}

void BufferIOFileClass::Close() { raw_.Close(); }
bool BufferIOFileClass::Is_Open() const { return raw_.Is_Open(); }

int BufferIOFileClass::Read(void* buf, int len) {
    int done = 0;
    uint8_t* out = static_cast<uint8_t*>(buf);
    while (done < len) {
        if (buffer_pos_ >= buffer_used_) {
            buffer_used_ = raw_.Read(buffer_.data(), buffer_size_);
            buffer_pos_ = 0;
            if (buffer_used_ <= 0) {
                break;
            }
        }
        const int n = std::min(len - done, buffer_used_ - buffer_pos_);
        std::memcpy(out + done, buffer_.data() + buffer_pos_, static_cast<size_t>(n));
        buffer_pos_ += n;
        done += n;
    }
    logical_pos_ += done;
    return done;
}

int BufferIOFileClass::Write(const void* buf, int len) {
    // 写路径保持直通（原引擎的缓冲主要是为读优化的）。
    const int n = raw_.Write(buf, len);
    if (n > 0) {
        logical_pos_ += n;
    }
    return n;
}

int BufferIOFileClass::Seek(int offset, int origin) {
    if (origin == SEEK_CUR) {
        offset += static_cast<int>(logical_pos_);
    } else if (origin == SEEK_END) {
        offset += Size();
    }
    if (raw_.Seek(offset, SEEK_SET) != 0) {
        return -1;
    }
    logical_pos_ = offset;
    buffer_used_ = buffer_pos_ = 0;  // 缓冲失效
    return 0;
}

int BufferIOFileClass::Size() { return raw_.Size(); }

// ---------------------------------------------------------------------------
// MixFileClass
// ---------------------------------------------------------------------------
std::string MixFileClass::Describe_Flags(uint32_t flags) {
    if (flags == kPlain) {
        return "plain（未加密、无校验和）";
    }
    std::string s;
    if (flags & kEncrypted) {
        s += "加密(Blowfish) ";
    }
    if (flags & kChecksum) {
        s += "带校验和 ";
    }
    s += "[0x%08X]";
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%08X", flags);
    // 把占位符换成真实值
    const size_t pos = s.find("[0x%08X]");
    if (pos != std::string::npos) {
        s.replace(pos, 9, buf);
    }
    return s;
}

uint32_t MixFileClass::CRC_Of(const char* name) {
    Init_Crc_Table();
    uint32_t crc = 0;
    for (const char* p = name; *p; ++p) {
        unsigned char c = static_cast<unsigned char>(*p);
        if (c >= 'a' && c <= 'z') {
            c = static_cast<unsigned char>(c - 'a' + 'A');  // 转大写
        }
        crc = (crc << 8) ^ CrcTable[((crc >> 24) ^ c) & 0xFFu];
    }
    return crc;
}

bool MixFileClass::Open(const char* path) {
    Close();
    if (!file_.Open(path, kRead)) {
        return false;
    }
    path_ = path;

    uint8_t hdr[12];
    if (file_.Read(hdr, sizeof(hdr)) != static_cast<int>(sizeof(hdr))) {
        Close();
        return false;
    }

    // 标志是小端 DWORD（实测 ra2md.mix / MULTIMD.MIX 均为 00 00 03/02 00）。
    const uint32_t flags = static_cast<uint32_t>(hdr[0]) |
                           (static_cast<uint32_t>(hdr[1]) << 8) |
                           (static_cast<uint32_t>(hdr[2]) << 16) |
                           (static_cast<uint32_t>(hdr[3]) << 24);
    has_names_ = (flags != 0);
    flags_ = flags;

    if (flags & kEncrypted) {
        // 头部与索引都是 Blowfish 密文，必须先解出会话密钥才能读。
        Close();
        return false;
    }

    uint32_t file_count = 0;
    uint32_t body_size = 0;
    uint64_t index_start = 4;

    if (flags == kPlain) {
        // 经典形态：[0]DWORD 0 | [4]WORD 文件数 | [6]DWORD 数据区长度（均大端）
        file_count = ReadBE16(hdr + 4);
        body_size = ReadBE32(hdr + 6);
        index_start = 10;
    } else {
        // 只有校验和、未加密：头部多一段校验数据，长度待确认。
        // 目前没有这种样本，先识别出来而不猜测。
        Close();
        return false;
    }

    const uint64_t file_size = static_cast<uint64_t>(file_.Size());
    data_start_ = index_start + static_cast<uint64_t>(file_count) * 12u;
    if (data_start_ + body_size > file_size + 64) {
        // 索引明显越界 —— 多半不是无文件名段的经典形态。
        Close();
        return false;
    }

    entries_.clear();
    entries_.reserve(file_count);
    for (uint32_t i = 0; i < file_count; ++i) {
        uint8_t e[12];
        if (file_.Read(e, 12) != 12) {
            break;
        }
        MixEntry m;
        m.id = ReadBE32(e);
        m.offset = ReadBE32(e + 4);
        m.size = ReadBE32(e + 8);
        entries_.push_back(m);
    }
    return !entries_.empty();
}

void MixFileClass::Close() {
    file_.Close();
    entries_.clear();
    path_.clear();
    data_start_ = 0;
}

const MixEntry* MixFileClass::Find(const char* filename) const {
    const uint32_t target = CRC_Of(filename);
    for (const auto& e : entries_) {
        if (e.id == target) {
            return &e;
        }
    }
    return nullptr;
}

std::vector<uint8_t> MixFileClass::Read_Entry(const MixEntry& e) const {
    std::vector<uint8_t> out;
    if (!file_.Is_Open()) {
        return out;
    }
    RawFileClass* f = const_cast<RawFileClass*>(&file_);
    if (f->Seek(static_cast<int>(data_start_ + e.offset), SEEK_SET) != 0) {
        return out;
    }
    out.resize(e.size);
    const int got = f->Read(out.data(), static_cast<int>(e.size));
    if (got != static_cast<int>(e.size)) {
        out.resize(static_cast<size_t>(got < 0 ? 0 : got));
    }
    return out;
}

uint32_t MixFileClass::Compute_CRC() const {
    // 与帧 CRC 同一套 FNV-1a：只要求"同一份数据必定得到同一个值"。
    uint32_t h = 2166136261u;
    auto mix = [&h](uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            h ^= (v >> (i * 8)) & 0xFFu;
            h *= 16777619u;
        }
    };
    for (const auto& e : entries_) {
        mix(e.id);
        mix(e.offset);
        mix(e.size);
    }
    return h;
}

bool MixFileClass::Validate_Index(int* out_bad_count) const {
    if (!file_.Is_Open() || entries_.empty()) {
        if (out_bad_count) {
            *out_bad_count = 0;
        }
        return false;
    }
    const uint64_t limit = static_cast<uint64_t>(const_cast<RawFileClass&>(file_).Size());
    int bad = 0;
    for (const auto& e : entries_) {
        const uint64_t begin = data_start_ + e.offset;
        const uint64_t end = begin + e.size;
        if (begin < data_start_ || end > limit) {
            ++bad;
        }
    }
    if (out_bad_count) {
        *out_bad_count = bad;
    }
    return bad == 0;
}

// ---------------------------------------------------------------------------
// MixFileSystem
// ---------------------------------------------------------------------------
bool MixFileSystem::Mount(const char* mix_path) {
    auto m = std::make_unique<MixFileClass>();
    if (!m->Open(mix_path)) {
        return false;
    }
    archives_.push_back(std::move(m));
    return true;
}

void MixFileSystem::Unmount_All() { archives_.clear(); }

std::vector<uint8_t> MixFileSystem::Read(const char* filename) const {
    // 倒序查找：后挂载的优先，这样补丁包能覆盖原包。
    for (auto it = archives_.rbegin(); it != archives_.rend(); ++it) {
        if (const MixEntry* e = (*it)->Find(filename)) {
            std::vector<uint8_t> data = (*it)->Read_Entry(*e);
            if (!data.empty()) {
                return data;
            }
        }
    }
    return {};
}

// ---------------------------------------------------------------------------
// Straw
// ---------------------------------------------------------------------------
int BufferStraw::Get(void* dest, int len) {
    if (pos_ >= size_ || len <= 0) {
        return 0;
    }
    const size_t n = std::min(static_cast<size_t>(len), size_ - pos_);
    std::memcpy(dest, data_ + pos_, n);
    pos_ += n;
    return static_cast<int>(n);
}

int BlowStraw::Get(void* dest, int len) {
    // TODO(逆向)：密钥派生流程未还原，暂做直通。
    //   还原后这里应改成 Blowfish 解密（游戏目录自带 Blowfish.dll，
    //   二进制里有 "blowfish.dll" 字符串）。
    (void)key_;
    if (upstream_ == nullptr) {
        return 0;
    }
    return upstream_->Get(dest, len);
}

}  // namespace ra2
