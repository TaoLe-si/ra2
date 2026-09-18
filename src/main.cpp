// main.cpp -- 冒烟测试：验证"并行结果与串行逐条一致"
//
// 这个测试是整个多核改造的验收基准：
// 只要它挂了，就说明某个并行改动破坏了确定性，联机必然失步。
//
// 构建： cmake -B build && cmake --build build
// 运行： ./build/ra2core

#include <algorithm>
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
#include "core/GameVersion.h"
#include "core/VTableMap.h"
#include "data/Ini.h"
#include "data/TypeDB.h"
#include "data/UnitModel.h"
#include "re/FieldOffsets.h"
#include "re/FieldNames.h"
#include "re/ObjectModel.h"
#include "re/ObjectSizes.h"
#include "engine/FrameQueue.h"
#include "gfx/HvaFile.h"
#include "gfx/Palette.h"
#include "gfx/PcxFile.h"
#include "gfx/RemapTable.h"
#include "gfx/TmpFile.h"
#include "gfx/VxlFile.h"
#include "gfx/VxlNormals.h"
#include "gfx/VoxelLight.h"
#include "io/FileSystem.h"
#include "io/MixCrypto.h"
#include "io/Lzo1x.h"
#include "map/Map.h"
#include "map/MapFile.h"
#include "map/MapRenderer.h"
#include "map/TheaterFile.h"
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

// FNV-1a，逐字节。C++ 与 Python 两边必须喂完全相同顺序的字节才能对得上账。
static void Fnv_Bytes(uint32_t* h, const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        *h = (*h ^ p[i]) * 16777619u;
    }
}

// ---- HVA ----
//
// HVA 没有魔数（游戏是按 `名字 + ".HVA"` 拼名查的），所以这里不能像 PCX 那样
// 先按首字节筛 —— 只能对每个叶子条目硬试一次 Load()，靠长度算术判定。
// 判据见 HvaFile.cpp：帧数/肢数合理 + 24+16L+48FL == size + 肢体名可打印。

static int Hash_Hva(const char* path, int max_depth = 4) {
    MixFileClass mix;
    if (!mix.Open(path)) {
        std::printf("FAIL MIX 打不开 %s\n", path);
        return 1;
    }
    std::printf("# mix=%s\n", path);

    int seq = 0, ok = 0;
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
            HvaFile h;
            if (!h.Load(d.data(), d.size())) {
                continue;
            }
            uint32_t hv = 2166136261u;
            const uint32_t hdr[2] = {static_cast<uint32_t>(h.Frame_Count()),
                                     static_cast<uint32_t>(h.Limb_Count())};
            Fnv_Bytes(&hv, reinterpret_cast<const uint8_t*>(hdr), sizeof(hdr));
            for (int i = 0; i < h.Limb_Count(); ++i) {
                const std::string n = h.Limb_Name(i);
                Fnv_Bytes(&hv, reinterpret_cast<const uint8_t*>(n.data()), n.size());
            }
            for (int f = 0; f < h.Frame_Count(); ++f) {
                for (int l = 0; l < h.Limb_Count(); ++l) {
                    const HvaMatrix mm = h.Matrix(f, l);
                    Fnv_Bytes(&hv, reinterpret_cast<const uint8_t*>(mm.m), sizeof(mm.m));
                }
            }
            std::printf("%d 0x%08X %dx%d %s 0x%08X\n", seq++, e.id, h.Frame_Count(),
                        h.Limb_Count(), h.Limb_Name(0).c_str(), hv);
            ++ok;
        }
    };
    walk(mix, 0);
    std::printf("# 共 %d 个 HVA\n", ok);
    return 0;
}

static int Verify_Hva(const char* path, uint32_t id) {
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
    HvaFile h;
    if (!h.Load(d.data(), d.size())) {
        std::printf("FAIL 0x%08X 不是自洽的 HVA（%zu 字节）\n", id, d.size());
        return 1;
    }
    std::printf("OK   0x%08X 源路径=\"%s\"  %d 帧 × %d 肢体\n", id,
                h.Source_Path().c_str(), h.Frame_Count(), h.Limb_Count());
    for (int i = 0; i < h.Limb_Count() && i < 16; ++i) {
        std::printf("     肢体[%2d] = %s\n", i, h.Limb_Name(i).c_str());
    }
    const HvaMatrix m0 = h.Matrix(0, 0);
    std::printf("     第 0 帧矩阵 平移 = (%.4f, %.4f, %.4f)\n", m0.Tx(), m0.Ty(), m0.Tz());
    for (int r = 0; r < 3; ++r) {
        std::printf("       [%6.3f %6.3f %6.3f | %8.3f]\n", m0.At(r, 0), m0.At(r, 1),
                    m0.At(r, 2), m0.At(r, 3));
    }
    return 0;
}

// ---- VXL ----
//
// VXL 有魔数（"Voxel Animation"），过滤比 HVA 省事：先比 15 个字节，再交给
// Load() 做长度自洽判定。哈希喂的字节序列必须和 tools/vxlhash.py 逐字节一致：
//
//   u32 palette_count, num_limbs, num_limb_frames, body_size
//   u8  remap_start, remap_end
//   每根肢体：
//     u32 名字长度 + 名字字节 + i32 number + u32 unk1 + u32 unk2
//     u32 span_start_ofs, span_end_ofs, span_data_ofs, det(float 原样 4 字节)
//     u32 × 12 变换矩阵 + u32 × 3 min + u32 × 3 max
//     u8 x_size, y_size, z_size, normals_type
//     u32 体素数 + 每个体素 5 字节 (x, y, z, colour, normal)
//
// 体素部分才是真正的验收点：外层结构对不对只看长度算术，列内编码错了
// 体素坐标/颜色就会整片偏掉，哈希立刻对不上。

static void Fnv_U32(uint32_t* h, uint32_t v) {
    Fnv_Bytes(h, reinterpret_cast<const uint8_t*>(&v), sizeof(v));
}

static void Fnv_Str(uint32_t* h, const std::string& s) {
    Fnv_U32(h, static_cast<uint32_t>(s.size()));
    Fnv_Bytes(h, reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

static uint32_t Hash_One_Vxl(const VxlFile& v) {
    uint32_t h = 2166136261u;
    Fnv_U32(&h, static_cast<uint32_t>(v.Palette_Count()));
    Fnv_U32(&h, static_cast<uint32_t>(v.Limb_Count()));
    Fnv_U32(&h, static_cast<uint32_t>(v.Limb_Frame_Count()));
    Fnv_U32(&h, v.Body_Size());
    const uint8_t remap[2] = {v.Remap_Start(), v.Remap_End()};
    Fnv_Bytes(&h, remap, sizeof(remap));
    Fnv_Bytes(&h, v.Palette(), 768);

    for (int l = 0; l < v.Limb_Count(); ++l) {
        const VxlLimbHeader& hd = v.Header(l);
        const VxlLimbTailer& t = v.Tailer(l);
        Fnv_Str(&h, hd.name);
        const uint8_t num[12] = {
            static_cast<uint8_t>(hd.number), static_cast<uint8_t>(hd.number >> 8),
            static_cast<uint8_t>(hd.number >> 16), static_cast<uint8_t>(hd.number >> 24),
            static_cast<uint8_t>(hd.unk1), static_cast<uint8_t>(hd.unk1 >> 8),
            static_cast<uint8_t>(hd.unk1 >> 16), static_cast<uint8_t>(hd.unk1 >> 24),
            static_cast<uint8_t>(hd.unk2), static_cast<uint8_t>(hd.unk2 >> 8),
            static_cast<uint8_t>(hd.unk2 >> 16), static_cast<uint8_t>(hd.unk2 >> 24)};
        Fnv_Bytes(&h, num, sizeof(num));
        Fnv_U32(&h, t.span_start_ofs);
        Fnv_U32(&h, t.span_end_ofs);
        Fnv_U32(&h, t.span_data_ofs);
        Fnv_Bytes(&h, reinterpret_cast<const uint8_t*>(&t.det), 4);
        Fnv_Bytes(&h, reinterpret_cast<const uint8_t*>(t.transform), sizeof(t.transform));
        Fnv_Bytes(&h, reinterpret_cast<const uint8_t*>(t.min_bounds), sizeof(t.min_bounds));
        Fnv_Bytes(&h, reinterpret_cast<const uint8_t*>(t.max_bounds), sizeof(t.max_bounds));
        const uint8_t dims[4] = {t.x_size, t.y_size, t.z_size, t.normals_type};
        Fnv_Bytes(&h, dims, sizeof(dims));

        std::vector<VxlVoxel> vox;
        if (!v.Decode_Limb(l, &vox)) {
            Fnv_U32(&h, 0xDEADBEEFu);   // 解码失败也要留下痕迹
            continue;
        }
        Fnv_U32(&h, static_cast<uint32_t>(vox.size()));
        std::vector<uint8_t> packed(vox.size() * 5);
        for (size_t i = 0; i < vox.size(); ++i) {
            packed[i * 5 + 0] = vox[i].x;
            packed[i * 5 + 1] = vox[i].y;
            packed[i * 5 + 2] = vox[i].z;
            packed[i * 5 + 3] = vox[i].colour;
            packed[i * 5 + 4] = vox[i].normal;
        }
        Fnv_Bytes(&h, packed.data(), packed.size());
    }
    return h;
}

static int Hash_Vxl(const char* path, int max_depth = 4) {
    MixFileClass mix;
    if (!mix.Open(path)) {
        std::printf("FAIL MIX 打不开 %s\n", path);
        return 1;
    }
    std::printf("# mix=%s\n", path);

    int seq = 0, ok = 0, bad = 0;
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
            if (d.size() < 802 || std::memcmp(d.data(), "Voxel Animation", 15) != 0) {
                continue;
            }
            VxlFile v;
            if (!v.Load(d.data(), d.size())) {
                ++bad;
                std::printf("BAD %d 0x%08X %zu\n", seq++, e.id, d.size());
                continue;
            }
            size_t nvox = 0;
            const bool vox_ok = v.Voxel_Count(&nvox, false);
            std::printf("%d 0x%08X limbs=%d body=%u vox=%zu%s 0x%08X\n", seq++, e.id,
                        v.Limb_Count(), v.Body_Size(), nvox, vox_ok ? "" : " 解码异常",
                        Hash_One_Vxl(v));
            ++ok;
        }
    };
    walk(mix, 0);
    std::printf("# 共 %d 个 VXL（结构失败 %d）\n", ok, bad);
    return bad == 0 ? 0 : 1;
}

// 把指定 VXL 解出来做成等距投影的 24 位 BMP —— 这是体素链路唯一能靠肉眼
// 一眼判断对错的验收方式：位置错一列、颜色/法线换序，图上立刻就不像车了。
static int Verify_Vxl(const char* path, uint32_t id) {
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
    VxlFile v;
    if (!v.Load(d.data(), d.size())) {
        std::printf("FAIL 0x%08X 不是自洽的 VXL（%zu 字节）\n", id, d.size());
        return 1;
    }
    std::printf("OK   0x%08X limbs=%d body=%u pal=%d remap=(%d,%d)\n", id, v.Limb_Count(),
                v.Body_Size(), v.Palette_Count(), v.Remap_Start(), v.Remap_End());
    for (int l = 0; l < v.Limb_Count(); ++l) {
        const VxlLimbHeader& hd = v.Header(l);
        const VxlLimbTailer& t = v.Tailer(l);
        size_t n = 0;
        std::vector<VxlVoxel> vox;
        const bool okv = v.Decode_Limb(l, &vox);
        n = vox.size();
        std::printf("     limb[%2d] %-12s num=%d unk=(%u,%u) %dx%dx%d nt=%d det=%g "
                    "span=(%u,%u,%u) 体素=%zu%s\n",
                    l, hd.name.c_str(), hd.number, hd.unk1, hd.unk2, t.x_size, t.y_size,
                    t.z_size, t.normals_type, t.det, t.span_start_ofs, t.span_end_ofs,
                    t.span_data_ofs, n, okv ? "" : " (解码异常)");
    }

    // 体素 -> 世界坐标（套肢体变换）-> 等距投影 -> 画家算法落索引图。
    // 光栅化本体放在 VxlFile::Render_Isometric，查看器走的是同一份实现，
    // 免得"验证台画对了、查看器画错了"这种两边不一致的坑。
    std::vector<uint8_t> idx;
    int w = 0, h = 0;
    if (!v.Render_Isometric(&idx, &w, &h, 8.0f)) {
        std::printf("FAIL 体素光栅化失败\n");
        return 1;
    }
    // 索引图 -> 24 位 BMP。调色板直接取用：VXL 里存的已经是展开好的 8 位值，
    // 千万不能 <<2 —— 实测踩过，整个炮塔会变成青紫洋红一片。
    const uint8_t* pal = v.Palette();
    std::vector<uint32_t> px(static_cast<size_t>(w) * h, 0u);
    for (size_t i = 0; i < px.size(); ++i) {
        const uint8_t c = idx[i];
        px[i] = pal[c * 3 + 0] | (static_cast<uint32_t>(pal[c * 3 + 1]) << 8) |
                (static_cast<uint32_t>(pal[c * 3 + 2]) << 16);
    }
    std::error_code ec;
    std::filesystem::create_directories("build/vxl", ec);
    char out[512];
    std::snprintf(out, sizeof(out), "build/vxl/0x%08X_%dx%d.bmp", id, w, h);
    if (!Write_BMP(out, px.data(), w, h)) {
        std::printf("FAIL 写不出 %s\n", out);
        return 1;
    }
    std::printf("     已写出 %s\n", out);
    return 0;
}

// ---- PCX ----
//
// 递归剥开所有嵌套 MIX，把每个 PCX 解成 RGBA 后打哈希。
// tools/pcxhash.py 用参考实现算同一张表，diff 为空才算过。
//
// 为什么用哈希而不是逐像素对账：ra2.mix 里 161 个 PCX 合计 500 多万像素，
// 逐像素输出体量太大；哈希能精确到"哪一张错了"，又不用搬海量数据。

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

// ---- P2 数据层：按 INI 把单位名解析成体素模型组成 ----
//
// 判据是"能不能自证"：86 个 Voxel=yes 单位的车体 VXL **必须全部命中**，
// 缺一个就说明 Image= / CRC 大小写 / 段合并这三处里有一处写错了。
// --dump 会把整张表写出去，交给 tools/unitvxl_check.py 与 Python 参考实现逐行对账。
// ---- 版本识别：RA2 与 YR 是"打底 + 覆盖"，不是一个目录两套无关素材 ----
static int Detect_Game(const char* dir) {
    using namespace ra2;
    std::vector<GameVersion> installed = GameInstall::Detect_Installed(dir);
    std::printf("目录 %s 里检测到 %zu 个版本：", dir, installed.size());
    for (GameVersion v : installed) {
        std::printf("%s ", Version_Name(v));
    }
    std::printf("（自动选择：%s）\n\n", Version_Name(GameInstall::Guess(dir)));
    if (installed.empty()) {
        std::printf("[x] 既没找到 game.exe+ra2.mix，也没找到 gamemd.exe+ra2md.mix\n");
        return 1;
    }
    int rc = 0;
    for (GameVersion v : installed) {
        GamePaths p;
        std::string missing;
        const bool ok = GameInstall::Resolve(dir, v, &p, &missing);
        GameInstall::Dump(p);
        if (!missing.empty()) {
            std::printf("   缺失（不致命）：%s\n", missing.c_str());
        }
        if (!ok) {
            std::printf("   [x] 缺核心文件\n");
            rc = 1;
        }
        std::printf("\n");
    }
    return rc;
}

static int Unit_DB(const std::vector<std::string>& mix_paths, const char* dump_path) {
    std::vector<ra2::MixFileClass> mixes(mix_paths.size());
    std::vector<const ra2::MixFileClass*> ptrs(mix_paths.size());
    for (size_t i = 0; i < mix_paths.size(); ++i) {
        if (!mixes[i].Open(mix_paths[i].c_str())) {
            std::printf("[x] 打不开 %s\n", mix_paths[i].c_str());
            return 1;
        }
        ptrs[i] = &mixes[i];
    }
    ra2::UnitModelDB db;
    if (!db.Load(ptrs.data(), static_cast<int>(ptrs.size()))) {
        return 1;
    }

    const ra2::UnitModelDB::Stats& s = db.Stats_();
    std::printf("段: rules=%d art=%d | 单位 %d | 体素 %d | 炮塔 %d | 炮管 %d | 车体缺失 %d\n",
                s.rules_sections, s.art_sections, s.units, s.voxel,
                s.with_turret, s.with_barrel, s.body_missing);

    if (dump_path) {
        std::FILE* f = std::fopen(dump_path, "wb");
        if (!f) {
            std::printf("[x] 写不了 %s\n", dump_path);
            return 1;
        }
        std::fprintf(f, "# unit image voxel turret bodyid bodypresent "
                        "turid turpresent barlid barlpresent\n");
        for (const std::string& u : db.Units()) {
            const ra2::UnitModel* m = db.Resolve(u.c_str());
            if (!m) {
                continue;
            }
            std::fprintf(f, "%s %s %d %d 0x%08X %d 0x%08X %d 0x%08X %d\n",
                         m->unit.c_str(), m->image.c_str(),
                         m->voxel ? 1 : 0, m->turret ? 1 : 0,
                         m->body.id, m->body.present ? 1 : 0,
                         m->turret_vxl.id, m->turret_vxl.present ? 1 : 0,
                         m->barrel_vxl.id, m->barrel_vxl.present ? 1 : 0);
        }
        std::fclose(f);
        std::printf("已写出 %s\n", dump_path);
        return 0;
    }

    db.Dump(true);
    return s.body_missing == 0 ? 0 : 1;
}

/// P2 全量打表：把 rules / art / sound / theme 装进 TypeDB，并**逐键对账**。
///
/// 验收标准不是"看着对"，而是可数的：
///   1. 每个单位的 rules 段与 art 段，**去重键数**必须与表里一致（不丢键、不添键）；
///   2. 每个键的**值必须逐字节相等**（不改值）。
/// 打印那一行 `逐键对账：不一致 0 处` 就是这一阶段的通过判据。
///
/// --raw <out>：把全量键值按 `单位/来源/键/值` 落盘，供独立实现（tools/techno.py）
///              逐行 diff。两边都排序输出，所以不依赖插入顺序。
static int Type_Table(const std::vector<std::string>& mix_paths, const char* raw_dump,
                      const char* summary_path, int top_keys) {
    std::vector<ra2::MixFileClass> mixes(mix_paths.size());
    std::vector<const ra2::MixFileClass*> ptrs(mix_paths.size());
    for (size_t i = 0; i < mix_paths.size(); ++i) {
        if (!mixes[i].Open(mix_paths[i].c_str())) {
            std::printf("[x] 打不开 %s\n", mix_paths[i].c_str());
            return 1;
        }
        ptrs[i] = &mixes[i];
    }
    ra2::UnitModelDB unitdb;
    if (!unitdb.Load(ptrs.data(), static_cast<int>(ptrs.size()))) {
        return 1;
    }
    ra2::TypeDB db;
    if (!db.Load(unitdb, ptrs.data(), static_cast<int>(ptrs.size()))) {
        return 1;
    }

    db.Dump_Summary(stdout, top_keys);

    // 逐键对账：表是从这两个 IniFile 建的，所以直接拿它们回查。
    const int bad = db.Verify(unitdb.Rules(), unitdb.Art(), stdout);
    std::printf("\n逐键对账：不一致 %d 处（%d 个 TechnoType × rules+art）\n", bad,
                db.Stats_().types);
    if (bad == 0) {
        std::printf("OK   键一个不丢、值一个不改\n");
    }

    if (summary_path) {
        std::FILE* f = std::fopen(summary_path, "wb");
        if (f) {
            db.Dump_Summary(f, top_keys);
            std::fclose(f);
            std::printf("已写出 %s\n", summary_path);
        }
    }
    if (raw_dump) {
        std::FILE* f = std::fopen(raw_dump, "wb");
        if (!f) {
            std::printf("[x] 写不了 %s\n", raw_dump);
            return 1;
        }
        db.Dump_Raw(f);
        std::fclose(f);
        std::printf("已写出 %s\n", raw_dump);
    }
    return bad == 0 ? 0 : 1;
}

/// 体素光影：渲染一个单位并出图，同时做"顶面亮 / 底面暗"的硬判据自检。
///
/// 为什么要自检而不是"看着差不多"：本环境看不到图，判断只能靠数值。
/// 判据来自常识之外的东西 —— 法线表本身（外表面法线朝外，94% 命中），
/// 所以"朝上的面必须比朝下的面亮"是可以直接算出来的。
static int Vxl_Light(const std::vector<std::string>& mix_paths, const char* unit_name,
                     const char* out_path, const float* light3) {
    std::vector<MixFileClass> mixes(mix_paths.size());
    for (size_t i = 0; i < mix_paths.size(); ++i) {
        if (!mixes[i].Open(mix_paths[i].c_str())) {
            std::printf("[x] 打不开 %s\n", mix_paths[i].c_str());
            return 1;
        }
    }
    auto read_any = [&](uint32_t id, std::vector<uint8_t>* out) -> bool {
        for (size_t i = mixes.size(); i-- > 0;) {
            *out = mixes[i].Read_Deep_By_ID(id);
            if (!out->empty()) {
                return true;
            }
        }
        return false;
    };

    std::vector<const MixFileClass*> ptrs(mixes.size());
    for (size_t i = 0; i < mixes.size(); ++i) {
        ptrs[i] = &mixes[i];
    }
    UnitModelDB db;
    if (!db.Load(ptrs.data(), static_cast<int>(ptrs.size()))) {
        std::printf("[x] 规则表加载失败\n");
        return 1;
    }
    const UnitModel* um = db.Resolve(unit_name);
    if (um == nullptr || !um->ok()) {
        std::printf("[x] %s 不是体素单位（或找不到）\n", unit_name);
        return 1;
    }

    std::vector<uint8_t> body, tur, barl;
    if (!read_any(um->body.id, &body)) {
        std::printf("[x] 读不到 %s\n", um->body.name.c_str());
        return 1;
    }
    VxlFile vxl;
    if (!vxl.Load(body.data(), body.size())) {
        std::printf("[x] %s 不是合法 VXL\n", um->body.name.c_str());
        return 1;
    }
    VxlFile vxl_tur, vxl_barl;
    VxlAttach att[2];
    int att_n = 0;
    if (um->turret_vxl.present && read_any(um->turret_vxl.id, &tur) &&
        vxl_tur.Load(tur.data(), tur.size())) {
        att[att_n].file = &vxl_tur;
        ++att_n;
    }
    if (um->barrel_vxl.present && read_any(um->barrel_vxl.id, &barl) &&
        vxl_barl.Load(barl.data(), barl.size())) {
        att[att_n].file = &vxl_barl;
        ++att_n;
    }

    VoxelLight light;
    if (light3 != nullptr) {
        light.light[0] = light3[0];
        light.light[1] = light3[1];
        light.light[2] = light3[2];
    }
    light.Normalize();

    std::vector<uint8_t> idx, shade;
    int w = 0, h = 0;
    if (!vxl.Render_Isometric(&idx, &w, &h, 8.0f, nullptr, att, att_n, &light, &shade)) {
        std::printf("[x] 渲染失败\n");
        return 1;
    }

    // ---- 硬判据自检：按世界法线的 z 分量分桶，看平均明暗级 ----
    // 判据：顶面(nz>0.7) 平均级 > 侧面(|nz|<0.3) > 底面(nz<-0.7)。
    double sum[3] = {0, 0, 0};
    long cnt[3] = {0, 0, 0};
    auto bucket = [](float nz) -> int {
        if (nz > 0.7f) return 0;
        if (nz < -0.7f) return 1;
        if (nz < 0.3f && nz > -0.3f) return 2;
        return -1;
    };
    std::vector<VxlVoxel> vox;
    VoxelShadeTable st;
    for (int l = 0; l < vxl.Limb_Count(); ++l) {
        const VxlLimbTailer& t = vxl.Tailer(l);
        vox.clear();
        if (!vxl.Decode_Limb(l, &vox)) {
            continue;
        }
        const float R[9] = {t.transform[0], t.transform[1], t.transform[2],
                            t.transform[4], t.transform[5], t.transform[6],
                            t.transform[8], t.transform[9], t.transform[10]};
        st.Build(t.normals_type, light, R);
        int count = 0;
        const float* tab = Voxel_Normal_Table(Normal_Slot_Of(t.normals_type), &count);
        if (tab == nullptr) {
            continue;
        }
        for (const VxlVoxel& v : vox) {
            if (v.normal >= count) {
                continue;
            }
            const float nx = tab[v.normal * 3 + 0];
            const float ny = tab[v.normal * 3 + 1];
            const float nz = tab[v.normal * 3 + 2];
            const float wz = R[6] * nx + R[7] * ny + R[8] * nz;
            const int b = bucket(wz);
            if (b < 0) {
                continue;
            }
            sum[b] += st.Level(v.normal);
            ++cnt[b];
        }
    }
    std::printf("单位 %s（美术名 %s）肢体 %d，炮塔/炮管附加层 %d\n",
                um->unit.c_str(), um->image.c_str(), vxl.Limb_Count(), att_n);
    std::printf("光向量 L = (%+.4f, %+.4f, %+.4f)  环境光 %.2f 漫反射 %.2f 分级 %d\n",
                light.light[0], light.light[1], light.light[2], light.ambient,
                light.diffuse, light.levels);
    const char* bn[3] = {"顶面", "底面", "侧面"};
    double mean[3] = {0, 0, 0};
    for (int b = 0; b < 3; ++b) {
        mean[b] = cnt[b] ? sum[b] / cnt[b] : -1.0;
        std::printf("  %s：%6ld 个体素，平均明暗级 %.2f / %d（亮度系数 %.3f）\n",
                    bn[b], cnt[b], mean[b], light.levels,
                    cnt[b] ? Shade_Factor(light, static_cast<int>(mean[b] + 0.5)) : 0.0);
    }
    int rc = 0;
    if (cnt[0] > 0 && cnt[1] > 0 && cnt[2] > 0) {
        const bool ok = (mean[0] > mean[2]) && (mean[2] > mean[1]);
        std::printf("  判据 顶面 > 侧面 > 底面：%s\n", ok ? "通过" : "**不通过**");
        if (!ok) rc = 1;
    } else {
        std::printf("  判据：样本不足，跳过\n");
    }

    // ---- 明暗级直方图 ----
    long hist[17] = {0};
    long opaque = 0;
    for (size_t i = 0; i < idx.size(); ++i) {
        if (idx[i] == 0) {
            continue;
        }
        ++opaque;
        const int lv = shade[i];
        hist[lv < 0 ? 0 : (lv > 16 ? 16 : lv)]++;
    }
    std::printf("  出图 %dx%d，不透明像素 %ld\n", w, h, opaque);
    std::printf("  明暗级直方图：");
    for (int i = 0; i <= 16; ++i) {
        if (hist[i]) {
            std::printf(" %d:%ld", i, hist[i]);
        }
    }
    std::printf("\n");

    if (out_path != nullptr) {
        std::vector<uint8_t> rgba;
        Shade_To_RGBA(idx.data(), shade.data(), static_cast<int>(idx.size()),
                      vxl.Palette(), light, &rgba);
        std::FILE* f = std::fopen(out_path, "wb");
        if (!f) {
            std::printf("[x] 写不了 %s\n", out_path);
            return 1;
        }
        // PPM（P6）：无依赖、Python 一行就能读，专门给离屏校验用。
        std::fprintf(f, "P6\n%d %d\n255\n", w, h);
        for (int i = 0; i < w * h; ++i) {
            std::fputc(rgba[i * 4 + 0], f);
            std::fputc(rgba[i * 4 + 1], f);
            std::fputc(rgba[i * 4 + 2], f);
        }
        std::fclose(f);
        std::printf("  已写出 %s\n", out_path);
    }
    return rc;
}

// ---------------------------------------------------------------------------
// 地图：MIX -> .map -> IsoMapPack5(分块 LZO) -> 剧场 TMP -> 等距画布
// ---------------------------------------------------------------------------
// 这是"把游戏跑起来"的地基：能铺出一张真实的战场，单位、寻路、光影才有地方放。
// 自检靠三条硬判据，不靠眼睛：
//   1. IsoMapPack5 解压后的字节数 == ((W*2-1)*H)*11 + 4
//   2. X+Y 为奇数的记录条数 == W*H（真单元数）
//   3. 用到的瓦片下标都能在剧场归档里找到 TMP，且确实画上了像素
static int Map_Dump(const std::vector<std::string>& mix_paths, const char* map_path,
                    const char* out_path) {
    MapFile map;
    std::string err;
    if (!map.Load_Path(map_path, &err)) {
        std::printf("[x] 地图加载失败：%s\n", err.c_str());
        return 1;
    }
    std::printf("地图 %s  剧场 %s  尺寸 %dx%d（可见 %d,%d %dx%d）\n",
                map.Name().c_str(), map.Theater().c_str(),
                map.Width(), map.Height(),
                map.Local_X(), map.Local_Y(), map.Local_Width(), map.Local_Height());
    // 判据：解压字节数 **不超过** ((W*2-1)*H)*11+4；补齐后单元数恰好 (2W-1)*H。
    const size_t cap = static_cast<size_t>((map.Width() * 2 - 1) * map.Height() * 11 + 4);
    std::printf("  IsoMapPack5 %zu -> %zu 字节（上限 %zu = ((%d*2-1)*%d)*11+4）%s\n",
                map.Packed_Bytes(), map.Unpacked_Bytes(), cap,
                map.Width(), map.Height(),
                map.Unpacked_Bytes() <= cap ? "✓" : " ✗");
    std::printf("  单元 %zu 个 = %dx%d %s；其中记录给出 %zu 个%s；最大瓦片下标 %d\n",
                map.Cells().size(), map.Iso_Width(), map.Height(),
                map.Cells().size() ==
                        static_cast<size_t>(map.Iso_Width()) * map.Height()
                    ? "✓"
                    : " ✗",
                map.Stored_Cells(),
                map.Stored_Cells() < map.Cells().size() ? "（裁剪包，缺的按 Clear01 补）"
                                                        : "",
                map.Max_Tile_Index());
    std::printf("  OverlayPack %zu 字节，OverlayDataPack %zu 字节\n",
                map.Overlay().size(), map.Overlay_Data().size());

    std::vector<MixFileClass> mixes(mix_paths.size());
    std::vector<MixFileClass*> roots;
    for (size_t i = 0; i < mix_paths.size(); ++i) {
        if (!mixes[i].Open(mix_paths[i].c_str())) {
            std::printf("[x] 打不开 %s\n", mix_paths[i].c_str());
            return 1;
        }
        roots.push_back(&mixes[i]);
    }

    MapRenderer mr;
    if (!mr.Bind(roots, map, &err)) {
        std::printf("[x] 素材绑定失败：%s\n", err.c_str());
        return 1;
    }
    std::printf("  剧场 %s：扩展名 .%s，调色板 %s，瓦片表 %d 项\n",
                mr.Theater().c_str(), mr.Extension().c_str(),
                mr.Palette_Name().c_str(), mr.Tile_Count());
    std::printf("  示例：下标 0 -> %s，下标 %d -> %s\n",
                mr.Tile_File_Name(0).c_str(), map.Max_Tile_Index(),
                mr.Tile_File_Name(map.Max_Tile_Index()).c_str());

    std::vector<uint32_t> canvas;
    int w = 0, h = 0;
    if (!mr.Render(map, 0, 0, 0, 0, &canvas, &w, &h, &err)) {
        std::printf("[x] 铺图失败：%s\n", err.c_str());
        return 1;
    }
    long opaque = 0;
    for (uint32_t p : canvas) {
        if ((p & 0xFF000000u) != 0) {
            ++opaque;
        }
    }
    std::printf("  画布 %dx%d，不透明像素 %ld（%.1f%%），瓦片命中 %d / 缺失 %d\n",
                w, h, opaque, 100.0 * opaque / double(w) * (1.0 / (h ? h : 1)),
                mr.Tiles_Loaded(), mr.Tiles_Missing());
    // "瓦片都在"不等于"画布填满了"：多格模板的 sub 取错时瓦片取到了、
    // 画上去却是空的，画布上会留下一排排菱形黑洞。所以单列一条判据。
    std::printf("  贴图格 %d，其中空白格 %d %s\n", mr.Cells_Drawn(), mr.Cells_Empty(),
                mr.Cells_Empty() == 0 ? "✓" : "✗（有格取到瓦片却画不出像素）");

    if (out_path != nullptr) {
        std::FILE* f = std::fopen(out_path, "wb");
        if (!f) {
            std::printf("[x] 写不了 %s\n", out_path);
            return 1;
        }
        std::fprintf(f, "P6\n%d %d\n255\n", w, h);
        for (uint32_t p : canvas) {
            std::fputc(static_cast<int>(p & 0xFF), f);
            std::fputc(static_cast<int>((p >> 8) & 0xFF), f);
            std::fputc(static_cast<int>((p >> 16) & 0xFF), f);
        }
        std::fclose(f);
        std::printf("  已写出 %s\n", out_path);
    }
    return (mr.Tiles_Missing() == 0) ? 0 : 1;
}

// ---------------------------------------------------------------------------
// --mapobj：地图里的对象（车辆/步兵/建筑/装饰）+ 路径点 + 阵营
//
// 自检判据（三条，逐张地图跑）：
//   1. 解析出的对象格必须落在 [0,W)x[0,H) —— 越界就说明坐标帧算错了。
//      坐标帧是拿全库 53 张官方地图反推出来的，见 MapFile.h 顶部。
//   2. 绝大多数对象要落在 LocalSize 可玩矩形里（实测 93.4%）。
//   3. 至少要有对象被解析出来，且 8 个出生点（waypoint 0..7）在地图上。
// ---------------------------------------------------------------------------
/// 路径里最后一段文件名，"D:/x/Arena.mmx" -> "Arena.mmx"。
static const char* Base_Name(const char* p) {
    const char* s1 = std::strrchr(p, '\\');
    const char* s2 = std::strrchr(p, '/');
    const char* s = (s1 && s2) ? (s1 > s2 ? s1 : s2) : (s1 ? s1 : s2);
    return s ? s + 1 : p;
}

static int Map_Objects(int count, char** paths) {
    int failures = 0;
    for (int i = 0; i < count; ++i) {
        MapFile map;
        std::string err;
        if (!map.Load_Path(paths[i], &err)) {
            std::printf("%-28s [x] 载入失败: %s\n", paths[i], err.c_str());
            ++failures;
            continue;
        }
        const int W = map.Width();
        const int H = map.Height();

        // 各类对象计数
        int n[5] = {0, 0, 0, 0, 0};
        for (const MapObject& o : map.Objects()) {
            n[static_cast<int>(o.kind)]++;
        }
        // 可玩矩形命中率
        int in_local = 0;
        for (const MapObject& o : map.Objects()) {
            if (o.cx >= map.Local_X() && o.cx < map.Local_X() + map.Local_Width() &&
                o.cy >= map.Local_Y() && o.cy < map.Local_Y() + map.Local_Height()) {
                ++in_local;
            }
        }
        const size_t total = map.Objects().size();
        const double pct = total ? 100.0 * static_cast<double>(in_local) /
                                       static_cast<double>(total) : 0.0;
        int spawn = 0;
        for (const MapWaypoint& w : map.Waypoints()) {
            if (w.index >= 0 && w.index < 8) ++spawn;
        }
        // 判据只有两条硬的：解析出对象、且几乎没有算出界的。
        // "落在可玩矩形内的比例"只作参考 —— 像 SinkSwim 这种小岛图，
        // 大片装饰树本来就在可玩区之外的海域里，比例天然低（实测 0.8%）。
        const bool ok = (total > 0) && (map.Objects_Dropped() <= 2);
        if (!ok) ++failures;
        std::printf("%-28s %3dx%-3d 对象%5zu (装饰%3d 车%3d 兵%3d 建筑%3d 机%2d) "
                    "可玩内%5.1f%% 越界%3d 路径点%3zu(出生%2d) 阵营%2zu  %s\n",
                    Base_Name(paths[i]), W, H, total, n[0], n[1], n[2], n[3], n[4],
                    pct, map.Objects_Dropped(), map.Waypoints().size(), spawn,
                    map.Houses().size(), ok ? "OK" : "FAIL");
    }
    std::printf(failures ? "[x] %d 张不过\n" : "[OK] %d 张全过\n", count - failures);
    return failures ? 1 : 0;
}

// ---------------------------------------------------------------------------
// --remap：阵营色（remap）表
// ---------------------------------------------------------------------------
static int Remap_Dump(const std::vector<std::string>& mix_paths,
                      const char* pal_name, const char* out_path) {
    MixFileSystem fs;
    for (const std::string& p : mix_paths) {
        if (!fs.Mount(p.c_str())) {
            std::printf("[x] 挂载失败 %s\n", p.c_str());
            return 1;
        }
    }

    // 1) [Colors] 从 rules.ini 来
    std::vector<uint8_t> ini = fs.Read_Deep("rules.ini");
    if (ini.empty()) ini = fs.Read_Deep("rulesmd.ini");
    if (ini.empty()) {
        std::printf("[x] 找不到 rules.ini / rulesmd.ini\n");
        return 1;
    }
    IniFile rules;
    if (!rules.Load(ini.data(), ini.size())) {
        std::printf("[x] rules 解析失败\n");
        return 1;
    }
    std::printf("rules.ini %zu 字节，%d 段，畸形行 %d\n",
                ini.size(), rules.Section_Count(), rules.Malformed_Lines());

    RemapTable remap;
    const int nc = remap.Load_From_Ini(rules);
    if (nc == 0) {
        std::printf("[x] [Colors] 段没解析出颜色\n");
        return 1;
    }
    // 判据 1：RA2/YR 的 [Colors] 是 19 条（17 个玩家色 + AlliedLoad + SovietLoad）
    std::printf("[Colors] %d 条 %s（RA2/YR 标准 19 条）\n", nc, nc >= 17 ? "✓" : " ✗");

    // 2) 调色板
    std::vector<uint8_t> pal = fs.Read_Deep(pal_name);
    if (pal.size() != 768) {
        std::printf("[x] 找不到 %s（读到 %zu 字节，要 768）\n", pal_name, pal.size());
        return 1;
    }
    // .PAL 是 6 位分量，VXL 内嵌的是 8 位。这里只处理 .PAL。
    std::printf("调色板 %s 768 字节（6 位分量）\n", pal_name);

    // 3) remap 区间：.PAL 没有自声明，用实测的 16..31（VXL 头也是这两个数）
    const int kStart = 16, kEnd = 31;

    // 打印占位色本身，证明它是"纯红渐变"
    std::printf("  占位色 %02X..%02X：" , kStart, kEnd);
    for (int i = kStart; i <= kEnd; ++i) {
        const int v = pal[i * 3];
        std::printf("%d ", (v << 2) | (v >> 4));
    }
    std::printf("\n");

    int bad = 0;
    std::vector<Palette> made;
    for (int c = 0; c < nc; ++c) {
        const RemapColor& rc = remap.Color(c);
        uint8_t tr = 0, tg = 0, tb = 0;
        RemapTable::Hsv_To_Rgb(rc.h, rc.s, rc.v, &tr, &tg, &tb);

        Palette p = remap.Make_Palette(pal.data(), false, kStart, kEnd, c);
        made.push_back(p);

        // 判据 2：16 级亮度严格递减（占位色是单调的，缩放后必然单调）
        double prev = 1e9;
        bool mono = true;
        std::string ramp;
        for (int i = kStart; i <= kEnd; ++i) {
            const Palette::Color& q = p.Colors()[i];
            const double l = 0.299 * q.r + 0.587 * q.g + 0.114 * q.b;
            if (l > prev + 0.5) mono = false;
            prev = l;
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%02x%02x%02x ", q.r, q.g, q.b);
            ramp += buf;
        }
        // 判据 3：最亮一级 = 目标色（缩放系数 1.0）
        const Palette::Color& top = p.Colors()[kStart];
        const bool top_ok = std::abs(top.r - tr) <= 1 && std::abs(top.g - tg) <= 1 &&
                            std::abs(top.b - tb) <= 1;
        if (!mono || !top_ok) ++bad;
        std::printf("  %-11s HSV(%3d,%3d,%3d) -> #%02x%02x%02x  %s%s\n",
                    rc.name.c_str(), rc.h, rc.s, rc.v, tr, tg, tb,
                    mono ? "" : "[亮度非单调!] ", top_ok ? "" : "[首级!=目标色!]");
        std::printf("      %s\n", ramp.c_str());
    }

    // 判据 4：区间外逐字节不变
    int outside_diff = 0;
    for (int c = 0; c < nc; ++c) {
        const Palette& p = made[c];
        for (int i = 0; i < 256; ++i) {
            if (i >= kStart && i <= kEnd) continue;
            const int v = pal[i * 3];
            const uint8_t r = static_cast<uint8_t>((v << 2) | (v >> 4));
            if (p.Colors()[i].r != r) ++outside_diff;
        }
    }
    std::printf("区间外 240 个颜色在 %d 张调色板上共 %d 处被改 %s\n",
                nc, outside_diff, outside_diff == 0 ? "✓" : " ✗");
    if (outside_diff != 0) ++bad;

    // 4) 输出色块图：每行一个阵营色，16 格由亮到暗
    if (out_path != nullptr) {
        const int cw = 24, ch = 20;
        const int w = 16 * cw;
        const int h = nc * ch;
        std::FILE* f = std::fopen(out_path, "wb");
        if (!f) {
            std::printf("[x] 写不了 %s\n", out_path);
            return 1;
        }
        std::fprintf(f, "P6\n%d %d\n255\n", w, h);
        for (int c = 0; c < nc; ++c) {
            for (int y = 0; y < ch; ++y) {
                for (int i = 0; i < 16; ++i) {
                    const Palette::Color& q = made[c].Colors()[kStart + i];
                    for (int x = 0; x < cw; ++x) {
                        std::fputc(q.r, f);
                        std::fputc(q.g, f);
                        std::fputc(q.b, f);
                    }
                }
            }
        }
        std::fclose(f);
        std::printf("  已写出色块图 %s（%dx%d，每行一个阵营色，16 级由亮到暗）\n",
                    out_path, w, h);
    }
    return bad == 0 ? 0 : 1;
}

/// 字段偏移的判据全部是"能算出来"的，不靠肉眼：
///
///  1. 每个类的字段末端必须落在它自己的 sizeof 之内（sizeof 来自
///     `push N; call operator new` 的配对，是**另一条**独立分析）；
///  2. 沿继承链，字段末端必须**严格递增** —— 派生类只会比基类字段更多；
///  3. TechnoClass 的 0xF0 / 0xF8 上必须有字段写入，因为 MSVC RTTI 的
///     基类位移表说 FlasherClass 在 240、StageClass 在 248。这是字段扫描
///     与 RTTI 两条完全独立的证据的交点，对不上就是有一边错了。
///
/// 3 条里任何一条不成立都返回非 0，冒烟测试挂掉。
static int Layout_Check() {
    int checked = 0;
    int entries = 0;
    for (int i = 0; i < re::kLayoutCount; ++i) {
        const re::ClassLayout& L = re::kLayouts[i];
        uint32_t worst = 0;
        for (int j = 0; j < L.count; ++j) {
            worst = std::max(worst, L.fields[j].off + L.fields[j].size);
        }
        entries += L.count;
        if (L.size == 0) {
            continue;
        }
        ++checked;
        if (worst > L.size) {
            std::printf("FAIL: %s 字段末端 0x%X 超出 sizeof %u\n",
                        L.name, worst, L.size);
            return 1;
        }
    }

    static const char* const kChain[] = {
        "AbstractClass", "ObjectClass", "MissionClass", "RadioClass",
        "TechnoClass", "FootClass", "UnitClass",
    };
    uint32_t prev = 0;
    uint32_t last = 0;
    for (const char* name : kChain) {
        const re::ClassLayout* L = re::LayoutOf(name);
        if (L == nullptr) {
            std::printf("FAIL: 布局表里没有 %s\n", name);
            return 1;
        }
        uint32_t worst = 0;
        for (int j = 0; j < L->count; ++j) {
            worst = std::max(worst, L->fields[j].off + L->fields[j].size);
        }
        if (worst <= prev) {
            std::printf("FAIL: 继承链末端没有递增 —— %s 末端 0x%X <= 前一个 0x%X\n",
                        name, worst, prev);
            return 1;
        }
        prev = worst;
        last = worst;
    }

    const re::ClassLayout* tc = re::LayoutOf("TechnoClass");
    if (tc == nullptr) {
        std::printf("FAIL: 布局表里没有 TechnoClass\n");
        return 1;
    }
    bool has_f0 = false;
    bool has_f8 = false;
    for (int j = 0; j < tc->count; ++j) {
        has_f0 = has_f0 || tc->fields[j].off == 0xF0;
        has_f8 = has_f8 || tc->fields[j].off == 0xF8;
    }
    if (!has_f0 || !has_f8) {
        std::printf("FAIL: TechnoClass 的 0xF0/0xF8 上没有字段，与 RTTI 的"
                    " FlasherClass/StageClass 基类位移矛盾\n");
        return 1;
    }

    std::printf("OK  字段偏移 %d 个类 / %d 个字段；%d 个有 sizeof 基准的全部落在界内；"
                "继承链末端严格递增（UnitClass 0x%X）\n",
                re::kLayoutCount, entries, checked, last);
    return 0;
}

/// `ra2core --layout [类名]`：把字段偏移表打印出来给人看。
static int Layout_Dump(const char* want) {
    if (want == nullptr) {
        std::printf("%-34s %8s %8s %6s\n", "类", "sizeof", "字段末端", "字段数");
        for (int i = 0; i < re::kLayoutCount; ++i) {
            const re::ClassLayout& L = re::kLayouts[i];
            uint32_t worst = 0;
            for (int j = 0; j < L.count; ++j) {
                worst = std::max(worst, L.fields[j].off + L.fields[j].size);
            }
            if (L.size != 0) {
                std::printf("%-34s %8u 0x%-6X %6d\n", L.name, L.size, worst,
                            L.count);
            } else {
                std::printf("%-34s %8s 0x%-6X %6d\n", L.name, "—", worst,
                            L.count);
            }
        }
        std::printf("\n（这里只含核心继承链；全量类与人读版说明见"
                    " db/fields.json 与 docs/fields.md）\n");
        return 0;
    }
    const re::ClassLayout* L = re::LayoutOf(want);
    if (L == nullptr) {
        std::printf("布局表里没有 %s（只含核心继承链，全量见 db/fields.json）\n", want);
        return 1;
    }
    std::printf("%s  sizeof=%s  字段 %d 个\n", L->name,
                L->size ? std::to_string(L->size).c_str() : "未知", L->count);
    for (int j = 0; j < L->count; ++j) {
        std::printf("  +0x%-5X 宽%d 写%-3u 读%-3u\n", L->fields[j].off,
                    L->fields[j].size, L->fields[j].written,
                    L->fields[j].read);
    }
    return 0;
}

/// 字段**名**的判据。名字来自各 TypeClass 的 `Read_INI`（`tools/fieldname.py`），
/// 全部是"能算出来"的，不靠肉眼：
///
///  1. **宽度必须与构造函数扫描出来的字段宽度一致**。两条通道完全无关：
///     名字来自 `Read_INI` 的"键名两侧同偏移"，宽度来自构造函数的
///     `mov [this+off], …`。同一个偏移上两边宽度对不上，就是有一边错了。
///  2. **偏移必须落在该类的 sizeof 之内**（sizeof 来自 `push N; call new`，第三条通道）。
///  3. 手工反汇编核对过的锚点：`ObjectTypeClass` 的 Armor@0x9C / Strength@0xA0，
///     `TechnoTypeClass` 的 Cost@0x610 / TechLevel@0x634 / Sight@0x5E8 / Points@0x728。
///     这些地址在 docs/fieldnames.md 里能逐条查到对应的指令流。
///  4. 反向对照：随便一个没被命名过的偏移必须查不到名字（防止表被写坏成通配）。
///
/// 任何一条不成立都返回非 0，冒烟测试挂掉。
static int FieldNames_Check() {
    int named = 0;
    int cross_width_ok = 0;
    int cross_width_bad = 0;
    int beyond_sizeof = 0;

    for (uint32_t i = 0; i < re::kFieldNameClassCount; ++i) {
        const re::ClassFieldNames& C = re::kFieldNames[i];
        uint32_t prev = 0;
        bool first = true;
        const uint32_t size = re::SizeOf(C.cls);

        for (uint32_t j = 0; j < C.count; ++j) {
            const re::FieldName& F = C.fields[j];
            ++named;

            if (F.key == nullptr || F.key[0] == '\0') {
                std::printf("FAIL: %s 偏移 0x%X 的名字是空的\n", C.cls, F.off);
                return 1;
            }
            if (F.width != 1 && F.width != 2 && F.width != 4 && F.width != 8) {
                std::printf("FAIL: %s 偏移 0x%X 宽度 %u 不是 1/2/4/8\n",
                            C.cls, F.off, F.width);
                return 1;
            }
            if (!first && F.off <= prev) {
                std::printf("FAIL: %s 的偏移没有严格递增 —— 0x%X 在 0x%X 之后\n",
                            C.cls, F.off, prev);
                return 1;
            }
            prev = F.off;
            first = false;

            // 判据 1：与构造函数扫描出来的字段宽度对拍
            const re::ClassLayout* L = re::LayoutOf(C.cls);
            if (L != nullptr) {
                for (int k = 0; k < L->count; ++k) {
                    if (L->fields[k].off != F.off) {
                        continue;
                    }
                    if (L->fields[k].size == F.width) {
                        ++cross_width_ok;
                    } else {
                        ++cross_width_bad;
                        std::printf("FAIL: %s 偏移 0x%X（%s）：Read_INI 说宽 %u，"
                                    "构造函数扫描说宽 %u\n",
                                    C.cls, F.off, F.key, F.width,
                                    L->fields[k].size);
                    }
                    break;
                }
            }

            // 判据 2：必须落在 sizeof 之内
            if (size != 0 && F.off >= size) {
                ++beyond_sizeof;
                std::printf("FAIL: %s 偏移 0x%X（%s）超出 sizeof %u\n",
                            C.cls, F.off, F.key, size);
            }
        }
    }
    if (cross_width_bad != 0 || beyond_sizeof != 0) {
        return 1;
    }

    // 判据 3：手工反汇编核对过的锚点
    struct Anchor { const char* cls; uint32_t off; const char* key; };
    static const Anchor kAnchors[] = {
        {"ObjectTypeClass", 0x9C, "ARMOR"},
        {"ObjectTypeClass", 0xA0, "STRENGTH"},
        {"TechnoTypeClass", 0x5E8, "SIGHT"},
        {"TechnoTypeClass", 0x610, "COST"},
        {"TechnoTypeClass", 0x634, "TECHLEVEL"},
        {"TechnoTypeClass", 0x728, "POINTS"},
    };
    for (const Anchor& a : kAnchors) {
        const char* got = re::FieldNameOf(a.cls, a.off);
        if (got == nullptr || std::strcmp(got, a.key) != 0) {
            std::printf("FAIL: %s 的 0x%X 应该是 %s，查到的却是 %s\n",
                        a.cls, a.off, a.key, got ? got : "(没有)");
            return 1;
        }
    }

    // 判据 4：反向对照 —— 没命名过的偏移查不到名字
    if (re::FieldNameOf("TechnoTypeClass", 0xFFFF0) != nullptr ||
        re::FieldNameOf("这个类不存在", 0x610) != nullptr) {
        std::printf("FAIL: 名字表把不存在的偏移/类也查出了名字\n");
        return 1;
    }

    std::printf("OK  字段名 %d 个类 / %d 条；其中 %d 条同时被构造函数扫描"
                "独立看到且宽度一致；sizeof 越界 0\n",
                re::kFieldNameClassCount, named, cross_width_ok);
    return 0;
}

/// 对象模型的闸门（src/re/ObjectModel.h，由 tools/layout.py 生成）。
///
/// FieldNames_Check 查的是"偏移有没有名字"；这里查的是"名字有没有真的落到
/// 结构体的那个偏移上" —— 把两张各自独立生成出来的表互相钉住，顺带钉住
/// 继承边界（父类字段在子类里的绝对偏移）。
static int Model_Check() {
    uint32_t members = 0;
    uint32_t named = 0;
    uint32_t cross = 0;

    // 判据 1：成员序列必须严丝合缝铺满 [start, end) —— 不留洞、不重叠、不越界
    for (uint32_t i = 0; i < re::model::kModelClassCount; ++i) {
        const re::model::ModelClass& C = re::model::kModelClasses[i];
        if (C.start >= C.end) {
            std::printf("FAIL: %s 的区间是空的（0x%X..0x%X）\n", C.name, C.start, C.end);
            return 1;
        }
        uint32_t at = C.start;
        uint32_t pads = 0;
        uint32_t flds = 0;
        uint32_t nm = 0;
        for (uint32_t j = 0; j < C.count; ++j) {
            const re::model::ModelMember& M =
                re::model::kModelMembers[C.first + j];
            if (M.off != at) {
                std::printf("FAIL: %s 第 %u 个成员落在 0x%X，应该落在 0x%X\n",
                            C.name, j, M.off, at);
                return 1;
            }
            if (M.size == 0) {
                std::printf("FAIL: %s 的成员 %s 宽度是 0\n", C.name, M.name);
                return 1;
            }
            if (M.kind <= 1) {
                if (M.kind == 0) {
                    ++flds;
                    if (M.key != nullptr) ++nm;
                } else {
                    pads += M.size;
                    if (M.key != nullptr) {
                        std::printf("FAIL: %s 的填充段 %s 竟然有 INI 键名\n",
                                    C.name, M.name);
                        return 1;
                    }
                }
            } else {
                std::printf("FAIL: %s 的成员 %s 有个不认识的 kind=%u\n",
                            C.name, M.name, M.kind);
                return 1;
            }
            at += M.size;
            ++members;
            if (M.key != nullptr) ++named;
        }
        if (at != C.end) {
            std::printf("FAIL: %s 的成员只铺到 0x%X，末端却是 0x%X\n",
                        C.name, at, C.end);
            return 1;
        }
        if (pads != C.pad_bytes || flds != C.fields || nm != C.named) {
            std::printf("FAIL: %s 的统计对不上：填充 %u/%u，字段 %u/%u，有名字 %u/%u\n",
                        C.name, pads, C.pad_bytes, flds, C.fields, nm, C.named);
            return 1;
        }
    }

    // 判据 2：FieldNames.h 里每条命名字段，都要能在模型里找到一条同偏移、同宽度、
    //         同键名的**字段**。字段可能不在本类本体里 —— 派生类的 Read_INI 也会去写
    //         继承来的字段（`UnitTypeClass::Read_INI` 就把 SPEEDTYPE 读进
    //         TechnoTypeClass 的 0x67C）。所以查不到本类就沿父类链往上找，
    //         并把这个"落在祖先上"的条数单独数出来（它是个有意义的量）。
    uint32_t via_base = 0;
    for (uint32_t i = 0; i < re::kFieldNameClassCount; ++i) {
        const re::ClassFieldNames& F = re::kFieldNames[i];
        const re::model::ModelClass* C = re::model::ModelOf(F.cls);
        if (C == nullptr) {
            std::printf("FAIL: 字段名表里有 %s，模型里却没有这个类\n", F.cls);
            return 1;
        }
        for (uint32_t j = 0; j < F.count; ++j) {
            const re::FieldName& N = F.fields[j];
            const re::model::ModelMember* M = nullptr;
            const re::model::ModelClass* owner = C;
            for (const re::model::ModelClass* K = C; K != nullptr;) {
                M = re::model::ModelMemberAt(*K, N.off);
                if (M != nullptr) {
                    owner = K;
                    break;
                }
                K = K->parent ? re::model::ModelOf(K->parent) : nullptr;
            }
            if (M == nullptr) {
                std::printf("FAIL: %s 的命名字段 0x%X（%s）在模型里（含继承链）"
                            "找不到成员\n", F.cls, N.off, N.key);
                return 1;
            }
            if (M->kind != 0 || M->key == nullptr ||
                std::strcmp(M->key, N.key) != 0 || M->size != N.width) {
                std::printf("FAIL: %s 的 0x%X 在模型里是 %s（键 %s，宽 %u），"
                            "字段名表说应该是 %s（宽 %u）\n",
                            F.cls, N.off, M->name,
                            M->key ? M->key : "(无)",
                            M->size, N.key, N.width);
                return 1;
            }
            if (owner != C) ++via_base;
            ++cross;
        }
    }

    // 判据 3：继承边界。父类的字段在子类里必须落在同一个绝对偏移上 ——
    // 这只有"父类末端恰好等于子类本体起点"时才成立，所以它同时钉住了继承边界。
    // 用 offsetof 而不是查表：查表是查自己的数据，offsetof 问的是编译器。
    static_assert(sizeof(re::model::ObjectTypeClass) == 0x294,
                  "ObjectTypeClass 的末端变了，继承边界跟着变");
    if (offsetof(re::model::TechnoTypeClass, ARMOR) != 0x9C ||
        offsetof(re::model::TechnoTypeClass, ARMOR) !=
            offsetof(re::model::ObjectTypeClass, ARMOR) ||
        offsetof(re::model::TechnoTypeClass, STRENGTH) != 0xA0 ||
        offsetof(re::model::TechnoTypeClass, COST) != 0x610 ||
        offsetof(re::model::TechnoTypeClass, TECHLEVEL) != 0x634 ||
        offsetof(re::model::TechnoTypeClass, SIGHT) != 0x5E8 ||
        offsetof(re::model::TechnoTypeClass, POINTS) != 0x728) {
        std::printf("FAIL: 继承链没把父类字段带到正确的绝对偏移"
                    "（ARMOR 在 TechnoTypeClass 里落到 0x%llX，"
                    "在 ObjectTypeClass 里是 0x%llX，应该是 0x9C）\n",
                    static_cast<unsigned long long>(
                        offsetof(re::model::TechnoTypeClass, ARMOR)),
                    static_cast<unsigned long long>(
                        offsetof(re::model::ObjectTypeClass, ARMOR)));
        return 1;
    }

    // 判据 4：父类末端必须严格小于子类末端（继承链方向没搞反）
    for (uint32_t i = 0; i < re::model::kModelClassCount; ++i) {
        const re::model::ModelClass& C = re::model::kModelClasses[i];
        if (C.parent == nullptr) continue;
        const re::model::ModelClass* P = re::model::ModelOf(C.parent);
        if (P == nullptr || P->end != C.start || P->end >= C.end) {
            std::printf("FAIL: %s 的父类 %s 对不上（父末端 0x%X，本体起点 0x%X）\n",
                        C.name, C.parent, P ? P->end : 0, C.start);
            return 1;
        }
        if (C.end > P->end + 0x10000) {
            std::printf("FAIL: %s 的末端 0x%X 离父类 0x%X 太远，像是填错了\n",
                        C.name, C.end, P->end);
            return 1;
        }
    }

    // 判据 5：反向 —— 不存在的类/键不该查得到东西
    if (re::model::ModelOf("这个类不存在") != nullptr ||
        re::model::ModelOf("TechnoTypeClass") == nullptr) {
        std::printf("FAIL: 模型按类名查表的结果不对\n");
        return 1;
    }
    const re::model::ModelClass* tt = re::model::ModelOf("TechnoTypeClass");
    if (re::model::ModelMemberOfKey(*tt, "COST") == nullptr ||
        re::model::ModelMemberOfKey(*tt, "COST")->off != 0x610 ||
        re::model::ModelMemberOfKey(*tt, "这个键不存在") != nullptr ||
        re::model::ModelMemberAt(*tt, 0xFFFF0) != nullptr) {
        std::printf("FAIL: 模型按键名/偏移查成员的结果不对\n");
        return 1;
    }

    std::printf("OK  对象模型 %u 个类 / %u 条成员（%u 条有 INI 键名）："
                "成员严丝合缝铺满每个类的区间；与字段名表交叉核对 %u 条全对齐"
                "（其中 %u 条落在继承来的字段上）；继承边界由 offsetof 实测钉住\n",
                re::model::kModelClassCount, members, named, cross, via_base);
    return 0;
}

/// `ra2core --model [类名]`：打印对象模型的成员序列。
static int Model_Dump(const char* want) {
    if (want == nullptr) {
        std::printf("%-24s %-22s %7s %7s %6s %6s %9s %8s\n",
                    "类", "父类", "本体起点", "末端", "字段", "有名", "填充字节", "覆盖");
        for (uint32_t i = 0; i < re::model::kModelClassCount; ++i) {
            const re::model::ModelClass& C = re::model::kModelClasses[i];
            const uint32_t span = C.end - C.start;
            std::printf("%-24s %-22s 0x%-5X 0x%-5X %6u %6u %9u %7.1f%%\n",
                        C.name, C.parent ? C.parent : "(根)", C.start, C.end,
                        C.fields, C.named, C.pad_bytes,
                        span ? 100.0 * (span - C.pad_bytes) / span : 0.0);
        }
        std::printf("\n（末端 = 最后一个有证据的字段的末端，是 sizeof 的**下界**；"
                    "覆盖 = 有证据的字节 / 本体字节。只含构造函数与 Read_INI "
                    "两条通道的证据，不等于「这里什么都没有」。"
                    "见 docs/object-model.md）\n");
        return 0;
    }
    const re::model::ModelClass* C = re::model::ModelOf(want);
    if (C == nullptr) {
        std::printf("模型里没有 %s（全量见 db/layout.json）\n", want);
        return 1;
    }
    std::printf("struct %s%s   // 0x%X .. 0x%X（本体 %u 字节）\n",
                C->name, C->parent ? "" : "", C->start, C->end, C->end - C->start);
    for (uint32_t j = 0; j < C->count; ++j) {
        const re::model::ModelMember& M = re::model::kModelMembers[C->first + j];
        if (M.kind == 1) {
            std::printf("  +0x%-5X %-30s u8[%u]    —— 没有证据\n",
                        M.off, M.name, M.size);
            continue;
        }
        std::printf("  +0x%-5X %-30s %-4s     %s\n",
                    M.off, M.name,
                    M.size == 1 ? "u8" : (M.size == 2 ? "u16" : "u32"),
                    M.key ? M.key : "(没名字：只有构造函数写过的证据)");
    }
    return 0;
}

/// `ra2core --fieldnames [类名]`：把 (偏移, INI 键名) 打出来给人看。
static int FieldNames_Dump(const char* want) {    if (want == nullptr) {
        std::printf("%-34s %8s\n", "类", "命名字段");
        int total = 0;
        for (uint32_t i = 0; i < re::kFieldNameClassCount; ++i) {
            std::printf("%-34s %8u\n", re::kFieldNames[i].cls,
                        re::kFieldNames[i].count);
            total += static_cast<int>(re::kFieldNames[i].count);
        }
        std::printf("%-34s %8d\n", "合计", total);
        std::printf("\n（含「双向」条目，以及没被人争过的单向条目；"
                    "单向且有争议的不进本表。取舍规则与来争的键名见"
                    " docs/fieldnames.md，全量见 db/fieldnames.json）\n");
        return 0;
    }
    for (uint32_t i = 0; i < re::kFieldNameClassCount; ++i) {
        const re::ClassFieldNames& C = re::kFieldNames[i];
        if (std::strcmp(C.cls, want) != 0) {
            continue;
        }
        std::printf("%s  命名字段 %u 个\n", C.cls, C.count);
        for (uint32_t j = 0; j < C.count; ++j) {
            std::printf("  +0x%-5X %-30s 宽%u  %s\n", C.fields[j].off,
                        C.fields[j].key, C.fields[j].width, C.fields[j].type);
        }
        return 0;
    }
    std::printf("名字表里没有 %s（全量见 db/fieldnames.json）\n", want);
    return 1;
}

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--vxlit") == 0) {
        if (argc < 3) {
            std::printf("用法：ra2core --vxlit <mix> [更多mix...] "
                        "[--unit NAME] [--out out.ppm] [--light x,y,z]\n");
            std::printf("  例：ra2core --vxlit D:/westwood/RA2YR/ra2.mix"
                        " D:/westwood/RA2YR/ra2md.mix --unit MTNK --out mtnk.ppm\n");
            return 1;
        }
        std::vector<std::string> mixes;
        const char* unit = "MTNK";
        const char* out = nullptr;
        float light3[3] = {0, 0, 0};
        bool has_light = false;
        for (int i = 2; i < argc; ++i) {
            if (argv[i][0] == '-' && argv[i][1] == '-') {
                if (std::strcmp(argv[i], "--unit") == 0 && i + 1 < argc) {
                    unit = argv[++i];
                } else if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
                    out = argv[++i];
                } else if (std::strcmp(argv[i], "--light") == 0 && i + 1 < argc) {
                    const char* s = argv[++i];
                    light3[0] = static_cast<float>(std::atof(s));
                    const char* c1 = std::strchr(s, ',');
                    if (c1) light3[1] = static_cast<float>(std::atof(c1 + 1));
                    const char* c2 = c1 ? std::strchr(c1 + 1, ',') : nullptr;
                    if (c2) light3[2] = static_cast<float>(std::atof(c2 + 1));
                    has_light = true;
                }
                continue;
            }
            mixes.push_back(argv[i]);
        }
        if (mixes.empty()) {
            std::printf("[x] 至少要给一个 .mix\n");
            return 1;
        }
        return Vxl_Light(mixes, unit, out, has_light ? light3 : nullptr);
    }
    if (argc > 1 && std::strcmp(argv[1], "--map") == 0) {
        if (argc < 4) {
            std::printf("用法：ra2core --map <地图.mmx/.yro/.map> <顶层mix> [更多mix...] "
                        "[--out out.ppm]\n");
            std::printf("  例：ra2core --map D:/westwood/RA2YR/Arena.mmx "
                        "D:/westwood/RA2YR/ra2.mix D:/westwood/RA2YR/ra2md.mix "
                        "--out arena.ppm\n");
            return 1;
        }
        std::vector<std::string> mixes;
        const char* out = nullptr;
        for (int i = 3; i < argc; ++i) {
            if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
                out = argv[++i];
                continue;
            }
            mixes.push_back(argv[i]);
        }
        if (mixes.empty()) {
            std::printf("[x] 至少要给一个 .mix\n");
            return 1;
        }
        return Map_Dump(mixes, argv[2], out);
    }
    if (argc > 1 && std::strcmp(argv[1], "--mapobj") == 0) {
        if (argc < 3) {
            std::printf("用法：ra2core --mapobj <地图.mmx/.yro/.map> [更多地图...]\n");
            return 1;
        }
        return Map_Objects(argc - 2, argv + 2);
    }
    if (argc > 1 && std::strcmp(argv[1], "--remap") == 0) {
        if (argc < 3) {
            std::printf("用法：ra2core --remap <mix> [更多mix...] [--pal 调色板] [--out out.ppm]\n");
            std::printf("  例：ra2core --remap D:/westwood/RA2YR/ra2.mix"
                        " D:/westwood/RA2YR/ra2md.mix --pal unittem.pal --out remap.ppm\n");
            return 1;
        }
        std::vector<std::string> mixes;
        const char* pal = "unittem.pal";
        const char* out = nullptr;
        for (int i = 2; i < argc; ++i) {
            if (std::strcmp(argv[i], "--pal") == 0 && i + 1 < argc) {
                pal = argv[++i];
            } else if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
                out = argv[++i];
            } else {
                mixes.push_back(argv[i]);
            }
        }
        return Remap_Dump(mixes, pal, out);
    }

    if (argc > 1 && std::strcmp(argv[1], "--ini") == 0) {
        if (argc < 4) {
            std::printf("用法：ra2core --ini <顶层mix> <0xINI的CRC>\n");
            return 1;
        }
        return Verify_Ini(argv[2],
                          static_cast<uint32_t>(std::strtoul(argv[3], nullptr, 16)));
    }
    if (argc > 1 && std::strcmp(argv[1], "--detect") == 0) {
        if (argc < 3) {
            std::printf("用法：ra2core --detect <游戏目录>\n");
            std::printf("  例：ra2core --detect D:/westwood/RA2YR\n");
            return 1;
        }
        return Detect_Game(argv[2]);
    }
    if (argc > 1 && std::strcmp(argv[1], "--layout") == 0) {
        return Layout_Dump(argc > 2 ? argv[2] : nullptr);
    }
    if (argc > 1 && std::strcmp(argv[1], "--fieldnames") == 0) {
        return FieldNames_Dump(argc > 2 ? argv[2] : nullptr);
    }
    if (argc > 1 && std::strcmp(argv[1], "--model") == 0) {
        return Model_Dump(argc > 2 ? argv[2] : nullptr);
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
    if (argc > 1 && std::strcmp(argv[1], "--hvahash") == 0) {
        if (argc < 3) {
            std::printf("用法：ra2core --hvahash <顶层mix> [最大嵌套深度]\n");
            return 1;
        }
        const int d = (argc > 3) ? std::atoi(argv[3]) : 4;
        return Hash_Hva(argv[2], d);
    }
    if (argc > 1 && std::strcmp(argv[1], "--hva") == 0) {
        if (argc < 4) {
            std::printf("用法：ra2core --hva <顶层mix> <0xHVA的CRC>\n");
            return 1;
        }
        return Verify_Hva(argv[2],
                          static_cast<uint32_t>(std::strtoul(argv[3], nullptr, 16)));
    }
    if (argc > 1 && std::strcmp(argv[1], "--unitdb") == 0) {
        if (argc < 3) {
            std::printf("用法：ra2core --unitdb <顶层mix> [更多mix...] [--dump <out.txt>]\n");
            std::printf("  例：ra2core --unitdb D:/westwood/RA2YR/ra2.mix"
                        " D:/westwood/RA2YR/ra2md.mix\n");
            return 1;
        }
        // 位置参数收到第一个 -- 开头的为止；后面的 --dump 单独认。
        std::vector<std::string> mixes;
        const char* dump = nullptr;
        for (int i = 2; i < argc; ++i) {
            if (argv[i][0] == '-' && argv[i][1] == '-') {
                if (std::strcmp(argv[i], "--dump") == 0 && i + 1 < argc) {
                    dump = argv[i + 1];
                    ++i;
                }
                continue;
            }
            mixes.push_back(argv[i]);
        }
        if (mixes.empty()) {
            std::printf("[x] 至少要给一个 .mix\n");
            return 1;
        }
        return Unit_DB(mixes, dump);
    }
    if (argc > 1 && std::strcmp(argv[1], "--typetable") == 0) {
        if (argc < 3) {
            std::printf("用法：ra2core --typetable <顶层mix> [更多mix...]"
                        " [--raw <out.txt>] [--summary <out.txt>] [--top <N>]\n");
            std::printf("  例：ra2core --typetable D:/RA2/\"Reunion 2023\"/ra2.mix"
                        " D:/RA2/\"Reunion 2023\"/ra2md.mix ...\n");
            std::printf("  把 rules/art/sound/theme 全量装进类型表，并逐键对账\n");
            return 1;
        }
        std::vector<std::string> mixes;
        const char* raw = nullptr;
        const char* summary = nullptr;
        int top = 20;
        for (int i = 2; i < argc; ++i) {
            if (argv[i][0] == '-' && argv[i][1] == '-') {
                if (std::strcmp(argv[i], "--raw") == 0 && i + 1 < argc) {
                    raw = argv[++i];
                } else if (std::strcmp(argv[i], "--summary") == 0 && i + 1 < argc) {
                    summary = argv[++i];
                } else if (std::strcmp(argv[i], "--top") == 0 && i + 1 < argc) {
                    top = std::atoi(argv[++i]);
                }
                continue;
            }
            mixes.push_back(argv[i]);
        }
        if (mixes.empty()) {
            std::printf("[x] 至少要给一个 .mix\n");
            return 1;
        }
        return Type_Table(mixes, raw, summary, top);
    }
    if (argc > 1 && std::strcmp(argv[1], "--vxlhash") == 0) {
        if (argc < 3) {
            std::printf("用法：ra2core --vxlhash <顶层mix> [最大嵌套深度]\n");
            return 1;
        }
        const int d = (argc > 3) ? std::atoi(argv[3]) : 4;
        return Hash_Vxl(argv[2], d);
    }
    if (argc > 1 && std::strcmp(argv[1], "--vxl") == 0) {
        if (argc < 4) {
            std::printf("用法：ra2core --vxl <顶层mix> <0xVXL的CRC>\n");
            return 1;
        }
        return Verify_Vxl(argv[2],
                          static_cast<uint32_t>(std::strtoul(argv[3], nullptr, 16)));
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

    // ---- 字段偏移 ----
    // src/re/FieldOffsets.h 由 tools/fieldscan.py 扫 gamemd.exe 的构造函数得出。
    // 没有字段偏移，还原出的结构体就只是空壳，P3 逻辑层无从下手。
    if (Layout_Check() != 0) {
        return 1;
    }

    // ---- 字段名 ----
    // src/re/FieldNames.h 由 tools/fieldname.py 扫 gamemd.exe 的 Read_INI 得出。
    // 偏移知道"有个字段"，名字才知道"这个字段是哪个 INI 键"。
    if (FieldNames_Check() != 0) {
        return 1;
    }

    // ---- 对象模型 ----
    // src/re/ObjectModel.h 由 tools/layout.py 把「偏移表 + 名字表 + RTTI 继承」
    // 铺成能编译的结构体。这一步查的是名字有没有真的落到那个偏移上。
    if (Model_Check() != 0) {
        return 1;
    }

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
