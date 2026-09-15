// FileSystem.h -- 虚拟文件系统（FileClass 家族 + MIX 打包）
//
// RTTI 确认存在的类（db/classes.md）：
//   基类      FileClass
//   实现      RawFileClass、CCFileClass（压缩）、CDFileClass（光盘）、
//             RAMFileClass（内存）、BufferIOFileClass（缓冲 IO）
//   打包      MixFileClass，容器 VectorClass<MixFileClass*> /
//             DynamicVectorClass<MixFileClass*>
//   流式变换  Pipe / Straw 及其实现：
//             BlowPipe / BlowStraw（Blowfish 加解密）
//             LCWPipe / LCWStraw、LZOPipe / LZOStraw、PKPipe / PKStraw、
//             SHAPipe（校验）、Base64Pipe / Base64Straw、BufferPipe / BufferStraw、
//             FilePipe / FileStraw、CacheStraw、RandomStraw、Straw
//
// 二进制证据：
//   "AUDIO.MIX" / "AUDIOMD.MIX"  -> 0x00406B10
//   "MOVIES%02d.MIX" / "MOVMD%02d.MIX" -> 0x00530460
//   RA2MD 目录下确实有 ra2md.mix / expandmd01.mix / maps02.mix 等
//   导入表里有 blowfish.dll（Blowfish.dll 也在游戏目录里）
//
// 设计：Pipe/Straw 是 Westwood 经典的"流式过滤器"——
//   Straw 是拉（pull）模型，Pipe 是推（push）模型，两者都是装饰器，
//   可以串成 文件 -> 解密 -> 解压 -> 业务 的管道。
//   这个结构天然可以并行：多个 MIX 条目的解包互不依赖。

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ra2 {

/// 打开方式位。
enum FileAccess : uint32_t {
    kRead = 0x01,
    kWrite = 0x02,
    kReadWrite = kRead | kWrite,
};

/// 文件抽象基类。RTTI 确认：FileClass。
class FileClass {
public:
    virtual ~FileClass() = default;

    virtual bool Open(const char* name, FileAccess mode) = 0;
    virtual void Close() = 0;
    virtual bool Is_Open() const = 0;

    virtual int Read(void* buf, int len) = 0;
    virtual int Write(const void* buf, int len) = 0;
    virtual int Seek(int offset, int origin) = 0;   ///< origin: SEEK_SET/SEEK_CUR/SEEK_END
    virtual int Size() = 0;

    const std::string& Name() const noexcept { return name_; }

protected:
    std::string name_;
    FileAccess mode_ = kRead;
};

/// 直接读写磁盘文件。RTTI 确认：RawFileClass。
class RawFileClass : public FileClass {
public:
    ~RawFileClass() override;

    bool Open(const char* name, FileAccess mode) override;
    void Close() override;
    bool Is_Open() const override;
    int Read(void* buf, int len) override;
    int Write(const void* buf, int len) override;
    int Seek(int offset, int origin) override;
    int Size() override;

private:
    void* handle_ = nullptr;  // HANDLE，避免把 windows.h 拖进头文件
};

/// 带缓冲的文件 IO。RTTI 确认：BufferIOFileClass。
class BufferIOFileClass : public FileClass {
public:
    explicit BufferIOFileClass(int buffer_size = 65536) : buffer_size_(buffer_size) {}
    ~BufferIOFileClass() override;

    bool Open(const char* name, FileAccess mode) override;
    void Close() override;
    bool Is_Open() const override;
    int Read(void* buf, int len) override;
    int Write(const void* buf, int len) override;
    int Seek(int offset, int origin) override;
    int Size() override;

private:
    RawFileClass raw_;
    std::vector<uint8_t> buffer_;
    int buffer_size_;
    int buffer_used_ = 0;
    int buffer_pos_ = 0;
    int64_t logical_pos_ = 0;
};

// ---------------------------------------------------------------------------
// MIX 打包
// ---------------------------------------------------------------------------
// MIX 是 Westwood 的归档格式。结构（无文件名段的最简形态）：
//
// 实测（2026-09-15，tools/mixdump.py）只有两种形态，全**小端**：
//
// (A) 加密 MIX —— flags 带 0x00020000（ra2.mix / ra2md.mix / expandmd01.mix 等）
//     [0x00] DWORD flags（明文）
//     [0x04] 80 字节 RSA 加密的密钥源
//     [0x54] Blowfish 加密的索引：u16 文件数 + u32 数据区长度 + 文件数×12 条目
//           索引尾部补零到 8 字节对齐
//     数据区紧跟其后；若带 kChecksum，文件末尾另有 20 字节 SHA1
//     解密流程见 src/io/MixCrypto.cpp（Blowfish + 320 位 RSA + Westwood CRC）。
//
// (B) 明文 MIX —— flags 不带 0x00020000（**地形归档全是这一种**）
//     [0x00] DWORD flags  （可能是 0，也可能是 0x00010000 带 SHA1）
//     [0x04] WORD  文件数
//     [0x06] DWORD 数据区长度
//     [0x0A] 文件数 × 12 字节索引 { id, offset, size }
//     数据区偏移 = 10 + 文件数*12
//     验算：ISOGEN.MIX 10 + 24*12 + 11991728 + 20 = 11992046 ✓
//     早期只实现了 (A)，于是 ra2.mix 里 13 个 10~35MB 的地形包全被误判成 SHP。
//
// 注意：旧注释写的是"全大端"，那是照抄资料抄错的，实测已推翻 ——
// 两种形态的头部与索引字段一律小端。

struct MixEntry {
    uint32_t id = 0;    ///< 文件名 CRC（Westwood_CRC）
    uint32_t offset = 0;
    uint32_t size = 0;
};

/// MIX 头部标志（小端 DWORD）。
///
/// 实测（D:\westwood\RA2YR 下真实文件的前 4 字节）：
///   ra2md.mix      0x00030000  -> kChecksum | kEncrypted
///   ra2.mix        0x00030000  -> kChecksum | kEncrypted
///   expandmd01.mix 0x00030000  -> kChecksum | kEncrypted
///   MULTIMD.MIX    0x00020000  -> kEncrypted
/// 由此确定位含义：0x00010000 = 带校验和，0x00020000 = 加密。
///
/// 带 kEncrypted 的 MIX，其头部（文件数、数据区长度）与整个索引都是
/// Blowfish 密文，会话密钥要先用 RSA 从头部 80 字节里解出来 ——
/// 这一步已经还原完成（src/io/MixCrypto.cpp）。
enum MixFlags : uint32_t {
    kPlain = 0x00000000,
    kChecksum = 0x00010000,
    kEncrypted = 0x00020000,
};

/// 一个 MIX 归档。RTTI 确认：MixFileClass。
///
/// 支持嵌套：ra2md.mix 里装的子归档（LOCALMD.MIX、CACHEMD.MIX …）本身
/// 也是加密 MIX。用 Open_Nested() 打开时只把索引读进内存，真正取数据时
/// 把偏移换算回父归档，避免为了一层索引就吞进几十 MB。
class MixFileClass {
public:
    /// 打开磁盘上的 MIX 并读入索引。
    bool Open(const char* path);

    /// 把已打开 MIX 里的某个条目当成嵌套 MIX 打开。不是 MIX 则返回 false。
    bool Open_Nested(const MixFileClass& parent, const MixEntry& e);

    /// 直接从内存块打开（一般用于测试）。
    bool Open_Memory(const uint8_t* data, size_t size);

    void Close();

    const std::string& Path() const noexcept { return path_; }
    int Count() const noexcept { return static_cast<int>(entries_.size()); }
    bool Is_Nested() const noexcept { return parent_ != nullptr; }
    uint64_t Data_Size() const noexcept { return data_size_; }
    uint64_t Data_Start() const noexcept { return data_start_; }
    const std::vector<MixEntry>& Entries() const noexcept { return entries_; }

    /// 按文件名查找（内部转成 CRC 比较）。
    const MixEntry* Find(const char* filename) const;

    /// 按 CRC 直接找 —— 名字还没反查出来时也能拿内容。
    const MixEntry* Find_By_ID(uint32_t id) const;

    /// 把某个条目读进内存。失败返回空。
    std::vector<uint8_t> Read_Entry(const MixEntry& e) const;

    /// 把条目当嵌套 MIX 打开；不是就返回 nullptr。
    std::unique_ptr<MixFileClass> Open_Sub(const MixEntry& e) const;

    /// 递归进子 MIX 取文件内容（子归档是临时对象，所以直接回数据而不是指针）。
    /// 找不到返回空。max_depth 限制递归层数。
    std::vector<uint8_t> Read_Deep(const char* filename, int max_depth = 4) const;
    std::vector<uint8_t> Read_Deep_By_ID(uint32_t id, int max_depth = 4) const;

    /// 校验：把所有条目 ID 累积成一个 CRC，用于联机一致性检查。
    /// 原引擎有大量 "*** CRCs" 日志，这里沿用同样的思路。
    uint32_t Compute_CRC() const;

    /// 索引自洽性检查：是否每个条目都落在 [data_start, 文件大小] 之内。
    /// 这是判断"大端解析 / CRC / 头部长度"三处理解是否正确的可执行判据。
    bool Validate_Index(int* out_bad_count = nullptr) const;

    /// 文件名 -> 32 位 ID（与 Westwood 的 CRC 算法一致）。
    static uint32_t CRC_Of(const char* name);

    uint32_t Flags() const noexcept { return flags_; }

    /// 头部标志的人类可读说明，用于诊断"为什么这个 MIX 读不了"。
    static std::string Describe_Flags(uint32_t flags);

    /// 从本归档的坐标空间读（0 = 本 MIX 开头）。嵌套时换算回父归档。
    bool Read_At(uint64_t offset, void* dst, size_t n) const;

private:
    /// 打开的核心：给定一段"至少含头部"的字节，解出索引。
    /// 按 flags 是否带 kEncrypted 分派到明文/加密两条路径。
    bool Parse_Header();

    /// 明文 MIX 索引解析（head 至少 10 字节）。见 FileSystem.cpp 里的布局说明。
    bool Parse_Plain(const uint8_t* head);

    std::string path_;
    std::vector<MixEntry> entries_;
    uint64_t data_start_ = 0;
    uint64_t file_size_ = 0;
    uint64_t data_size_ = 0;
    uint32_t flags_ = 0;

    const MixFileClass* parent_ = nullptr;  ///< 嵌套时的父归档
    uint64_t parent_off_ = 0;               ///< 本归档在父归档数据区里的偏移

    std::vector<uint8_t> mem_;              ///< Open_Memory() 用的内存镜像
    mutable RawFileClass file_;
};

/// 把若干 MIX 挂成一个虚拟文件系统。
/// 原引擎用 VectorClass<MixFileClass*> 管理，查找时按挂载顺序倒序命中
/// （后挂载的优先，这样补丁包能覆盖原包）。
class MixFileSystem {
public:
    bool Mount(const char* mix_path);
    void Unmount_All();

    /// 按名字取文件内容，找不到返回空。只在已挂载的顶层 MIX 里找。
    std::vector<uint8_t> Read(const char* filename) const;

    /// 递归进子 MIX 找。
    std::vector<uint8_t> Read_Deep(const char* filename) const;

    int Archive_Count() const noexcept { return static_cast<int>(archives_.size()); }

private:
    std::vector<std::unique_ptr<MixFileClass>> archives_;
};

// ---------------------------------------------------------------------------
// Pipe / Straw：流式过滤器
// ---------------------------------------------------------------------------
// Straw 是"拉"模型：Get(buf, len) 从上游拉取并变换。
// 原引擎用它做 文件 -> 解压 -> 解密 -> 消费 的串联。
class Straw {
public:
    virtual ~Straw() = default;
    /// 返回实际读到的字节数，0 表示流结束。
    virtual int Get(void* dest, int len) = 0;
    /// 把本 Straw 接到上游。
    void Attach(std::shared_ptr<Straw> upstream) { upstream_ = std::move(upstream); }

protected:
    std::shared_ptr<Straw> upstream_;
};

/// 从内存块拉数据。用于测试与解包后的回吐。
class BufferStraw : public Straw {
public:
    BufferStraw(const uint8_t* data, size_t size) : data_(data), size_(size) {}
    int Get(void* dest, int len) override;

private:
    const uint8_t* data_ = nullptr;
    size_t size_ = 0;
    size_t pos_ = 0;
};

/// RTTI 确认：BlowStraw / BlowPipe —— Blowfish 解密。
/// 游戏目录自带 Blowfish.dll，二进制里也有 "blowfish.dll" 字符串。
/// TODO(逆向)：真正的密钥来自 ra2md.mix 头部与可执行文件本身的校验和，
///   需要还原密钥派生流程后才能解真正的加密包；
///   这里先把接口与串联结构搭好。
class BlowStraw : public Straw {
public:
    explicit BlowStraw(std::vector<uint8_t> key) : key_(std::move(key)) {}
    int Get(void* dest, int len) override;

private:
    std::vector<uint8_t> key_;
};

}  // namespace ra2
