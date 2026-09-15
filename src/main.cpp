// main.cpp -- 冒烟测试：验证"并行结果与串行逐条一致"
//
// 这个测试是整个多核改造的验收基准：
// 只要它挂了，就说明某个并行改动破坏了确定性，联机必然失步。
//
// 构建： cmake -B build && cmake --build build
// 运行： ./build/ra2core

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <random>
#include <string>
#include <vector>

#include "ai/PathFinder.h"
#include "core/VTableMap.h"
#include "data/Ini.h"
#include "re/ObjectSizes.h"
#include "engine/FrameQueue.h"
#include "gfx/Palette.h"
#include "gfx/PcxFile.h"
#include "gfx/TmpFile.h"
#include "io/FileSystem.h"
#include "io/MixCrypto.h"
#include "map/Map.h"
#include "threading/TaskSystem.h"

using namespace ra2;

namespace {

bool SameResult(const PathResult& a, const PathResult& b) {
    if (a.found != b.found || a.cost != b.cost || a.used_hierarchical != b.used_hierarchical) {
        return false;
    }
    if (a.waypoints.size() != b.waypoints.size()) {
        return false;
    }
    for (size_t i = 0; i < a.waypoints.size(); ++i) {
        if (a.waypoints[i] != b.waypoints[i]) {
            return false;
        }
    }
    return true;
}

}  // namespace

// 传入真实 MIX 文件路径，验证加密 MIX 的解密链路是否完好。
// 例：  ra2core.exe D:\westwood\RA2YR\ra2md.mix
//
// 这是整个工程最有价值的一条回归测试：它同时验证了
//   RSA 密钥派生、Blowfish 解密、索引布局、Westwood CRC、嵌套 MIX
// 五处实现。任何一处错了，(条目数, 数据区长度) 就对不上，或者 CRC 反查失败。
static int Verify_Mix(const char* path) {
    // CRC 算法的实测锚点：这两个名字的 CRC 是从真实 ra2md.mix 里撞出来的。
    struct Anchor { const char* name; uint32_t crc; };
    const Anchor anchors[] = {
        {"LOCALMD.MIX", 0xFBE0D09D},
        {"CACHEMD.MIX", 0x68EEC99A},
        {"AIMD.INI", 0x116F3F76},
        {"MNBTTN.SHP", 0x1BB65278},
    };
    for (const Anchor& a : anchors) {
        const uint32_t got = Westwood_CRC(a.name);
        if (got != a.crc) {
            std::printf("FAIL Westwood CRC：%s 算出 0x%08X，应为 0x%08X\n",
                        a.name, got, a.crc);
            return 1;
        }
    }
    std::printf("OK   Westwood CRC 通过 %d 个实测锚点\n",
                static_cast<int>(sizeof(anchors) / sizeof(anchors[0])));

    MixFileClass mix;
    if (!mix.Open(path)) {
        std::printf("FAIL MIX 打不开 %s（flags=%s）\n", path,
                    MixFileClass::Describe_Flags(mix.Flags()).c_str());
        return 1;
    }

    int bad = 0;
    const bool ok = mix.Validate_Index(&bad);
    std::printf("MIX  %s\n     flags=%s 条目=%d 数据区=%llu 起=%llu %s（越界 %d）\n",
                path, MixFileClass::Describe_Flags(mix.Flags()).c_str(),
                mix.Count(), (unsigned long long)mix.Data_Size(),
                (unsigned long long)mix.Data_Start(),
                ok ? "索引自洽" : "索引不自洽", bad);
    if (!ok) {
        return 1;
    }

    // 按名字取条目（CRC 反查 + 索引都对了才会命中）
    if (const MixEntry* e = mix.Find("LOCALMD.MIX")) {
        std::printf("OK   按名找到 LOCALMD.MIX：off=%u size=%u\n", e->offset, e->size);
        auto sub = mix.Open_Sub(*e);
        if (sub) {
            int sub_bad = 0;
            sub->Validate_Index(&sub_bad);
            std::printf("OK   嵌套 MIX 打开成功：条目=%d 数据区=%llu 越界=%d\n",
                        sub->Count(), (unsigned long long)sub->Data_Size(), sub_bad);
        } else {
            std::printf("FAIL LOCALMD.MIX 应当是个嵌套 MIX\n");
            return 1;
        }
    } else {
        // ra2.mix 里确实没有 LOCALMD.MIX（它在 ra2md.mix 里），所以这里不算失败。
        // 真正的通用锚点是下面的密钥文件 —— 每个 MIX 都带。
        std::printf("INFO 本归档没有 LOCALMD.MIX（ra2.mix 属于这种情况，正常）\n");
    }

    // 递归取出那个每个 MIX 都带的密钥文件，内容应当以 [PublicKey] 开头。
    // 这个文件本身就是算法正确性的铁证：里面的公钥必须与二进制里的一致。
    std::vector<uint8_t> keyfile = mix.Read_Deep_By_ID(0x763C81DD);
    if (keyfile.size() != 151) {
        std::printf("FAIL 密钥文件大小 %zu，应为 151\n", keyfile.size());
        return 1;
    }
    const std::string head(reinterpret_cast<const char*>(keyfile.data()), 11);
    if (head != "[PublicKey]") {
        std::printf("FAIL 密钥文件开头是 %s，应为 [PublicKey]\n", head.c_str());
        return 1;
    }
    std::printf("OK   递归取出密钥文件（151 字节），开头 = [PublicKey]\n");
    std::printf("     %.*s\n", 58, reinterpret_cast<const char*>(keyfile.data()) + 11);
    return 0;
}

// 写一张 24 位 BMP。测试代码里不想引 zlib，BMP 不压缩最好写，
// 而且 Windows 自带预览，足够和 Python 版 PNG 对照着肉眼验收。
// 注意：逐字节手工拼头，不用结构体 —— 结构体的对齐/打包语法各家编译器不一样。
static bool Write_BMP(const char* path, const uint32_t* rgba, int w, int h) {
    if (w <= 0 || h <= 0 || rgba == nullptr) {
        return false;
    }
    const int stride = (w * 3 + 3) & ~3;
    const uint32_t img = static_cast<uint32_t>(stride) * static_cast<uint32_t>(h);
    std::vector<uint8_t> buf(54 + img, 0);
    auto put16 = [&buf](size_t o, uint16_t v) {
        buf[o] = static_cast<uint8_t>(v);
        buf[o + 1] = static_cast<uint8_t>(v >> 8);
    };
    auto put32 = [&buf](size_t o, uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            buf[o + i] = static_cast<uint8_t>(v >> (i * 8));
        }
    };
    put16(0, 0x4D42);          // 'BM'
    put32(2, 54 + img);
    put32(10, 54);             // 像素数据起点
    put32(14, 40);             // BITMAPINFOHEADER
    put32(18, static_cast<uint32_t>(w));
    put32(22, static_cast<uint32_t>(h));
    put16(26, 1);              // planes
    put16(28, 24);             // bpp
    // BMP 是自下而上存行的。
    for (int y = 0; y < h; ++y) {
        const uint32_t* src = rgba + static_cast<size_t>(y) * w;
        uint8_t* dst = buf.data() + 54 + static_cast<size_t>(h - 1 - y) * stride;
        for (int x = 0; x < w; ++x) {
            // Pack_RGBA 把 R 放在低字节，而 BMP 是 BGR 序 —— 这里必须换过来。
            // 曾经因为顺手写成低字节当 B，整张图红蓝互换，对账时虚报 38% 不一致。
            const uint32_t v = src[x];
            dst[x * 3 + 0] = static_cast<uint8_t>(v >> 16);   // B
            dst[x * 3 + 1] = static_cast<uint8_t>(v >> 8);    // G
            dst[x * 3 + 2] = static_cast<uint8_t>(v);         // R
        }
    }
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        return false;
    }
    f.write(reinterpret_cast<const char*>(buf.data()),
            static_cast<std::streamsize>(buf.size()));
    return true;
}

// 校验 TMP 等距地形瓦片。
//
//   ra2core.exe --tmp <顶层mix> <0x归档ID> <调色板PAL的CRC> <第几个瓦片>
//
// 这条测试一次压三条链路：明文 MIX 索引 -> 嵌套归档 -> TMP 菱形解码。
// 地形归档（ISOGEN.MIX / GENERIC.MIX / TEMPERAT）全是不带 0x00020000 的
// 明文 MIX，之前 C++ 只实现了加密路径，这一整类素材不可见。
static int Verify_Tmp(const char* path, uint32_t arch_id, uint32_t pal_id, int pick,
                      bool render_extra = true) {
    MixFileClass mix;
    if (!mix.Open(path)) {
        std::printf("FAIL MIX 打不开 %s\n", path);
        return 1;
    }
    const MixEntry* arch = mix.Find_By_ID(arch_id);
    if (arch == nullptr) {
        std::printf("FAIL 顶层没有 0x%08X\n", arch_id);
        return 1;
    }
    auto sub = mix.Open_Sub(*arch);
    if (!sub) {
        std::printf("FAIL 0x%08X 不是 MIX（明文 MIX 路径没打通？）\n", arch_id);
        return 1;
    }
    std::printf("OK   嵌套归档 0x%08X 打开：flags=%s 条目=%d\n",
                arch_id, MixFileClass::Describe_Flags(sub->Flags()).c_str(),
                sub->Count());

    Palette pal;
    if (pal_id != 0) {
        std::vector<uint8_t> pd = mix.Read_Deep_By_ID(pal_id);
        if (pd.empty() || !pal.Load(pd.data(), pd.size())) {
            std::printf("WARN 调色板 0x%08X 取不到，PNG/BMP 输出会用灰度\n", pal_id);
        }
    }

    int ok = 0, fail = 0, empty_first = 0, with_extra = 0, with_z = 0;
    int cells_checked = 0;
    std::vector<uint32_t> picked;
    int picked_w = 0, picked_h = 0, picked_idx = -1;
    const std::vector<MixEntry>& ents = sub->Entries();
    for (size_t ei = 0; ei < ents.size(); ++ei) {
        const MixEntry& e = ents[ei];
        std::vector<uint8_t> data = sub->Read_Entry(e);
        TmpFile t;
        if (!t.Load(data.data(), data.size())) {
            ++fail;     // 不是 TMP（归档里混着 PAL / SHP 之类）
            continue;
        }
        ++ok;
        // 硬判据：RA2 的画布恒 60x30，每个非空 cell 的 iso 段恒 900 字节。
        if (t.Cell_Width() != 60 || t.Cell_Height() != 30) {
            std::printf("FAIL id=0x%08X 画布 %dx%d，RA2 应为 60x30\n",
                        e.id, t.Cell_Width(), t.Cell_Height());
            return 1;
        }
        for (const TmpTile& tile : t.Tiles()) {
            if (!tile.present) {
                continue;
            }
            ++cells_checked;
            if (tile.iso.size() != 900) {
                std::printf("FAIL id=0x%08X cell#%d iso=%zu，应为 900\n",
                            e.id, tile.index, tile.iso.size());
                return 1;
            }
            if (tile.header.Has_Extra()) {
                ++with_extra;
            }
            if (tile.header.Has_Z()) {
                ++with_z;
            }
        }
        // pick 的编号和 tools/tmpcmp.py 对齐：按"第几个能解析的模板"数，
        // 空首 cell 的模板也占一个号。
        const int parsed_index = ok - 1;
        if (!t.Tiles()[0].present) {
            // 模板的第一个 cell 是空的完全正常（悬崖/桥类素材常见），
            // 只是没有"代表 cell"可指。
            ++empty_first;
        } else if (parsed_index == pick) {
            picked = t.Render_Image_RGBA(pal, render_extra);
            picked_w = t.Canvas_Width();
            picked_h = t.Canvas_Height();
            picked_idx = static_cast<int>(ei);
            std::printf("INFO 第 %d 个模板 = 条目 #%zu id=0x%08X\n", pick, ei, e.id);
            // 把每个 cell 的 iso 累积成一个 FNV 哈希打出来，和 Python 对账用：
            // 只要哈希不同，就说明"数据切片"两边不一致，跟渲染无关。
            uint32_t h = 2166136261u;
            for (const TmpTile& tile : t.Tiles()) {
                for (uint8_t b : tile.iso) {
                    h ^= b;
                    h *= 16777619u;
                }
            }
            std::printf("INFO iso FNV=0x%08X 画布=%dx%d 原点=(%d,%d)\n", h,
                        t.Canvas_Width(), t.Canvas_Height(),
                        t.Cell_Origin(0).first, t.Cell_Origin(0).second);
        }
    }
    std::printf("OK   TMP 解析 %d 个（非 TMP %d，首 cell 空 %d）；"
                "非空 cell 共 %d，带 extra %d，带 z %d\n",
                ok, fail, empty_first, cells_checked, with_extra, with_z);
    if (ok == 0) {
        std::printf("FAIL 一个 TMP 都没解出来\n");
        return 1;
    }
    if (!picked.empty()) {
        size_t opaque = 0;
        for (uint32_t v : picked) {
            if ((v >> 24) != 0) {
                ++opaque;
            }
        }
        std::printf("OK   第 %d 个模板（条目 #%d）画布 %dx%d，非透明像素 %zu\n",
                    pick, picked_idx, picked_w, picked_h, opaque);
        char out[256];
        std::snprintf(out, sizeof(out), "build/tmp_%08X_%d.bmp", arch_id, pick);
        if (Write_BMP(out, picked.data(), picked_w, picked_h)) {
            std::printf("     已写出 %s\n", out);
        }
    }
    return 0;
}

// 全量回归：把归档里每个 TMP 渲染一遍，逐条打出 (序号, id, 画布, RGBA 哈希)。
// tools/tmphash.py 会用参考实现算同样的表，两边 diff 为空才算过。
// 这比"看几张图"强得多 —— 660 个模板 × 每个几万像素，任何一格解码错都会被抓到。
static int Hash_Tmp(const char* path, uint32_t arch_id, uint32_t pal_id, bool with_extra) {
    MixFileClass mix;
    if (!mix.Open(path)) {
        std::printf("FAIL MIX 打不开 %s\n", path);
        return 1;
    }
    const MixEntry* arch = mix.Find_By_ID(arch_id);
    if (arch == nullptr) {
        std::printf("FAIL 顶层没有 0x%08X\n", arch_id);
        return 1;
    }
    auto sub = mix.Open_Sub(*arch);
    if (!sub) {
        std::printf("FAIL 0x%08X 不是 MIX\n", arch_id);
        return 1;
    }
    Palette pal;
    if (pal_id != 0) {
        std::vector<uint8_t> pd = mix.Read_Deep_By_ID(pal_id);
        pal.Load(pd.data(), pd.size());
    }
    std::printf("# arch=0x%08X flags=%s entries=%d pal=%d extra=%d\n", arch_id,
                MixFileClass::Describe_Flags(sub->Flags()).c_str(), sub->Count(),
                pal.Is_Loaded() ? 1 : 0, with_extra ? 1 : 0);
    int idx = 0, ok = 0;
    for (const MixEntry& e : sub->Entries()) {
        std::vector<uint8_t> data = sub->Read_Entry(e);
        TmpFile t;
        if (!t.Load(data.data(), data.size())) {
            continue;
        }
        const std::vector<uint32_t> px = t.Render_Image_RGBA(pal, with_extra);
        uint32_t h = 2166136261u;
        auto mixv = [&h](uint32_t v) {
            for (int i = 0; i < 4; ++i) {
                h ^= (v >> (i * 8)) & 0xFFu;
                h *= 16777619u;
            }
        };
        mixv(static_cast<uint32_t>(t.Canvas_Width()));
        mixv(static_cast<uint32_t>(t.Canvas_Height()));
        for (uint32_t v : px) {
            mixv(v);
        }
        std::printf("%d 0x%08X %dx%d %zu 0x%08X\n", idx++, e.id,
                    t.Canvas_Width(), t.Canvas_Height(), px.size(), h);
        ++ok;
    }
    std::printf("# 共 %d 个模板\n", ok);
    return 0;
}

// ---- PCX ----
//
// 递归剥开所有嵌套 MIX，把每个 PCX 解成 RGBA 后打哈希。
// tools/pcxhash.py 用参考实现算同一张表，diff 为空才算过。
//
// 为什么用哈希而不是逐像素对账：ra2.mix 里 161 个 PCX 合计 500 多万像素，
// 逐像素输出体量太大；哈希能精确到"哪一张错了"，又不用搬海量数据。

static void Fnv_Bytes(uint32_t* h, const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        *h = (*h ^ p[i]) * 16777619u;
    }
}

static int Hash_Pcx(const char* path, int max_depth = 4) {
    MixFileClass mix;
    if (!mix.Open(path)) {
        std::printf("FAIL MIX 打不开 %s\n", path);
        return 1;
    }
    std::printf("# mix=%s\n", path);

    int seq = 0, ok = 0, bad = 0;
    // 和 tools/pcxdec.py 的 iter_leaves 对齐：只有还没到深度上限时才继续下钻。
    std::function<void(const MixFileClass&, int)> walk = [&](const MixFileClass& m,
                                                             int depth) {
        for (const MixEntry& e : m.Entries()) {
            if (depth < max_depth) {
                auto sub = m.Open_Sub(e);
                if (sub) {
                    walk(*sub, depth + 1);
                    continue;
                }
            }
            const std::vector<uint8_t> d = m.Read_Entry(e);
            // 先用最省字节的判据筛：0A 05 魔数。真正是否成立交给 Load() 判定
            // （编码字节必须是 1、数据区必须刚好吃完），所以 0x08050506 那种
            // 首字节碰巧是 0A 的 101MB 条目不会被误收。
            if (d.size() < 128 || d[0] != 0x0A || d[1] != 0x05) {
                continue;
            }
            PcxFile p;
            if (!p.Load(d.data(), d.size())) {
                ++bad;
                std::printf("BAD %d 0x%08X %zu\n", seq++, e.id, d.size());
                continue;
            }
            const std::vector<uint8_t> rgba = p.To_RGBA();
            uint32_t h = 2166136261u;
            const uint32_t hdr[3] = {static_cast<uint32_t>(p.Width()),
                                     static_cast<uint32_t>(p.Height()),
                                     static_cast<uint32_t>(p.Planes())};
            Fnv_Bytes(&h, reinterpret_cast<const uint8_t*>(hdr), sizeof(hdr));
            Fnv_Bytes(&h, rgba.data(), rgba.size());
            std::printf("%d 0x%08X %dx%d p%d %zu 0x%08X\n", seq++, e.id, p.Width(),
                        p.Height(), p.Planes(), rgba.size(), h);
            ++ok;
        }
    };
    walk(mix, 0);
    std::printf("# 共 %d 个 PCX（解码失败 %d）\n", ok, bad);
    return bad == 0 ? 0 : 1;
}

// 把指定 PCX 解出来写成 24 位 BMP，用来肉眼验收（尤其是 24 位的 R/G/B 平面顺序）。
static int Verify_Pcx(const char* path, uint32_t id) {
    MixFileClass mix;
    if (!mix.Open(path)) {
        std::printf("FAIL MIX 打不开 %s\n", path);
        return 1;
    }
    const std::vector<uint8_t> d = mix.Read_Deep_By_ID(id);
    if (d.empty()) {
        std::printf("FAIL 递归找不到 0x%08X\n", id);
        return 1;
    }
    PcxFile p;
    if (!p.Load(d.data(), d.size())) {
        std::printf("FAIL 0x%08X 不是可解的 PCX（%zu 字节，头 %02X %02X）\n", id, d.size(),
                    d[0], d[1]);
        return 1;
    }
    const std::vector<uint8_t> rgba = p.To_RGBA();
    std::vector<uint32_t> px(static_cast<size_t>(p.Width()) * p.Height());
    for (size_t i = 0; i < px.size(); ++i) {
        // Write_BMP 认的是"R 在低字节"的打包格式（见 Pack_RGBA 的约定）。
        px[i] = static_cast<uint32_t>(rgba[i * 4 + 0]) |
                (static_cast<uint32_t>(rgba[i * 4 + 1]) << 8) |
                (static_cast<uint32_t>(rgba[i * 4 + 2]) << 16);
    }
    std::error_code ec;
    std::filesystem::create_directories("build/pcx", ec);
    char out[512];
    std::snprintf(out, sizeof(out), "build/pcx/0x%08X_%dx%d_p%d.bmp", id, p.Width(),
                  p.Height(), p.Planes());
    if (!Write_BMP(out, px.data(), p.Width(), p.Height())) {
        std::printf("FAIL 写不出 %s\n", out);
        return 1;
    }
    std::printf("OK   0x%08X %dx%d planes=%d bpl=%d 内嵌调色板=%s\n", id, p.Width(),
                p.Height(), p.Planes(), p.Bytes_Per_Line(),
                p.Has_Embedded_Palette() ? "有" : "无");
    std::printf("     已写出 %s\n", out);
    return 0;
}

// INI 解析器的自检。用的样本是**从真实文件里剪出来的行**，
// 每一条都对应一个实测确认过的方言特征 —— 改解析器时这里挂掉就说明改错了。
//
// 语料清单（来源）：
//   [JumpjetControls] ;gs ...      段头行尾注释            rulesmd 570
//   [GAFWLL];temp wall for yuri[YAWALL]  段名取到第一个 ]   rulesmd 13553
//   Burst = 2                      等号两侧空白            rulesmd 22682
//   LetsDoTheTimeWarpOutAgain = X; 值截到 ';'             rulesmd 737
//   Report=                        空值（214 处）          rulesmd [CRNeutronRifle]
//   Report=ChronoLegionAttack      同一段里重复键          rulesmd [CRNeutronRifle]
//   842-GAWETH_ED                  少了等号的畸形行        rulesmd 2508
//   // PCG; ...                    // 注释                 rulesmd 911
//   1=E1 / 2=E2                    编号列表从 1 开始       rulesmd [InfantryTypes]
//   1=TWLT100 / 3=ELECTRO          编号列表跳号            rulesmd [Animations]
//   [VIRUS] / [Virus]              段名仅大小写不同        rulesmd 5154/27076
static int Ini_Self_Test() {
    static const char kSample[] =
        "; rifle soldier weapons (multiple shots)\r\n"
        "[JumpjetControls] ;gs These are now merely defaults\r\n"
        "  Burst = 2\t\t; two at a time\r\n"
        "LetsDoTheTimeWarpOutAgain = ChronoScreenSound; sound for tim\r\n"
        "RefundPercent=50%\r\n"
        "ParachuteMaxFallRate=-3\r\n"
        "Verses=100%,80%,80%,50%,25%,25%,75%,50%,25%,200%\r\n"
        "LegalTargets=infantry,vehicle,building\r\n"
        "EmptyValue=\r\n"
        "842-GAWETH_ED\r\n"
        "// PCG; Provides knobs to tweak\r\n"
        "[GAFWLL];temp wall for yuri[YAWALL]\r\n"
        "Strength=100\r\n"
        "[VIRUS]\r\n"
        "Report=\r\n"
        "Report=ChronoLegionAttack\r\n"
        "[Virus]\r\n"
        "IsRadBeam=yes\r\n"
        "[InfantryTypes]\r\n"
        "1=E1\r\n"
        "2=E2\r\n"
        "3=SHK\r\n"
        "[Animations]\r\n"
        "1=TWLT100\r\n"
        "3=ELECTRO\r\n"
        "12=SMOKEY\r\n";

    int fail = 0;
    auto check = [&fail](bool ok, const char* what) {
        std::printf("%s %s\n", ok ? "OK  " : "FAIL", what);
        if (!ok) {
            ++fail;
        }
    };

    IniFile ini;
    if (!ini.Load(kSample)) {
        std::printf("FAIL INI 自检样本解析失败\n");
        return 1;
    }
    check(ini.Malformed_Lines() == 1, "畸形行（少了等号的 842-GAWETH_ED）被跳过并计数");
    // 段：JumpjetControls / GAFWLL / VIRUS(含 Virus 合并) / InfantryTypes / Animations
    check(ini.Section_Count() == 5, "段数=5（[Virus] 并入 [VIRUS]，没有多出来）");
    check(ini.Has_Section("jumpjetcontrols") && ini.Has_Section("JUMPJETCONTROLS"),
          "段名大小写不敏感");
    check(ini.Has_Section("gafwll") && !ini.Has_Section("gafwll]temp wall for yuri[yawall"),
          "段名取到第一个 ']'，后面的注释不进段名");

    check(ini.Get_Int("JumpjetControls", "Burst", 0) == 2, "键两侧空白/TAB 被吃掉");
    check(ini.Get_String("JumpjetControls", "LetsDoTheTimeWarpOutAgain") == "ChronoScreenSound",
          "值截断到第一个 ';'");
    check(ini.Get_Int("JumpjetControls", "RefundPercent", 0) == 50, "百分比按 atoi 语义取整数");
    check(ini.Get_Double("JumpjetControls", "RefundPercent", 0.0) == 50.0, "小数读取");
    check(ini.Get_Int("JumpjetControls", "ParachuteMaxFallRate", 0) == -3, "负数值");
    check(ini.Get_String("JumpjetControls", "EmptyValue", "X").empty(), "空值");
    check(ini.Get_Int("JumpjetControls", "不存在的键", 77) == 77, "找不到键时返回默认值");

    std::vector<int> verses;
    ini.Get_Percent_List("JumpjetControls", "Verses", &verses);
    check(verses.size() == 10 && verses[0] == 100 && verses[8] == 25 && verses[9] == 200,
          "百分比列表：10 项，含超过 100 的 200%");

    std::vector<std::string> targets;
    ini.Get_String_List("JumpjetControls", "LegalTargets", &targets);
    check(targets.size() == 3 && targets[0] == "infantry" && targets[2] == "building",
          "字符串列表");

    check(ini.Get_String("VIRUS", "Report") == "", "段内重复键：按名取到第一个（空值）");
    check(ini.Entry_Count("VIRUS") == 3, "段内重复键全部保留（2 个键 + 合并来的 1 个）");
    check(ini.Get_Bool("VIRUS", "IsRadBeam", false), "[Virus] 的条目并进了 [VIRUS]");

    std::vector<std::string> inf;
    ini.Read_Numbered_List("InfantryTypes", &inf);
    check(inf.size() == 3 && inf[0] == "E1" && inf[2] == "SHK",
          "编号列表从 1 开始也能读全（0 不存在）");
    std::vector<std::string> anim;
    ini.Read_Numbered_List("Animations", &anim);
    check(anim.size() == 3 && anim[0] == "TWLT100" && anim[1] == "ELECTRO" &&
              anim[2] == "SMOKEY",
          "编号列表跳号也能读全（1,3,12）");

    if (fail != 0) {
        std::printf("INI 自检失败 %d 项\n", fail);
        return 1;
    }
    std::printf("OK   INI 自检全部通过\n");
    return 0;
}

// 把 INI 解析结果按"规范化文本"打出来，供 tools/inidump.py 逐行对账。
// 规范化规则必须和参考实现字节一致：
//   段名 = '[' 与第一个 ']' 之间（trim）
//   键   = 第一个 '=' 之前（trim）
//   值   = 第一个 '=' 之后、第一个 ';' 之前（trim）
// 保留原始大小写 —— 这样对账同时能验出"是不是把大小写弄丢了"。
static int Dump_Ini(const char* path) {
    IniFile ini;
    if (!ini.Load_File(path)) {
        std::printf("FAIL INI 打不开或为空: %s\n", path);
        return 1;
    }
    std::printf("# sections=%d entries=%d malformed=%d\n", ini.Section_Count(),
                [&] {
                    int n = 0;
                    for (int i = 0; i < ini.Section_Count(); ++i) {
                        n += static_cast<int>(ini.Section(i)->entries.size());
                    }
                    return n;
                }(),
                ini.Malformed_Lines());
    for (int i = 0; i < ini.Section_Count(); ++i) {
        const IniSection* s = ini.Section(i);
        std::printf("[%s]\n", s->name.c_str());
        for (const IniEntry& e : s->entries) {
            std::printf("%s=%s\n", e.key.c_str(), e.value.c_str());
        }
    }
    return 0;
}

// 端到端：从 MIX（含嵌套子 MIX）里直接取出 INI 解析。
//   ra2core.exe --ini <顶层mix> <0xINI的CRC>
// 这条链路一次压三层：明文/加密 MIX 索引 -> 嵌套归档 -> INI 方言。
static int Verify_Ini(const char* path, uint32_t ini_id) {
    MixFileClass mix;
    if (!mix.Open(path)) {
        std::printf("FAIL MIX 打不开 %s\n", path);
        return 1;
    }
    std::vector<uint8_t> data = mix.Read_Deep_By_ID(ini_id);
    if (data.empty()) {
        std::printf("FAIL 递归找不到 0x%08X\n", ini_id);
        return 1;
    }
    IniFile ini;
    if (!ini.Load(data.data(), data.size())) {
        std::printf("FAIL INI 解析失败（%zu 字节）\n", data.size());
        return 1;
    }
    int entries = 0;
    for (int i = 0; i < ini.Section_Count(); ++i) {
        entries += static_cast<int>(ini.Section(i)->entries.size());
    }
    std::printf("OK   0x%08X %zu 字节 -> %d 段 / %d 条目 / 畸形行 %d\n",
                ini_id, data.size(), ini.Section_Count(), entries,
                ini.Malformed_Lines());

    // 类型表规模：这几个数字就是 P2 数据层要实例化的对象数量。
    static const char* kLists[] = {"InfantryTypes", "VehicleTypes", "AircraftTypes",
                                   "BuildingTypes", "TerrainTypes", "SmudgeTypes",
                                   "OverlayTypes", "Animations", "VoxelAnims",
                                   "Particles", "Weapons", "Warheads", "SuperWeaponTypes"};
    for (const char* name : kLists) {
        std::vector<std::string> list;
        const int n = ini.Read_Numbered_List(name, &list);
        if (n > 0) {
            std::printf("     [%-16s] %4d 项   首=%s 末=%s\n", name, n,
                        list.front().c_str(), list.back().c_str());
        }
    }
    return 0;
}

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--ini") == 0) {
        if (argc < 4) {
            std::printf("用法：ra2core --ini <顶层mix> <0xINI的CRC>\n");
            return 1;
        }
        return Verify_Ini(argv[2],
                          static_cast<uint32_t>(std::strtoul(argv[3], nullptr, 16)));
    }
    if (argc > 1 && std::strcmp(argv[1], "--initest") == 0) {
        return Ini_Self_Test();
    }
    if (argc > 1 && std::strcmp(argv[1], "--inidump") == 0) {
        if (argc < 3) {
            std::printf("用法：ra2core --inidump <path.ini>\n");
            return 1;
        }
        return Dump_Ini(argv[2]);
    }
    if (argc > 1 && std::strcmp(argv[1], "--pcxhash") == 0) {
        if (argc < 3) {
            std::printf("用法：ra2core --pcxhash <顶层mix> [最大嵌套深度]\n");
            return 1;
        }
        const int d = (argc > 3) ? std::atoi(argv[3]) : 4;
        return Hash_Pcx(argv[2], d);
    }
    if (argc > 1 && std::strcmp(argv[1], "--pcx") == 0) {
        if (argc < 4) {
            std::printf("用法：ra2core --pcx <顶层mix> <0xPCX的CRC>\n");
            return 1;
        }
        return Verify_Pcx(argv[2],
                          static_cast<uint32_t>(std::strtoul(argv[3], nullptr, 16)));
    }
    if (argc > 1 && std::strcmp(argv[1], "--tmphash") == 0) {
        if (argc < 4) {
            std::printf("用法：ra2core --tmphash <顶层mix> <0x归档ID> [0x调色板ID] [iso]\n");
            return 1;
        }
        const uint32_t arch = static_cast<uint32_t>(std::strtoul(argv[3], nullptr, 16));
        const uint32_t pal = (argc > 4)
                                 ? static_cast<uint32_t>(std::strtoul(argv[4], nullptr, 16))
                                 : 0u;
        const bool with_extra = !(argc > 5 && std::strcmp(argv[5], "iso") == 0);
        return Hash_Tmp(argv[2], arch, pal, with_extra);
    }
    if (argc > 1 && std::strcmp(argv[1], "--tmp") == 0) {
        if (argc < 4) {
            std::printf("用法：ra2core --tmp <顶层mix> <0x归档ID> [0x调色板ID] [第几张]\n");
            return 1;
        }
        const uint32_t arch = static_cast<uint32_t>(std::strtoul(argv[3], nullptr, 16));
        const uint32_t pal = (argc > 4)
                                 ? static_cast<uint32_t>(std::strtoul(argv[4], nullptr, 16))
                                 : 0u;
        const int pick = (argc > 5) ? std::atoi(argv[5]) : 0;
        // 第 6 个参数传 "iso" 就只画菱形、不贴 extra —— 用于把解码错误和
        // extra 合成错误分开定位。
        const bool with_extra = !(argc > 6 && std::strcmp(argv[6], "iso") == 0);
        return Verify_Tmp(argv[2], arch, pal, pick, with_extra);
    }
    if (argc > 1) {
        return Verify_Mix(argv[1]);  // 只做 MIX 校验，不跑下面的基准
    }

    // 素材层自检：INI 方言的每个坑都在这里钉住（不依赖外部文件）。
    if (Ini_Self_Test() != 0) {
        return 1;
    }

    // ---- 地图 ----
    MapClass map;
    constexpr int kW = 128;
    constexpr int kH = 128;
    map.Init_Clear(kW, kH);

    // 随机铺一些障碍，构造出需要绕路的场景。
    std::mt19937 rng(20011031);  // 固定种子：测试必须可复现
    for (int i = 0; i < kW * kH / 8; ++i) {
        CellStruct c{static_cast<int16_t>(rng() % kW), static_cast<int16_t>(rng() % kH)};
        if (CellClass* cell = map.Cell_At(c)) {
            cell->Set_Land(LandType::Rock);
        }
    }

    // ---- 构造一批寻路请求 ----
    std::vector<PathRequest> reqs;
    for (int i = 0; i < 256; ++i) {
        PathRequest r;
        r.from = CellStruct{static_cast<int16_t>(rng() % kW), static_cast<int16_t>(rng() % kH)};
        r.to = CellStruct{static_cast<int16_t>(rng() % kW), static_cast<int16_t>(rng() % kH)};
        r.ticket = static_cast<uint32_t>(i);
        r.max_cost = 4096;
        reqs.push_back(r);
    }

    // ---- 串行基准 ----
    PathFinder serial(&map);
    const auto t0 = std::chrono::steady_clock::now();
    std::vector<PathResult> base = serial.Solve(reqs);
    const auto t1 = std::chrono::steady_clock::now();
    const double serial_ms =
        std::chrono::duration<double, std::milli>(t1 - t0).count();

    // ---- 并行 ----
    TaskSystem pool;
    PathFinder parallel(&map);
    const auto t2 = std::chrono::steady_clock::now();
    std::vector<PathResult> test = parallel.Solve_Parallel(pool, reqs);
    const auto t3 = std::chrono::steady_clock::now();
    const double par_ms = std::chrono::duration<double, std::milli>(t3 - t2).count();

    // ---- 一致性校验 ----
    int found = 0;
    for (size_t i = 0; i < base.size(); ++i) {
        if (base[i].found) {
            ++found;
        }
        if (!SameResult(base[i], test[i])) {
            std::printf("FAIL: 第 %zu 条路径串行/并行结果不一致\n", i);
            return 1;
        }
    }

    // ---- 帧队列的确定性 ----
    FrameQueue q;
    std::vector<FrameEvent> out;
    q.Submit_Local(0, {FrameEvent{0, 1, 10, 20, 0}, FrameEvent{0, 2, 1, 2, 3}});
    q.Submit_Remote(1, 0, {FrameEvent{1, 3, 5, 6, 7}});
    if (q.Execute(out) != FrameStepResult::Ok || out.size() != 3) {
        std::printf("FAIL: 帧队列未能在两方输入齐备时推进\n");
        return 1;
    }
    // 只有一方提交时应当卡住 —— 这正是锁步的语义。
    q.Submit_Local(1, {FrameEvent{0, 1, 0, 0, 0}});
    std::vector<FrameEvent> out2;
    if (q.Execute(out2) != FrameStepResult::WaitingForPlayers) {
        std::printf("FAIL: 缺少远端输入时不应推进\n");
        return 1;
    }

    std::printf("OK  路径 %zu 条（命中 %d），串行 %.2f ms，并行 %.2f ms，加速 %.2fx\n",
                base.size(), found, serial_ms, par_ms,
                par_ms > 0.0 ? serial_ms / par_ms : 0.0);
    std::printf("OK  锁步帧队列行为正确，帧 CRC = 0x%08X\n", q.Compute_CRC());

    // ---- 类层次 ----
    // 这部分不再是推断，而是 gamemd.exe 的 MSVC RTTI 原文（src/re/ClassHierarchy.h）。
    // 下面几条 static_assert 是回归测试：一旦重新抽取的类表不再满足这些关系，
    // 说明要么 RTTI 解析退化了，要么我们对引擎对象模型的理解是错的。
    static_assert(re::IsDerivedFrom(vtable::cls::kUnitClass, vtable::cls::kTechnoClass),
                  "UnitClass 必须派生自 TechnoClass");
    static_assert(re::IsDerivedFrom(vtable::cls::kInfantryClass, vtable::cls::kFootClass),
                  "InfantryClass 必须派生自 FootClass");
    static_assert(re::IsDerivedFrom(vtable::cls::kBuildingClass, vtable::cls::kTechnoClass),
                  "BuildingClass 必须派生自 TechnoClass");
    static_assert(!re::IsDerivedFrom(vtable::cls::kAnimClass, vtable::cls::kTechnoClass),
                  "AnimClass 挂在 ObjectClass 下，不是 TechnoClass");

    // 类大小：来自二进制里 `push <size>; call operator new` 的实测值，
    // 不是按字段推算的。这几个数对多核改造是硬约束 —— 对象 2KB 级别，
    // 一次遍历的访存量远大于计算量，并行必须按缓存行切分而不是按对象切分。
    const uint32_t sz_unit = re::SizeOf("UnitClass");
    const uint32_t sz_inf = re::SizeOf("InfantryClass");
    const uint32_t sz_air = re::SizeOf("AircraftClass");
    if (sz_unit == 0 || sz_inf == 0 || sz_air == 0) {
        std::printf("FAIL: 关键类的 sizeof 未能从二进制中提取\n");
        return 1;
    }
    std::printf("INFO sizeof 实测：UnitClass=%u InfantryClass=%u AircraftClass=%u（共 %d 条）\n",
                sz_unit, sz_inf, sz_air, re::kSizeCount);

    std::printf("INFO 类层次取自 RTTI 实证：%d 个类 / %d 个虚表槽位\n",
                re::kClassCount, re::kFlatSlotCount);
    std::printf("     UnitClass 链：");
    for (int id = vtable::cls::kUnitClass; id >= 0;
         id = re::kClassTable[id].base_index) {
        std::printf("%s(%d槽) ", re::kClassTable[id].name, re::kClassTable[id].slots);
    }
    std::printf("\n");
    return 0;
}
