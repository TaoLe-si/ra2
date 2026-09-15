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

static inline uint16_t RdU16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}
static inline uint32_t RdU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

/// 明文 MIX（flags 不带 kEncrypted）。
///
/// 布局（三个样本严格验算，不是猜的）：
///     +0  u32 flags        （0x00010000 = 尾部有 20 字节 SHA1）
///     +4  u16 count
///     +6  u32 body_size
///     +10 count × 12 字节索引 { id, offset, size }   —— 全小端
/// 验算（data_start + body_size 应等于文件大小）：
///     ISOGEN.MIX   10 + 24*12 + 11991728 + 20 = 11992046 ✓
///     ru2 大包     10 + 1257*12 + 31796288 + 20 = 31811402 ✓
///     0x92144015   10 + 3*12 + 10784 + 20 = 10850 ✓
///
/// **地形归档（ISOGEN.MIX / GENERIC.MIX / TEMPERAT 等）全是这一种。**
/// 之前只实现了加密路径，导致 ra2.mix 里 13 个 10~35MB 的条目被误判成 SHP，
/// 整个地形素材不可见。
bool MixFileClass::Parse_Plain(const uint8_t* head) {
    const uint32_t count = RdU16(head + 4);
    data_size_ = RdU32(head + 6);
    if (count == 0 || count > 20000u) {
        return false;
    }
    const uint64_t idx_len = static_cast<uint64_t>(count) * 12;
    data_start_ = 10 + idx_len;
    // 自洽检查：数据区必须装得下。少了这一条，普通文件（尤其大 SHP）会被当成
    // 明文 MIX，Read_Deep 里层层递归直接指数爆炸。允许 64 字节对齐/校验和冗余。
    if (file_size_ > 0 && data_start_ + data_size_ > file_size_ + 64) {
        return false;
    }
    std::vector<uint8_t> raw(static_cast<size_t>(idx_len));
    if (!Read_At(10, raw.data(), static_cast<size_t>(idx_len))) {
        return false;
    }
    entries_.clear();
    entries_.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* e = raw.data() + i * 12;
        MixEntry m;
        m.id = RdU32(e);
        m.offset = RdU32(e + 4);
        m.size = RdU32(e + 8);
        entries_.push_back(m);
    }
    return !entries_.empty();
}

bool MixFileClass::Parse_Header() {
    // 先读 flags + 8 字节密文索引头（够解出文件数）。
    uint8_t head[kMixBody + 8];
    if (!Read_At(0, head, sizeof(head))) {
        return false;
    }
    flags_ = static_cast<uint32_t>(head[0]) | (static_cast<uint32_t>(head[1]) << 8) |
             (static_cast<uint32_t>(head[2]) << 16) | (static_cast<uint32_t>(head[3]) << 24);

    if (!(flags_ & kEncrypted)) {
        // 明文 MIX：头部只有 10 字节，索引是小端明文。
        // 注意 flags 的高位也可能是 0x00010000（带 SHA1），仍属明文路径。
        return Parse_Plain(head);
    }

    std::vector<uint8_t> key = Derive_Mix_Key(head);
    if (key.empty()) {
        return false;   // 密钥源解不开：不是真的加密 MIX
    }

    Blowfish bf(key.data(), key.size());
    uint8_t plain[8];
    bf.Decrypt(head + kMixBody, plain, 8);
    const uint32_t count = RdU16(plain);
    data_size_ = RdU32(plain + 2);
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

    // 关键的自洽检查：数据区必须装得下。少了这一条，普通文件也会被误判成
    // 嵌套 MIX（随便 92 字节都能解出个 count），于是 Read_Deep 会往假归档里
    // 一层层递归 —— 25 × 187 × ... 指数爆炸，实测直接把程序挂死。
    data_start_ = kMixBody + enc_len;
    if (file_size_ > 0 && data_start_ + data_size_ > file_size_ + 64) {
        return false;
    }
    entries_.clear();
    entries_.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* e = raw.data() + 6 + i * 12;
        MixEntry m;
        m.id = RdU32(e);
        m.offset = RdU32(e + 4);
        m.size = RdU32(e + 8);
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
    // 再过一道：条目必须全部落在数据区内。真归档一定满足，噪声几乎不可能。
    int bad = 0;
    if (!Validate_Index(&bad)) {
        Close();      // 会顺带清掉 parent_ / parent_off_
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
    // 子归档里存着指向本对象的 parent_ 指针，本对象一变它们就全失效了，
    // 所以缓存必须跟着清 —— 不清的话下次 Find_Deep 会顺着野指针读。
    sub_cache_.clear();
    deep_index_.clear();
    deep_index_built_ = false;
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

const MixFileClass* MixFileClass::Sub_At(const MixEntry& e) const {
    // 键用"条目在本归档里的下标"，不用 (offset,size)：下标一定唯一，
    // 而且不会因为两条目 offset 巧合相同而互相顶掉。
    const size_t key = static_cast<size_t>(&e - entries_.data());
    const auto it = sub_cache_.find(key);
    if (it != sub_cache_.end()) {
        return it->second.get();   // 可能是 nullptr = "已试过，不是 MIX"
    }
    std::unique_ptr<MixFileClass> sub;
    {
        auto candidate = std::make_unique<MixFileClass>();
        if (candidate->Open_Nested(*this, e)) {
            sub = std::move(candidate);
        }
        // 失败就留空 unique_ptr：记住"这条不是 MIX"，下次不再解密一遍
    }
    const MixFileClass* raw = sub.get();
    sub_cache_[key] = std::move(sub);
    return raw;
}

void MixFileClass::Build_Deep_Index() const {
    if (deep_index_built_) {
        return;
    }
    deep_index_built_ = true;
    // 先收本层的：命中优先级与旧的 Read_Deep_By_ID 一致（浅层优先）
    for (const auto& e : entries_) {
        deep_index_.emplace(e.id, Deep_Entry{this, &e});
    }
    // 再按条目顺序深度优先往子归档里收
    for (const auto& e : entries_) {
        const MixFileClass* sub = Sub_At(e);
        if (sub != nullptr) {
            sub->Build_Deep_Index();
            for (const auto& kv : sub->deep_index_) {
                deep_index_.emplace(kv.first, kv.second);
            }
        }
    }
}

const MixFileClass::Deep_Entry* MixFileClass::Find_Deep(uint32_t id) const {
    Build_Deep_Index();
    const auto it = deep_index_.find(id);
    return it == deep_index_.end() ? nullptr : &it->second;
}

std::vector<uint8_t> MixFileClass::Read_Deep_By_ID(uint32_t id) const {
    const Deep_Entry* de = Find_Deep(id);
    if (de == nullptr || de->owner == nullptr || de->entry == nullptr) {
        return {};
    }
    return de->owner->Read_Entry(*de->entry);
}

int MixFileClass::Collect_Leaf_IDs(std::vector<uint32_t>* out, int max_depth) const {
    if (!out) {
        return 0;
    }
    const int before = static_cast<int>(out->size());
    for (const auto& e : entries_) {
        out->push_back(e.id);
    }
    if (max_depth <= 0) {
        return static_cast<int>(out->size()) - before;
    }
    for (const auto& e : entries_) {
        // 用缓存版 Sub_At：挪动过一次之后就不再解密索引了
        if (const MixFileClass* sub = Sub_At(e)) {
            sub->Collect_Leaf_IDs(out, max_depth - 1);
        }
    }
    return static_cast<int>(out->size()) - before;
}

std::vector<uint8_t> MixFileClass::Read_Deep(const char* filename) const {
    return Read_Deep_By_ID(CRC_Of(filename));
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
    // 注意不能拿 file_.Is_Open() 当前置条件：嵌套归档没有自己的文件句柄，
    // 数据是从父归档读的。曾经这里写成 `!file_.Is_Open() || entries_.empty()`，
    // 结果所有子 MIX 都被判成无效，Read_Deep 一层都下不去。
    if (entries_.empty()) {
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
