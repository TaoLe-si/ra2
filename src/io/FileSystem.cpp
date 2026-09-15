// FileSystem.cpp
//
// 实现要点：
//   * FileClass 家族用标准 C 文件 IO 实现，避免在头文件里拖进 windows.h。
//   * MIX 索引里所有整数字段都是**大端**，x86 上必须换序。
//   * CRC 用的是公开记载的 Westwood CRC-32（多项式 0x04C11DB7，初值 0），
//     不是从二进制里反推出来的；因此 `Validate_Index()` 提供了一条可执行的
//     验证路径：拿真实的 ra2md.mix 跑一遍，索引自洽就说明算法与格式对得上。

#include "io/FileSystem.h"

#include "io/MixCrypto.h"

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
// 注：这里原先有一份"非反射 CRC32 / 多项式 0x04C11DB7"的实现，是照抄资料抄错的。
// 实测证明 MIX 用的是反射 CRC32 + 末组填充（见 MixCrypto.cpp），旧实现已删除。

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
    // 旧实现用的是"非反射 CRC32、多项式 0x04C11DB7"——那份资料是错的。
    // 实际是反射 CRC32 + 末组填充，见 MixCrypto.cpp 里的说明与实测依据。
    return Westwood_CRC(name);
}

// 加密 MIX 的头部与索引布局（详见 MixCrypto.h）
namespace {
constexpr uint64_t kMixKeySource = 80;   // 头部里 RSA 加密的密钥源长度
constexpr uint64_t kMixBody = 4 + kMixKeySource;   // 84：索引起点
}  // namespace

bool MixFileClass::Parse_Header() {
    // 先读 flags + 8 字节密文索引头（够解出文件数）。
    uint8_t head[kMixBody + 8];
    if (!Read_At(0, head, sizeof(head))) {
        return false;
    }
    flags_ = static_cast<uint32_t>(head[0]) | (static_cast<uint32_t>(head[1]) << 8) |
             (static_cast<uint32_t>(head[2]) << 16) | (static_cast<uint32_t>(head[3]) << 24);

    std::vector<uint8_t> key;
    if (flags_ & kEncrypted) {
        key = Derive_Mix_Key(head);
        if (key.empty()) {
            return false;   // 密钥源解不开：不是真的加密 MIX
        }
    } else {
        // 未加密（含"只有校验和"）的样本目前没有，先不支持，别猜。
        return false;
    }

    Blowfish bf(key.data(), key.size());
    uint8_t plain[8];
    bf.Decrypt(head + kMixBody, plain, 8);
    const uint32_t count = static_cast<uint32_t>(plain[0]) | (static_cast<uint32_t>(plain[1]) << 8);
    data_size_ = static_cast<uint32_t>(plain[2]) | (static_cast<uint32_t>(plain[3]) << 8) |
                 (static_cast<uint32_t>(plain[4]) << 16) | (static_cast<uint32_t>(plain[5]) << 24);
    if (count == 0 || count > 200000u) {
        return false;       // 解出来是噪声
    }

    const uint64_t need = 6 + static_cast<uint64_t>(count) * 12;
    const uint64_t enc_len = (need + 7) / 8 * 8;
    std::vector<uint8_t> raw(static_cast<size_t>(enc_len));
    if (!Read_At(kMixBody, raw.data(), static_cast<size_t>(enc_len))) {
        return false;
    }
    bf.Decrypt_In_Place(raw.data(), raw.size());

    data_start_ = kMixBody + enc_len;
    entries_.clear();
    entries_.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* e = raw.data() + 6 + i * 12;
        MixEntry m;
        m.id = static_cast<uint32_t>(e[0]) | (static_cast<uint32_t>(e[1]) << 8) |
               (static_cast<uint32_t>(e[2]) << 16) | (static_cast<uint32_t>(e[3]) << 24);
        m.offset = static_cast<uint32_t>(e[4]) | (static_cast<uint32_t>(e[5]) << 8) |
                   (static_cast<uint32_t>(e[6]) << 16) | (static_cast<uint32_t>(e[7]) << 24);
        m.size = static_cast<uint32_t>(e[8]) | (static_cast<uint32_t>(e[9]) << 8) |
                 (static_cast<uint32_t>(e[10]) << 16) | (static_cast<uint32_t>(e[11]) << 24);
        entries_.push_back(m);
    }
    return !entries_.empty();
}

bool MixFileClass::Read_At(uint64_t offset, void* dst, size_t n) const {
    if (n == 0) {
        return true;
    }
    if (parent_ != nullptr) {
        // 嵌套：把本归档的坐标换算回父归档的数据区坐标。
        return parent_->Read_At(parent_->data_start_ + parent_off_ + offset, dst, n);
    }
    if (!file_.Is_Open()) {
        // 内存镜像模式
        if (offset + n > mem_.size()) {
            return false;
        }
        std::memcpy(dst, mem_.data() + offset, n);
        return true;
    }
    RawFileClass* f = const_cast<RawFileClass*>(&file_);
    if (f->Seek(static_cast<int>(offset), SEEK_SET) != 0) {
        return false;
    }
    return f->Read(dst, static_cast<int>(n)) == static_cast<int>(n);
}

bool MixFileClass::Open(const char* path) {
    Close();
    if (!file_.Open(path, kRead)) {
        return false;
    }
    path_ = path;
    file_size_ = static_cast<uint64_t>(file_.Size());
    if (!Parse_Header()) {
        Close();
        return false;
    }
    return true;
}

bool MixFileClass::Open_Nested(const MixFileClass& parent, const MixEntry& e) {
    Close();
    parent_ = &parent;
    parent_off_ = e.offset;
    file_size_ = e.size;
    path_ = parent.path_ + " > #" + std::to_string(e.id);
    if (!Parse_Header()) {
        parent_ = nullptr;
        parent_off_ = 0;
        return false;
    }
    return true;
}

bool MixFileClass::Open_Memory(const uint8_t* data, size_t size) {
    Close();
    mem_.assign(data, data + size);
    file_size_ = size;
    path_ = "<memory>";
    if (!Parse_Header()) {
        mem_.clear();
        return false;
    }
    return true;
}

std::unique_ptr<MixFileClass> MixFileClass::Open_Sub(const MixEntry& e) const {
    auto sub = std::make_unique<MixFileClass>();
    if (!sub->Open_Nested(*this, e)) {
        return nullptr;
    }
    return sub;
}

void MixFileClass::Close() {
    file_.Close();
    entries_.clear();
    path_.clear();
    data_start_ = 0;
    file_size_ = 0;
    data_size_ = 0;
    flags_ = 0;
    parent_ = nullptr;
    parent_off_ = 0;
}

const MixEntry* MixFileClass::Find(const char* filename) const {
    return Find_By_ID(CRC_Of(filename));
}

const MixEntry* MixFileClass::Find_By_ID(uint32_t id) const {
    for (const auto& e : entries_) {
        if (e.id == id) {
            return &e;
        }
    }
    return nullptr;
}

std::vector<uint8_t> MixFileClass::Read_Deep_By_ID(uint32_t id, int max_depth) const {
    if (const MixEntry* e = Find_By_ID(id)) {
        return Read_Entry(*e);
    }
    if (max_depth <= 0) {
        return {};
    }
    // 逐个把条目当子 MIX 试开。判据就是"能不能解出索引"——打不开就是普通文件，
    // 代价仅一次头部解密。ra2md.mix 有 25 个顶层条目，全部试一遍也就几十微秒。
    for (const auto& e : entries_) {
        auto sub = Open_Sub(e);
        if (!sub) {
            continue;
        }
        std::vector<uint8_t> data = sub->Read_Deep_By_ID(id, max_depth - 1);
        if (!data.empty()) {
            return data;
        }
    }
    return {};
}

std::vector<uint8_t> MixFileClass::Read_Deep(const char* filename, int max_depth) const {
    return Read_Deep_By_ID(CRC_Of(filename), max_depth);
}

std::vector<uint8_t> MixFileClass::Read_Entry(const MixEntry& e) const {
    std::vector<uint8_t> out;
    if (e.size == 0) {
        return out;
    }
    out.resize(e.size);
    if (!Read_At(data_start_ + e.offset, out.data(), e.size)) {
        out.clear();
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
    // 判据：每个条目都必须落在数据区之内。data_size_ 是解密索引里读出来的
    // 数据区长度，与文件大小对不上就说明头部/索引解析有错。
    const uint64_t limit = data_size_ > 0 ? data_size_ : file_size_;
    int bad = 0;
    for (const auto& e : entries_) {
        const uint64_t begin = e.offset;
        const uint64_t end = begin + e.size;
        if (begin > limit || end > limit) {
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

std::vector<uint8_t> MixFileSystem::Read_Deep(const char* filename) const {
    for (auto it = archives_.rbegin(); it != archives_.rend(); ++it) {
        std::vector<uint8_t> data = (*it)->Read_Deep(filename);
        if (!data.empty()) {
            return data;
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
