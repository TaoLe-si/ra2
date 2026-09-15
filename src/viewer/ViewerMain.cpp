// ViewerMain.cpp -- RA2 素材查看器（Win32 + DX12）
//
// 全量还原的第一个"看得见"的里程碑：
//   加密 MIX -> SHP(TS) 解码 -> 调色板 -> DX12 上屏
//
// 用法：
//   ra2view.exe <mix路径> [SHP名] [PAL名]
//   不带名字时自动挑归档里最大的那个 SHP，调色板用帧表 FrameColor 自动匹配。
//
// 体素模式：
//   ra2view.exe <mix> --vxl 0xVXLID [--hva 0xHVAID] [--hvaframeN]
//                     [--turret 0xTURID] [--barrel 0xBARLID] [--offscreen]
//   RA2 把坦克拆成三个共享同一模型空间原点的文件，炮塔/炮管要单独给：
//     ra2view.exe D:/westwood/RA2YR/ra2.mix --vxl 0xAE458B95 \
//         --turret 0xFDC7E10F --barrel 0x5BA86B7E --turretyaw 35
//
// 单位模式（P2 数据层）：给单位名，剩下的自己从 INI 推出来
//   ra2view.exe <mix> --unit MTNK [--addmix <另一个mix>] [--offscreen]
//   rules 在 ra2.mix 里、rulesmd 在 ra2md.mix 里，所以看 YR 单位要挂两个：
//     ra2view.exe D:/westwood/RA2YR/ra2.mix --addmix D:/westwood/RA2YR/ra2md.mix \
//         --unit YTNK --turretyaw 40 --barrelpitch 25 --offscreen

// 操作： 空格/点击 = 下一帧；←/→ = HVA 帧；A/D = 转炮塔；
//        W/S = 抬炮口；ESC = 退出；滚轮/+- = 缩放。

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "gfx/HvaFile.h"
#include "gfx/Palette.h"
#include "gfx/RemapTable.h"
#include "gfx/ShpFile.h"
#include "gfx/TmpFile.h"
#include "gfx/VxlFile.h"
#include "gfx/VoxelLight.h"
#include "data/UnitModel.h"
#include "gfx/dx12/Dx12Renderer.h"
#include "io/FileSystem.h"
#include "map/MapFile.h"
#include "map/MapRenderer.h"
#include "map/TheaterFile.h"

using namespace ra2;

namespace {

constexpr int kWinW = 1024;
constexpr int kWinH = 768;

struct App {
    MixFileClass mix;
    /// --addmix：第二个归档。rules 在 ra2.mix、rulesmd 在 ra2md.mix，
    /// 想按单位名查 YR 的单位就得挂两个。
    MixFileClass mix2;
    bool has_mix2 = false;
    ShpFile shp;
    std::string shp_name;
    std::string pal_name;
    int frame = 0;
    float scale = 1.0f;
    int sprite = -1;

    /// TMP 地形模式：合成好的索引图（一次上传，不逐帧改）。
    std::vector<uint8_t> terrain;
    int terrain_w = 0;
    int terrain_h = 0;

    /// 真实地图模式（--map）：MIX -> .map -> IsoMapPack5(分块 LZO) -> 剧场 TMP
    /// -> 一整张等距战场。铺好一次就固定，之后只做平移缩放。
    std::vector<uint32_t> map_rgba;
    int map_w = 0;
    int map_h = 0;
    bool map_mode = false;
    float pan_x = 0.0f, pan_y = 0.0f;
    bool fit_map = true;          ///< 开局缩放到刚好装得下整张图

    /// VXL 体素模式：软件等距光栅化出来的索引图，形状和 TMP 那条路一样，
    /// 所以能直接复用同一个 R8 + 256×1 查表的精灵管线。
    std::vector<uint8_t> vxl_idx;
    std::vector<uint8_t> vxl_shade;   ///< 每个像素的明暗级（0..16），配 vxl_idx 用
    std::vector<uint8_t> vxl_rgba;    ///< 打光后烘出来的真彩图
    VoxelLight light;                 ///< 光照参数（默认值 = gamemd.exe 实测）
    bool light_on = true;             ///< L 键切换：关掉就是"原始色号"，方便对比
    int vxl_w = 0;
    int vxl_h = 0;
    VxlFile vxl;
    HvaFile hva;
    uint32_t hva_id = 0;
    bool hva_linked = false;
    int hva_frame = 0;
    std::vector<float> vxl_pose;   ///< 当前帧的姿势：Limb_Count() × 12 个 float

    /// 炮塔 / 炮管：RA2 里是独立 VXL（<名>TUR.VXL / <名>BARL.VXL），
    /// 与车体共享同一模型空间原点，所以只要给它们一个绕 Z 的偏航就能叠上去。
    VxlFile turret;
    VxlFile barrel;
    bool has_turret = false;
    bool has_barrel = false;
    float turret_yaw = 0.0f;       ///< 弧度，绕 Z（模型空间 X 向前、Y 横向、Z 向上）
    float barrel_pitch = 0.0f;     ///< 弧度，绕 Y（炮口抬起）

    /// 地面投影阴影。方位复用 VoxelLight 的光向量（同一个太阳）。
    VoxelShadow shadow;
    std::vector<uint8_t> vxl_shadow;   ///< w×h 的 0/255 掩膜

    /// 阵营色（remap）。
    ///
    /// 素材里要显示成阵营色的像素统一用调色板 16..31 这 16 个"占位色"
    /// （unittem.pal 里就是一组纯红渐变），游戏按所属阵营把这段整段换掉，
    /// 一份素材渲出 8 个阵营。颜色表来自 rules.ini 的 [Colors]。
    /// VXL 自己在头里声明了区间（Remap_Start/End，实测 16/31），
    /// 所以直接用素材报的，不写死。
    RemapTable remap_table;
    int remap_index = -1;     ///< [Colors] 下标；-1 = 不 remap
    std::vector<uint8_t> remap_pal;   ///< remap 后的 768 字节，供打光管线用

    Dx12Renderer r;
    bool ok = false;
};

App g_app;
Palette g_app_frame_palette;   ///< 当前帧配的调色板

/// 按 ID 取内容，主归档找不到再去副归档找。
std::vector<uint8_t> Read_Any(uint32_t id) {
    std::vector<uint8_t> d = g_app.mix.Read_Deep_By_ID(id);
    if (d.empty() && g_app.has_mix2) {
        d = g_app.mix2.Read_Deep_By_ID(id);
    }
    return d;
}

/// 把一个已打开的 MIX（含子 MIX）里所有 768 字节条目读成候选调色板。
std::vector<Palette> Collect_Palettes(MixFileClass& m, int depth = 0) {
    std::vector<Palette> out;
    if (depth > 3) {
        return out;
    }
    auto entries = m.Entries();          // 拷贝一份：Open_Sub 期间不改动 entries_
    for (const MixEntry& e : entries) {
        if (e.size == 768) {
            std::vector<uint8_t> d = m.Read_Entry(e);
            Palette p;
            if (p.Load(d.data(), d.size())) {
                out.push_back(p);
            }
        } else {
            auto sub = m.Open_Sub(e);
            if (sub) {
                std::vector<Palette> inner = Collect_Palettes(*sub, depth + 1);
                out.insert(out.end(), inner.begin(), inner.end());
            }
        }
    }
    return out;
}

bool Load_Asset(MixFileClass& mix, const char* shp_name, const char* pal_name,
                ShpFile* out_shp, std::string* out_pal, int* out_best) {
    std::vector<uint8_t> shp_data;
    if (shp_name && *shp_name) {
        shp_data = mix.Read_Deep(shp_name);
        if (shp_data.empty()) {
            std::printf("[x] 找不到 SHP: %s (crc=0x%08X)\n", shp_name,
                        MixFileClass::CRC_Of(shp_name));
            if (const MixEntry* e = mix.Find(shp_name)) {
                std::printf("    顶层索引里有 off=%u size=%u，直接读 -> %zu 字节\n",
                            e->offset, e->size, mix.Read_Entry(*e).size());
            } else {
                std::printf("    顶层索引里也没有\n");
            }
            return false;
        }
    } else {
        // 自动挑：最大的那个 SHP（通常就是最能看的整屏图）
        struct Cand { const MixEntry* e; };
        std::vector<uint8_t> best;
        std::vector<const MixEntry*> pool;
        for (const MixEntry& e : mix.Entries()) {
            pool.push_back(&e);
        }
        for (const MixEntry* e : pool) {
            if (e->size < 4096) {
                continue;
            }
            std::vector<uint8_t> head = mix.Read_Entry(*e);
            if (head.size() < 8 || head[0] != 0 || head[1] != 0) {
                continue;
            }
            if (head.size() > best.size()) {
                best = std::move(head);
            }
        }
        // 子 MIX 里也找一遍
        for (const MixEntry* e : pool) {
            auto sub = mix.Open_Sub(*e);
            if (!sub) {
                continue;
            }
            for (const MixEntry& se : sub->Entries()) {
                if (se.size <= best.size()) {
                    continue;
                }
                std::vector<uint8_t> d = sub->Read_Entry(se);
                if (d.size() >= 8 && d[0] == 0 && d[1] == 0) {
                    best = std::move(d);
                }
            }
        }
        if (best.empty()) {
            std::printf("[x] 归档里没找到 SHP\n");
            return false;
        }
        shp_data = std::move(best);
    }

    if (!out_shp->Load(shp_data.data(), shp_data.size())) {
        std::printf("[x] SHP 解析失败\n");
        return false;
    }

    // 调色板
    Palette pal;
    if (pal_name && *pal_name) {
        std::vector<uint8_t> pd = mix.Read_Deep(pal_name);
        if (pd.size() != 768 || !pal.Load(pd.data(), pd.size())) {
            std::printf("[x] 调色板加载失败: %s\n", pal_name);
            return false;
        }
        *out_pal = pal_name;
        *out_best = -1;
    } else {
        std::vector<Palette> cands = Collect_Palettes(mix);
        const ShpFrameInfo& f0 = out_shp->Frame_Info(0);
        double err = 0;
        int best_i = ShpFile::Pick_Palette(f0, out_shp->Frame_Pixels(0), cands, &err);
        if (best_i < 0) {
            std::printf("[x] 没有可用调色板\n");
            return false;
        }
        pal = cands[best_i];
        char buf[64];
        std::snprintf(buf, sizeof(buf), "自动匹配(色差 %.1f)", err);
        *out_pal = buf;
        *out_best = best_i;
    }
    g_app_frame_palette = pal;
    return true;
}

// ---------------------------------------------------------------------------
// TMP 等距地形合成
// ---------------------------------------------------------------------------
//
// 游戏里地形是一格一格画的：地块 (cx, cy) 的屏幕位置是
//     x = (cx - cy) * cw/2
//     y = (cx + cy) * ch/2
// 也就是往右下和左下各走半个格子 —— 这就是"等距"的全部含义。
// 绘制顺序必须是 (cx+cy) 递增（由远及近），否则前格的下半边会被后格盖掉。
//
// 这里合成的是**索引图**而不是 RGBA：整张图共用一个调色板，
// 索引 0 就是透明，直接拷非零索引即可，省掉一次 256 色展开。

struct TerrainTileSet {
    std::vector<TmpFile> tiles;
    int cell_w = 60;
    int cell_h = 30;
};

/// 从已打开的归档里挑出所有能当地砖的模板（1x1 且首 cell 非空）。
bool Load_Terrain_Tiles(MixFileClass& sub, TerrainTileSet* out) {
    for (const MixEntry& e : sub.Entries()) {
        std::vector<uint8_t> d = sub.Read_Entry(e);
        TmpFile t;
        if (!t.Load(d.data(), d.size())) {
            continue;
        }
        if (!t.Tiles()[0].present || t.Block_Width() != 1 || t.Block_Height() != 1) {
            continue;
        }
        out->cell_w = t.Cell_Width();
        out->cell_h = t.Cell_Height();
        out->tiles.push_back(std::move(t));
    }
    return !out->tiles.empty();
}

/// 把 N×N 个地块拼成一张索引图。返回 false 表示没东西可画。
bool Build_Terrain_Image(const TerrainTileSet& set, int n, bool random_pick,
                         std::vector<uint8_t>* out, int* out_w, int* out_h) {
    if (set.tiles.empty() || n <= 0) {
        return false;
    }
    const int cw = set.cell_w;
    const int ch = set.cell_h;
    // 原点要留出整整"半边斜带"：(cx-cy) 最小是 -(n-1)，不补这么多的话
    // 左下角那一半地块会被裁到画布外面（实测第一版就是右边一片空白、
    // 左边缺一块）。y 方向再各留 2 格给 extra 的负坐标（树冠探到上一格）。
    const int margin_x = (n - 1) * cw / 2;
    const int margin_y = ch * 2;
    const int W = n * cw;
    const int H = n * ch + margin_y * 2;
    std::vector<uint8_t> img(static_cast<size_t>(W) * H, 0);
    const auto rows = TmpFile::Row_Geometry(cw, ch);

    int drawn = 0;
    // 由远及近：(cx+cy) 小的先画。
    for (int sum = 0; sum <= 2 * (n - 1); ++sum) {
        for (int cx = 0; cx < n; ++cx) {
            const int cy = sum - cx;
            if (cy < 0 || cy >= n) {
                continue;
            }
            // 选哪块砖。默认按顺序取（等于把模板集铺成一张"图集"，每块砖都露面，
            // 适合验收）；--random 用坐标散列，画面更像真实地图但不可复现地好看。
            size_t pick = 0;
            if (random_pick) {
                const uint32_t k = static_cast<uint32_t>(cx) * 73856093u ^
                                   static_cast<uint32_t>(cy) * 19349663u;
                pick = k % set.tiles.size();
            } else {
                pick = (static_cast<size_t>(cx) * static_cast<size_t>(n) +
                        static_cast<size_t>(cy)) % set.tiles.size();
            }
            const TmpFile& t = set.tiles[pick];
            const TmpTile& tile = t.Tiles()[0];

            const int ox = margin_x + (cx - cy) * (cw / 2);
            const int oy = margin_y + (cx + cy) * (ch / 2);

            size_t row_start = 0;
            for (int y = 0; y < ch; ++y) {
                const int x0 = rows[static_cast<size_t>(y)].first;
                const int w = rows[static_cast<size_t>(y)].second;
                const int dy = oy + y;
                if (dy >= 0 && dy < H) {
                    uint8_t* dst = img.data() + static_cast<size_t>(dy) * W;
                    for (int kk = 0; kk < w; ++kk) {
                        const size_t p = row_start + static_cast<size_t>(kk);
                        if (p >= tile.iso.size()) {
                            break;
                        }
                        const uint8_t v = tile.iso[p];
                        const int dx = ox + x0 + kk;
                        if (v != 0 && dx >= 0 && dx < W) {
                            dst[dx] = v;
                        }
                    }
                }
                row_start += static_cast<size_t>(w);
            }

            // extra 用同一套画布坐标，允许越出本格 —— 树冠/岩壁就是这么压到上一格的。
            if (!tile.extra.empty() && tile.header.extra_width > 0) {
                const int ew = static_cast<int>(tile.header.extra_width);
                const int eh = static_cast<int>(tile.header.extra_height);
                for (int y = 0; y < eh; ++y) {
                    const int dy = oy + tile.header.extra_y + y;
                    if (dy < 0 || dy >= H) {
                        continue;
                    }
                    uint8_t* dst = img.data() + static_cast<size_t>(dy) * W;
                    for (int x = 0; x < ew; ++x) {
                        const int dx = ox + tile.header.extra_x + x;
                        const uint8_t v = tile.extra[static_cast<size_t>(y) * ew + x];
                        if (v != 0 && dx >= 0 && dx < W) {
                            dst[dx] = v;
                        }
                    }
                }
            }
            ++drawn;
        }
    }
    std::printf("[地形] 拼了 %d 个地块 -> 索引图 %dx%d\n", drawn, W, H);
    *out = std::move(img);
    *out_w = W;
    *out_h = H;
    return true;
}

void Rebuild_Vxl_Pose();   ///< 定义在下面（要等 DX12 就绪才能上传）

/// 地图模式下的绘制缩放：默认"整张图刚好装进窗口"，按 F 切回手动。
float Map_Draw_Scale() {
    if (!g_app.map_mode || g_app.map_w <= 0 || g_app.map_h <= 0) {
        return g_app.scale;
    }
    if (g_app.fit_map) {
        // 不用 std::min：windows.h 把 min 定义成了宏，会撞。
        const float sx = (kWinW - 16.0f) / static_cast<float>(g_app.map_w);
        const float sy = (kWinH - 16.0f) / static_cast<float>(g_app.map_h);
        const float s = (sx < sy) ? sx : sy;
        return s > 0.0f ? s : 1.0f;
    }
    return g_app.scale;
}

/// 把当前帧调色板按阵营色 remap 一份。remap 关着就原样返回。
///
/// 区间取素材自己报的：VXL 头里有 Remap_Start/End（实测 16/31），
/// SHP 没有这个字段，用同一个实测区间 —— unittem.pal 的 0x10..0x1F
/// 就是那组纯红占位色，各剧场单位调色板布局一致。
Palette Current_Palette_Remapped() {
    if (g_app.remap_index < 0 || g_app.remap_table.Color_Count() <= 0) {
        return g_app_frame_palette;
    }
    uint8_t pal8[768];
    g_app_frame_palette.To_RGB8(pal8);
    int rs = 16, re = 31;
    if (g_app.vxl.Limb_Count() > 0) {
        rs = g_app.vxl.Remap_Start();
        re = g_app.vxl.Remap_End();
    }
    // 原地改写：Make_Palette768 先把 base 整份拷进 out 再动 remap 区，
    // 同一指针时那次 memcpy 是自拷贝，安全。
    g_app.remap_table.Make_Palette768(pal8, true, rs, re, g_app.remap_index, pal8);
    Palette out;
    out.Load_Expanded(pal8, 768);
    return out;
}

void Upload_Current_Frame() {
    g_app.r.Set_Palette(Current_Palette_Remapped());
    if (g_app.map_mode && !g_app.map_rgba.empty()) {
        g_app.sprite = g_app.r.Upload_Sprite_RGBA(
            reinterpret_cast<const uint8_t*>(g_app.map_rgba.data()),
            g_app.map_w, g_app.map_h);
        return;
    }
    if (!g_app.terrain.empty()) {
        g_app.sprite = g_app.r.Upload_Sprite(g_app.terrain.data(),
                                             g_app.terrain_w, g_app.terrain_h);
        return;
    }
    if (g_app.vxl.Limb_Count() > 0) {
        // VXL 模式：每次都要按当前姿势重画 —— 姿势变了图片就变了。
        Rebuild_Vxl_Pose();
        return;
    }
    const ShpFrameInfo& f = g_app.shp.Frame_Info(g_app.frame);
    g_app.sprite = g_app.r.Upload_Sprite(g_app.shp.Frame_Pixels(g_app.frame).data(), f.w, f.h);
}

/// 在归档里找这个 VXL 的 HVA。
///
/// MIX 不存文件名（游戏是按 `模型名 + ".HVA"` 拼名算 CRC 查表的），所以只能
/// 按内容配 —— 判据在 HvaFile.h 的 Hva_Matches_Vxl 里：
/// "HVA 第 0 帧的矩阵 == VXL 肢体尾的姿态"。要求唯一命中，不唯一就不用，
/// 宁可静态也不要配错（配错会让整辆车错位）。
bool Find_Hva_For_Vxl(MixFileClass& m, const VxlFile& vxl, HvaFile* out,
                      uint32_t* out_id) {
    int hits = 0;
    HvaFile best;
    uint32_t best_id = 0;
    std::function<void(const MixFileClass&, int)> walk = [&](const MixFileClass& mm,
                                                             int depth) {
        for (const MixEntry& e : mm.Entries()) {
            if (depth < 3) {
                auto sub = mm.Open_Sub(e);
                if (sub) {
                    walk(*sub, depth + 1);
                    continue;
                }
            }
            const std::vector<uint8_t> d = mm.Read_Entry(e);
            HvaFile h;
            if (!h.Load(d.data(), d.size())) {
                continue;
            }
            if (!Hva_Matches_Vxl(h, vxl)) {
                continue;
            }
            ++hits;
            if (hits == 1) {
                best = h;
                best_id = e.id;
            }
        }
    };
    walk(m, 0);
    if (hits != 1) {
        std::printf("     HVA 配对: 命中 %d 个%s\n", hits,
                    hits == 0 ? "（该 VXL 没有动画，用静态姿态）" : "（不唯一，放弃配对）");
        return false;
    }
    *out = best;
    *out_id = best_id;
    std::printf("     HVA 配对: 0x%08X  %d 帧 × %d 肢\n", best_id, best.Frame_Count(),
                best.Limb_Count());
    return true;
}

/// VXL 模式：按当前 HVA 帧重算索引图并重新上传。
void Rebuild_Vxl_Pose() {
    if (g_app.vxl.Limb_Count() <= 0) {
        return;
    }
    const float* pose = nullptr;
    if (g_app.hva_linked && g_app.hva.Frame_Count() > 0) {
        const int f = g_app.hva_frame % g_app.hva.Frame_Count();
        g_app.vxl_pose.assign(static_cast<size_t>(g_app.vxl.Limb_Count()) * 12, 0.0f);
        for (int l = 0; l < g_app.vxl.Limb_Count(); ++l) {
            const HvaMatrix hm = g_app.hva.Matrix(f, l);
            std::memcpy(&g_app.vxl_pose[static_cast<size_t>(l) * 12], hm.m,
                        sizeof(float) * 12);
        }
        pose = g_app.vxl_pose.data();
    }
    // 炮塔 / 炮管：叠一层绕 Z 的偏航（炮塔朝向）和绕 Y 的俯仰（炮口抬高）。
    // 实测 GTNK 车体顶面 z=11.01、GTNKTUR 底面 z=11.02、GTNKBARL 从炮塔内部
    // 穿出，三者肢体平移完全相同 —— 共享模型空间原点，所以不需要坐标换算。
    VxlAttach att[2];
    int att_n = 0;
    // 3×4 = Rz(yaw) · [ Ry(-pitch) | 枢轴项 ]，先绕枢轴 p 俯仰、再整体偏航。
    //
    // 模型空间 X 向前、Y 横向、Z 向上（实测四足机甲的四只脚分别在 ±X、±Y 上）。
    // 右手系里绕 +Y 转 +φ 是把 +X 压向 −Z，也就是低头 —— 所以抬炮口要传
    // −pitch，不然按 W 炮管会往下扎（踩过）。
    //
    // 【枢轴不能是原点】炮管绕模型原点俯仰时，管尾会从炮塔顶上戳出来。
    // 实测 GTNKBARL 世界 X 7.85..32.85、Z 11.18..14.18，真正的耳轴在管尾
    // (7.85, 0, 12.7) 附近，所以枢轴取"炮管自身 AABB 的尾端中点"。
    // 炮塔没有这个问题（炮塔环本来就在模型原点：GTNKTUR 世界 X 中心 −1.29），
    // 传 p = {0,0,0} 即可。
    auto attach_matrix = [](float m[12], float yaw, float pitch, const float p[3]) {
        const float cy = std::cos(yaw), sy = std::sin(yaw);
        const float cp = std::cos(-pitch), sp = std::sin(-pitch);
        const float rz[9] = {cy, -sy, 0, sy, cy, 0, 0, 0, 1};
        const float ry[9] = {cp, 0, sp, 0, 1, 0, -sp, 0, cp};
        // t = p − Ry·p：把"绕原点转"变成"绕 p 转"
        const float tx = p[0] - (ry[0] * p[0] + ry[2] * p[2]);
        const float ty = 0.0f;
        const float tz = p[2] - (ry[6] * p[0] + ry[8] * p[2]);
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                m[r * 4 + c] = rz[r * 3 + 0] * ry[0 * 3 + c] +
                               rz[r * 3 + 1] * ry[1 * 3 + c] +
                               rz[r * 3 + 2] * ry[2 * 3 + c];
            }
            m[r * 4 + 3] = rz[r * 3 + 0] * tx + rz[r * 3 + 1] * ty + rz[r * 3 + 2] * tz;
        }
    };
    if (g_app.has_turret) {
        att[att_n].file = &g_app.turret;
        const float zero[3] = {0, 0, 0};
        attach_matrix(att[att_n].transform, g_app.turret_yaw, 0.0f, zero);
        ++att_n;
    }
    if (g_app.has_barrel) {
        att[att_n].file = &g_app.barrel;
        float pivot[3] = {0, 0, 0};
        if (g_app.barrel.Limb_Count() > 0) {
            const VxlLimbTailer& bt = g_app.barrel.Tailer(0);
            pivot[0] = bt.transform[3] + bt.min_bounds[0];
            pivot[1] = bt.transform[7] +
                       (bt.min_bounds[1] + bt.max_bounds[1]) * 0.5f;
            pivot[2] = bt.transform[11] +
                       (bt.min_bounds[2] + bt.max_bounds[2]) * 0.5f;
        }
        attach_matrix(att[att_n].transform, g_app.turret_yaw, g_app.barrel_pitch,
                      pivot);
        ++att_n;
    }
    const VxlAttach* att_p = (att_n > 0) ? att : nullptr;

    // 【光影】体素走真彩管线：明暗由法线算好后烘进 RGB（见 gfx/VoxelLight.h）。
    // 索引管线做不到 —— 同一个色号在不同朝向上要有不同亮度。
    const VoxelLight* light_p = g_app.light_on ? &g_app.light : nullptr;

    // 先按每体素 8 像素画；画布超过窗口就整体缩小重画一次。
    // 体素模型的世界尺寸差得极远（小坦克 ~30、四足机甲 ~400），固定倍数必然有一头看不全。
    // 阴影和明暗用同一个光向量：同一个太阳，影子才不会和受光面对不上。
    const float* shadow_light = g_app.shadow.on ? g_app.light.light : nullptr;
    std::vector<uint8_t>* shadow_out =
        g_app.shadow.on ? &g_app.vxl_shadow : nullptr;

    float sc = 8.0f;
    if (!g_app.vxl.Render_Isometric(&g_app.vxl_idx, &g_app.vxl_w, &g_app.vxl_h, sc, pose,
                                    att_p, att_n, light_p, &g_app.vxl_shade,
                                    shadow_light, g_app.shadow.ground_z, shadow_out)) {
        std::printf("[x] 体素光栅化失败（HVA 帧 %d）\n", g_app.hva_frame);
        return;
    }
    // 注意：windows.h 把 min/max 定义成宏，这里不能写 std::min/std::max。
    const float fx = (kWinW - 64.0f) / g_app.vxl_w;
    const float fy = (kWinH - 64.0f) / g_app.vxl_h;
    const float fit = (fx < fy) ? fx : fy;
    if (fit < 1.0f) {
        sc = 8.0f * fit;
        if (sc < 1.0f) {
            sc = 1.0f;
        }
        if (!g_app.vxl.Render_Isometric(&g_app.vxl_idx, &g_app.vxl_w, &g_app.vxl_h, sc,
                                        pose, att_p, att_n, light_p, &g_app.vxl_shade,
                                        shadow_light, g_app.shadow.ground_z, shadow_out)) {
            std::printf("[x] 体素光栅化失败（缩放后）\n");
            return;
        }
    }
    // 转炮塔 / 翻 HVA 帧会直接调到这里（不走 Upload_Current_Frame），
    // 所以调色板必须在这里也刷一次，不然换阵营色后要等到下一帧才生效。
    g_app.r.Set_Palette(Current_Palette_Remapped());

    if (g_app.light_on) {
        // 打光管线吃的是 768 字节调色板，remap 开着时用换过阵营色的那份。
        const uint8_t* pal768 = g_app.vxl.Palette();
        if (g_app.remap_index >= 0) {
            g_app.remap_pal.assign(768, 0);
            if (g_app.remap_table.Make_Palette768(
                    g_app.vxl.Palette(), true, g_app.vxl.Remap_Start(),
                    g_app.vxl.Remap_End(), g_app.remap_index,
                    g_app.remap_pal.data())) {
                pal768 = g_app.remap_pal.data();
            }
        }
        Shade_To_RGBA(g_app.vxl_idx.data(), g_app.vxl_shade.data(),
                      g_app.vxl_w * g_app.vxl_h, pal768, g_app.light,
                      &g_app.vxl_rgba);
        if (g_app.shadow.on && g_app.vxl_shadow.size() ==
                                   static_cast<size_t>(g_app.vxl_w) * g_app.vxl_h) {
            Composite_Shadow(g_app.vxl_rgba.data(), g_app.vxl_w * g_app.vxl_h,
                             g_app.vxl_idx.data(), g_app.vxl_shadow.data(),
                             g_app.shadow);
        }
        g_app.sprite = g_app.r.Upload_Sprite_RGBA(g_app.vxl_rgba.data(), g_app.vxl_w,
                                                  g_app.vxl_h);
    } else {
        g_app.sprite = g_app.r.Upload_Sprite(g_app.vxl_idx.data(), g_app.vxl_w,
                                             g_app.vxl_h);
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_KEYDOWN:
            // 地图模式：方向键改成平移，F 切"整图适配/手动缩放"。
            // 没有帧可以翻，所以这几个键闲着也是闲着。
            if (g_app.map_mode) {
                const float step = 160.0f;
                if (wp == VK_LEFT)  g_app.pan_x += step;
                if (wp == VK_RIGHT) g_app.pan_x -= step;
                if (wp == VK_UP)    g_app.pan_y += step;
                if (wp == VK_DOWN)  g_app.pan_y -= step;
                if (wp == 'F') {
                    g_app.fit_map = !g_app.fit_map;
                    if (!g_app.fit_map) {
                        g_app.scale = 1.0f;
                    }
                    std::printf("整图适配：%s\n", g_app.fit_map ? "开" : "关");
                }
                return 0;
            }
            if (wp == VK_ESCAPE) {
                PostQuitMessage(0);
            } else if (wp == VK_SPACE || wp == VK_RIGHT) {
                if (g_app.hva_linked && g_app.hva.Frame_Count() > 0) {
                    // 体素模式：HVA 的帧才是"动画帧"，VXL 只有一个静态形状。
                    g_app.hva_frame = (g_app.hva_frame + 1) % g_app.hva.Frame_Count();
                    Rebuild_Vxl_Pose();
                    std::printf("HVA 帧 %d / %d\n", g_app.hva_frame,
                                g_app.hva.Frame_Count());
                } else if (g_app.shp.Frame_Count() > 0) {
                    g_app.frame = (g_app.frame + 1) % g_app.shp.Frame_Count();
                    Upload_Current_Frame();
                }
            } else if (wp == VK_LEFT) {
                if (g_app.hva_linked && g_app.hva.Frame_Count() > 0) {
                    g_app.hva_frame = (g_app.hva_frame + g_app.hva.Frame_Count() - 1) %
                                      g_app.hva.Frame_Count();
                    Rebuild_Vxl_Pose();
                    std::printf("HVA 帧 %d / %d\n", g_app.hva_frame,
                                g_app.hva.Frame_Count());
                } else if (g_app.shp.Frame_Count() > 0) {
                    g_app.frame = (g_app.frame + g_app.shp.Frame_Count() - 1) %
                                  g_app.shp.Frame_Count();
                    Upload_Current_Frame();
                }
            } else if (wp == 'L') {
                // 开/关体素光影。关掉就是"原始色号"直出，用来对比明暗到底起了多大作用。
                g_app.light_on = !g_app.light_on;
                Rebuild_Vxl_Pose();
                std::printf("体素光影：%s（环境光 %.2f 漫反射 %.2f 分级 %d）\n",
                            g_app.light_on ? "开" : "关", g_app.light.ambient,
                            g_app.light.diffuse, g_app.light.levels);
            } else if (wp == 'H') {
                // 开/关地面投影阴影。
                g_app.shadow.on = !g_app.shadow.on;
                Rebuild_Vxl_Pose();
                std::printf("地面阴影：%s（地面 z=%.1f 不透明度 %.2f）\n",
                            g_app.shadow.on ? "开" : "关", g_app.shadow.ground_z,
                            g_app.shadow.alpha);
                InvalidateRect(hwnd, nullptr, FALSE);
            } else if (wp == 'R') {
                // 循环阵营色。R 一下换一个，走完回到"不 remap"。
                if (g_app.remap_table.Color_Count() <= 0) {
                    std::printf("阵营色表没加载（缺 rules.ini 的 [Colors]）\n");
                } else {
                    g_app.remap_index += 1;
                    if (g_app.remap_index >= g_app.remap_table.Color_Count()) {
                        g_app.remap_index = -1;
                    }
                    if (g_app.remap_index < 0) {
                        std::printf("阵营色：关（原样占位色）\n");
                    } else {
                        const RemapColor& rc = g_app.remap_table.Color(g_app.remap_index);
                        std::printf("阵营色 [%d/%d] %s\n", g_app.remap_index,
                                    g_app.remap_table.Color_Count(), rc.name.c_str());
                    }
                    Upload_Current_Frame();
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            } else if (wp == VK_OEM_PLUS || wp == VK_ADD) {
                g_app.scale *= 1.25f;
            } else if (wp == VK_OEM_MINUS || wp == VK_SUBTRACT) {
                g_app.scale /= 1.25f;
                if (g_app.scale < 0.05f) {
                    g_app.scale = 0.05f;
                }
            } else if ((wp == 'A' || wp == 'D') &&
                       (g_app.has_turret || g_app.has_barrel)) {
                // 转炮塔：绕 Z。模型空间 X 向前、Y 横向、Z 向上。
                g_app.turret_yaw += (wp == 'D') ? 0.1745f : -0.1745f;   // ±10°
                Rebuild_Vxl_Pose();
                std::printf("炮塔偏航 %.0f°\n", g_app.turret_yaw * 57.29578f);
                InvalidateRect(hwnd, nullptr, FALSE);
            } else if ((wp == 'W' || wp == 'S') && g_app.has_barrel) {
                // 抬炮口：绕 Y。限到 [-5°, 60°]，别让炮管翻过去。
                g_app.barrel_pitch += (wp == 'W') ? 0.0873f : -0.0873f;   // ±5°
                if (g_app.barrel_pitch < -0.0873f) {
                    g_app.barrel_pitch = -0.0873f;
                }
                if (g_app.barrel_pitch > 1.0472f) {
                    g_app.barrel_pitch = 1.0472f;
                }
                Rebuild_Vxl_Pose();
                std::printf("炮口俯仰 %.0f°\n", g_app.barrel_pitch * 57.29578f);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_MOUSEWHEEL: {
            const int d = GET_WHEEL_DELTA_WPARAM(wp);
            g_app.scale *= (d > 0) ? 1.1f : (1.0f / 1.1f);
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR cmdline, int) {
    // 关掉 stdout 缓冲：这个程序可能被超时杀掉，缓冲里的进度会整段丢失，
    // 那样就分不清是"卡在 DX12 初始化"还是"卡在消息循环"。
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    // 命令行（宽字符 -> UTF-8 多字节）
    // 不能用 std::string(w.begin(), w.end())：那是按 wchar_t 逐位截断成 char，
    // 中文/日文路径会直接变成乱码。必须走 WideCharToMultiByte。
    // 【别设太小】原来是 8，加上炮塔/炮管那组开关（--turret/--barrel/
    // --turretyaw/--barrelpitch）之后 9 个参数就溢出了，超出的被静默丢掉 ——
    // 表现是 `--offscreen` 消失、程序开了个窗口等着按键。
    constexpr int kMaxArgs = 32;
    char args[kMaxArgs][MAX_PATH] = {};
    {
        std::string a;
        if (cmdline && *cmdline) {
            const int need = WideCharToMultiByte(CP_UTF8, 0, cmdline, -1, nullptr, 0,
                                                 nullptr, nullptr);
            if (need > 1) {
                a.resize(static_cast<size_t>(need));
                WideCharToMultiByte(CP_UTF8, 0, cmdline, -1, a.data(), need,
                                    nullptr, nullptr);
                a.resize(static_cast<size_t>(need) - 1);   // 去掉结尾的 '\0'
            }
        }
        // 简单按空格切，路径带引号就去掉引号
        int n = 0;
        std::string cur;
        bool q = false;
        for (char c : a) {
            if (c == '"') {
                q = !q;
            } else if ((c == ' ' || c == '\t') && !q) {
                if (!cur.empty() && n < kMaxArgs) {
                    std::snprintf(args[n++], MAX_PATH, "%s", cur.c_str());
                }
                cur.clear();
            } else {
                cur += c;
            }
        }
        if (!cur.empty()) {
            if (n < kMaxArgs) {
                std::snprintf(args[n++], MAX_PATH, "%s", cur.c_str());
            } else {
                std::printf("[!] 命令行参数超过 %d 个，多的被丢弃：%s\n", kMaxArgs,
                            cur.c_str());
            }
        }
    }
    const char* mix_path = args[0][0] ? args[0] : "D:\\westwood\\RA2YR\\ra2md.mix";
    const char* shp_name = args[1];
    const char* pal_name = args[2];
    // 剩下的参数里找开关（--offscreen / --framesN / --frameN）
    bool offscreen = false;
    int want_frame = 0;
    for (int i = 0; i < kMaxArgs; ++i) {
        if (std::strncmp(args[i], "--offscreen", 11) == 0) {
            offscreen = true;
        } else if (std::strncmp(args[i], "--frame", 7) == 0 && args[i][7] != 's') {
            // --frameN：指定渲第几帧。回归 flags=0x03 要靠它，因为离线路径
            // 只渲一帧，而 flags 各异的帧常常不在 0 号位。
            want_frame = std::atoi(args[i] + 7);
        }
    }

    std::printf("[1] 打开 MIX: %s\n", mix_path);
    if (!g_app.mix.Open(mix_path)) {
        std::printf("[x] MIX 打不开: %s\n", mix_path);
        return 1;
    }
    std::printf("MIX  %s  条目=%d\n", mix_path, g_app.mix.Count());

    // ---- TMP 地形模式：--tmp <0x归档ID> ----
    // 归档 ID 必须是明文 MIX（地形素材全是这一类），pal_name 缺省用温带调色板。
    // 形如：ra2view.exe <mix> --tmp 0x0F5D1D99 [TEMPERAT.PAL] [--grid15]
    // 地形模式的两个位置参数跟在 --tmp 后面，和 SHP 模式（第 2/3 个位置参数）
    // 是两套，别混用 —— 否则 "--tmp" 会被当成 SHP 名。
    int tmp_arch = -1;
    // 注意别用 int 存 VXL 的 CRC：0x8C848DEE 这类高位为 1 的 ID 转 int 会变负数，
    // 于是 vxl_id >= 0 这种判断整个失效（踩过）。这里用 uint32 + 独立的开关布尔。
    bool vxl_mode = false;
    uint32_t vxl_id = 0;
    bool hva_force = false;          ///< --hva 0xID：不走自动配对
    uint32_t hva_force_id = 0;
    int hva_frame = 0;               ///< --hvaframeN：初始帧
    int grid = 15;
    bool random_pick = false;
    // 炮塔 / 炮管：RA2 里是独立文件（<名>TUR.VXL / <名>BARL.VXL），
    // 与车体共享模型空间原点，所以要单独给 id 再各自绕 Z 转。
    bool has_turret = false;
    uint32_t turret_id = 0;
    bool has_barrel = false;
    uint32_t barrel_id = 0;
    float turret_yaw = 0.0f;
    float barrel_pitch = 0.0f;
    // P2 数据层：--unit <单位名>，车体/炮塔/炮管全从 INI 推，不用手写 ID。
    const char* unit_name = nullptr;
    const char* addmix_path = nullptr;
    const char* map_path = nullptr;    ///< --map <地图.mmx/.yro/.map>
    const char* tmp_pal_name = "TEMPERAT.PAL";
    const char* remap_arg = nullptr;
    for (int i = 0; i < kMaxArgs; ++i) {
        if (std::strcmp(args[i], "--vxl") == 0 && i + 1 < kMaxArgs && args[i + 1][0]) {
            // MIX 里没有 VXL 的文件名可用，只能按 CRC 取。
            vxl_id = static_cast<uint32_t>(std::strtoul(args[i + 1], nullptr, 16));
            vxl_mode = true;
        } else if (std::strcmp(args[i], "--turret") == 0 && i + 1 < kMaxArgs &&
                   args[i + 1][0]) {
            turret_id = static_cast<uint32_t>(std::strtoul(args[i + 1], nullptr, 16));
            has_turret = true;
        } else if (std::strcmp(args[i], "--barrel") == 0 && i + 1 < kMaxArgs &&
                   args[i + 1][0]) {
            barrel_id = static_cast<uint32_t>(std::strtoul(args[i + 1], nullptr, 16));
            has_barrel = true;
        } else if (std::strcmp(args[i], "--turretyaw") == 0 && i + 1 < kMaxArgs &&
                   args[i + 1][0]) {
            turret_yaw = static_cast<float>(std::atof(args[i + 1])) * 3.14159265f / 180.0f;
        } else if (std::strcmp(args[i], "--barrelpitch") == 0 && i + 1 < kMaxArgs &&
                   args[i + 1][0]) {
            barrel_pitch =
                static_cast<float>(std::atof(args[i + 1])) * 3.14159265f / 180.0f;
        } else if (std::strncmp(args[i], "--hvaframe", 10) == 0 && args[i][10]) {
            hva_frame = std::atoi(args[i] + 10);        // --hvaframe8
        } else if (std::strcmp(args[i], "--hva") == 0 && i + 1 < kMaxArgs &&
                   args[i + 1][0]) {
            hva_force_id = static_cast<uint32_t>(std::strtoul(args[i + 1], nullptr, 16));
            hva_force = true;
        } else if (std::strcmp(args[i], "--tmp") == 0 && i + 1 < kMaxArgs && args[i + 1][0]) {
            tmp_arch = static_cast<int>(std::strtoul(args[i + 1], nullptr, 16));
            if (i + 2 < kMaxArgs && args[i + 2][0] && args[i + 2][0] != '-') {
                tmp_pal_name = args[i + 2];
            }
        } else if (std::strcmp(args[i], "--grid") == 0 && i + 1 < kMaxArgs && args[i + 1][0]) {
            grid = std::atoi(args[i + 1]);       // --grid 16
        } else if (std::strncmp(args[i], "--grid", 6) == 0 && args[i][6]) {
            grid = std::atoi(args[i] + 6);       // --grid16（两种写法都收）
        } else if (std::strcmp(args[i], "--random") == 0) {
            random_pick = true;
        } else if (std::strcmp(args[i], "--unit") == 0 && i + 1 < kMaxArgs &&
                   args[i + 1][0]) {
            unit_name = args[i + 1];
        } else if (std::strcmp(args[i], "--addmix") == 0 && i + 1 < kMaxArgs &&
                   args[i + 1][0]) {
            addmix_path = args[i + 1];
        } else if (std::strcmp(args[i], "--map") == 0 && i + 1 < kMaxArgs &&
                   args[i + 1][0]) {
            map_path = args[i + 1];
        } else if (std::strcmp(args[i], "--light") == 0 && i + 1 < kMaxArgs &&
                   args[i + 1][0]) {
            // 光向量 "x,y,z"（指向光源）。明暗和阴影共用 —— 同一个太阳。
            // 默认 normalize(1,1,2) 的来由见 gfx/VoxelLight.h。
            float v[3] = {0, 0, 0};
            int got = std::sscanf(args[i + 1], "%f,%f,%f", &v[0], &v[1], &v[2]);
            if (got == 3) {
                g_app.light.light[0] = v[0];
                g_app.light.light[1] = v[1];
                g_app.light.light[2] = v[2];
                g_app.light.Normalize();
            }
        } else if (std::strcmp(args[i], "--shadow") == 0) {
            // 开地面投影阴影。可以带不透明度：--shadow 0.5
            g_app.shadow.on = true;
            if (i + 1 < kMaxArgs && args[i + 1][0] && args[i + 1][0] != '-') {
                g_app.shadow.alpha = static_cast<float>(std::atof(args[i + 1]));
            }
            if (g_app.shadow.alpha < 0.0f) g_app.shadow.alpha = 0.0f;
            if (g_app.shadow.alpha > 1.0f) g_app.shadow.alpha = 1.0f;
        } else if (std::strncmp(args[i], "--shadow", 8) == 0 && args[i][8]) {
            g_app.shadow.on = true;
            g_app.shadow.alpha = static_cast<float>(std::atof(args[i] + 8));
        } else if (std::strcmp(args[i], "--remap") == 0 && i + 1 < kMaxArgs &&
                   args[i + 1][0]) {
            // 阵营色：[Colors] 的下标，或颜色名（DarkRed / Gold …）。
            // 表是在 VXL 载入之后才读的，所以这里只把字符串存下来。
            remap_arg = args[i + 1];
        }
    }
    if (grid < 1 || grid > 64) {
        grid = 15;
    }

    // ---- --addmix：第二个归档（YR 的 rulesmd/artmd 就在 ra2md.mix 里）----
    if (addmix_path && *addmix_path) {
        if (g_app.mix2.Open(addmix_path)) {
            g_app.has_mix2 = true;
            std::printf("MIX2 %s  条目=%d\n", addmix_path, g_app.mix2.Count());
        } else {
            std::printf("[!] 副 MIX 打不开: %s\n", addmix_path);
        }
    }

    // ---- --unit <名>：从 INI 推出体素模型组成 ----
    // 这一步把 P2 数据层接进渲染链路：给单位名，剩下的（Image= / Voxel=yes /
    // Turret=yes / 三段 CRC）全由 UnitModelDB 算，不再手写 0xID。
    if (unit_name && *unit_name) {
        std::vector<const MixFileClass*> mounts;
        mounts.push_back(&g_app.mix);
        if (g_app.has_mix2) {
            mounts.push_back(&g_app.mix2);
        }
        UnitModelDB db;
        if (!db.Load(mounts.data(), static_cast<int>(mounts.size()))) {
            std::printf("[x] 单位表加载失败（MIX 里没有 RULES/ART）\n");
            return 1;
        }
        const UnitModel* um = db.Resolve(unit_name);
        if (um == nullptr) {
            std::printf("[x] 单位表里没有 %s\n", unit_name);
            return 1;
        }
        std::printf("[2] 单位模式 %s -> Image=%s Voxel=%s Turret=%s\n",
                    um->unit.c_str(), um->image.c_str(),
                    um->voxel ? "yes" : "no", um->turret ? "yes" : "no");
        if (!um->ok()) {
            std::printf("[x] %s 不是体素单位，或车体 %s 不在包里（0x%08X）\n",
                        um->unit.c_str(), um->body.name.c_str(), um->body.id);
            return 1;
        }
        std::printf("     车体 %s 0x%08X %s\n", um->body.name.c_str(), um->body.id,
                    um->body.present ? "" : "(缺失)");
        std::printf("     HVA  %s 0x%08X %s\n", um->body_hva.name.c_str(),
                    um->body_hva.id, um->body_hva.present ? "" : "(缺失，用静态姿态)");
        vxl_mode = true;
        vxl_id = um->body.id;
        if (um->body_hva.present) {
            hva_force = true;
            hva_force_id = um->body_hva.id;
        }
        if (um->turret_vxl.present) {
            has_turret = true;
            turret_id = um->turret_vxl.id;
            std::printf("     炮塔 %s 0x%08X\n", um->turret_vxl.name.c_str(),
                        um->turret_vxl.id);
        }
        if (um->barrel_vxl.present) {
            has_barrel = true;
            barrel_id = um->barrel_vxl.id;
            std::printf("     炮管 %s 0x%08X\n", um->barrel_vxl.name.c_str(),
                        um->barrel_vxl.id);
        }
        if (um->flh[0] || um->flh[1] || um->flh[2]) {
            std::printf("     FLH  %d,%d,%d\n", um->flh[0], um->flh[1], um->flh[2]);
        }
    }
    // ---- 阵营色表：rules.ini 的 [Colors]，按 R 键循环 ----
    // 放在模式分支之前，VXL / SHP 两条路都要用。
    // rules.ini 在 ra2.mix 里、rulesmd.ini 在 ra2md.mix 里，两个归档都试。
    {
        std::vector<uint8_t> ini = g_app.mix.Read_Deep("rules.ini");
        if (ini.empty() && g_app.has_mix2) {
            ini = g_app.mix2.Read_Deep("rules.ini");
        }
        if (ini.empty()) {
            ini = g_app.mix.Read_Deep("rulesmd.ini");
        }
        if (ini.empty() && g_app.has_mix2) {
            ini = g_app.mix2.Read_Deep("rulesmd.ini");
        }
        IniFile rules;
        if (!ini.empty() && rules.Load(ini.data(), ini.size())) {
            const int n = g_app.remap_table.Load_From_Ini(rules);
            std::printf("[2] 阵营色表 %d 条（按 R 键循环换阵营色）\n", n);
            if (remap_arg != nullptr && *remap_arg) {
                // 先当数字下标，不是数字再按名字找（大小写不敏感）。
                bool numeric = true;
                for (const char* p = remap_arg; *p; ++p) {
                    if (*p < '0' || *p > '9') {
                        numeric = false;
                        break;
                    }
                }
                int idx = numeric ? std::atoi(remap_arg)
                                  : g_app.remap_table.Find_Color(remap_arg);
                if (idx < 0 || idx >= n) {
                    std::printf("     [!] 阵营色 \"%s\" 不在 [Colors] 里，忽略\n", remap_arg);
                } else {
                    g_app.remap_index = idx;
                    std::printf("     启用阵营色 [%d] %s\n", idx,
                                g_app.remap_table.Color(idx).name.c_str());
                }
            }
        } else {
            std::printf("[2] 阵营色表 未加载（找不到 rules.ini）\n");
        }
    }

    if (map_path && *map_path) {
        // ---- --map <地图>：铺一整张真实战场 ----
        std::printf("[2] 地图模式 %s\n", map_path);
        MapFile mf;
        std::string err;
        if (!mf.Load_Path(map_path, &err)) {
            std::printf("[x] 地图加载失败: %s\n", err.c_str());
            return 1;
        }
        std::printf("    %s 剧场 %s 尺寸 %dx%d，IsoMapPack5 %zu -> %zu 字节，"
                    "真单元 %zu\n",
                    mf.Name().c_str(), mf.Theater().c_str(), mf.Width(), mf.Height(),
                    mf.Packed_Bytes(), mf.Unpacked_Bytes(), mf.Cells().size());

        std::vector<MixFileClass*> roots;
        roots.push_back(&g_app.mix);
        if (g_app.has_mix2) {
            roots.push_back(&g_app.mix2);
        }
        MapRenderer mr;
        if (!mr.Bind(roots, mf, &err)) {
            std::printf("[x] 素材绑定失败: %s\n", err.c_str());
            return 1;
        }
        std::printf("    剧场 %s：.%s / %s / %d 个瓦片（下标 0 = %s）\n",
                    mr.Theater().c_str(), mr.Extension().c_str(),
                    mr.Palette_Name().c_str(), mr.Tile_Count(),
                    mr.Tile_File_Name(0).c_str());
        if (!mr.Render(mf, 0, 0, 0, 0, &g_app.map_rgba, &g_app.map_w, &g_app.map_h,
                       &err)) {
            std::printf("[x] 铺图失败: %s\n", err.c_str());
            return 1;
        }
        g_app.map_mode = true;
        std::printf("    战场画布 %dx%d，瓦片命中 %d 缺失 %d\n",
                    g_app.map_w, g_app.map_h, mr.Tiles_Loaded(), mr.Tiles_Missing());
        std::printf("SHP  (地图模式，跳过)\n");
    } else if (tmp_arch >= 0) {
        std::printf("[2] TMP 地形模式 归档=0x%08X 网格=%d\n", tmp_arch, grid);
        const MixEntry* arch = g_app.mix.Find_By_ID(static_cast<uint32_t>(tmp_arch));
        if (arch == nullptr) {
            std::printf("[x] 顶层没有 0x%08X\n", tmp_arch);
            return 1;
        }
        auto sub = g_app.mix.Open_Sub(*arch);
        if (!sub) {
            std::printf("[x] 0x%08X 不是 MIX\n", tmp_arch);
            return 1;
        }
        std::printf("归档 flags=%s 条目=%d\n",
                    MixFileClass::Describe_Flags(sub->Flags()).c_str(), sub->Count());

        Palette pal;
        const char* pn = tmp_pal_name;
        std::vector<uint8_t> pd = g_app.mix.Read_Deep(pn);
        if (pd.size() != 768 || !pal.Load(pd.data(), pd.size())) {
            std::printf("[x] 调色板取不到: %s\n", pn);
            return 1;
        }
        g_app_frame_palette = pal;
        g_app.pal_name = pn;
        std::printf("调色板 %s\n", pn);

        TerrainTileSet set;
        if (!Load_Terrain_Tiles(*sub, &set)) {
            std::printf("[x] 归档里没有可用的 1x1 地形模板\n");
            return 1;
        }
        std::printf("可用地形模板 %zu 个，每格 %dx%d\n", set.tiles.size(),
                    set.cell_w, set.cell_h);
        if (!Build_Terrain_Image(set, grid, random_pick, &g_app.terrain,
                                 &g_app.terrain_w, &g_app.terrain_h)) {
            std::printf("[x] 地形合成失败\n");
            return 1;
        }
        std::printf("SHP  (地形模式，跳过)\n");
    } else if (vxl_mode) {
        // ---- VXL 体素模式：--vxl <0xID> ----
        // 形如：ra2view.exe <mix> --vxl 0x8C848DEE
        // 调色板不用外部 .PAL —— VXL 自带 768 字节内嵌调色板，而且是**展开好的
        // 8 位值**（全部分量 ≡ 3 mod 4），必须走 Load_Expanded，走 Load 再展开
        // 一次会整片变青紫洋红。
        std::printf("[2] VXL 体素模式 id=0x%08X\n", vxl_id);
        const std::vector<uint8_t> d = Read_Any(vxl_id);
        if (d.empty()) {
            std::printf("[x] 递归找不到 0x%08X\n", vxl_id);
            return 1;
        }
        if (!g_app.vxl.Load(d.data(), d.size())) {
            std::printf("[x] 0x%08X 不是自洽的 VXL（%zu 字节）\n", vxl_id, d.size());
            return 1;
        }
        std::printf("VXL  limbs=%d body=%u remap=(%d,%d)\n", g_app.vxl.Limb_Count(),
                    g_app.vxl.Body_Size(), g_app.vxl.Remap_Start(),
                    g_app.vxl.Remap_End());
        for (int l = 0; l < g_app.vxl.Limb_Count(); ++l) {
            const VxlLimbTailer& t = g_app.vxl.Tailer(l);
            std::printf("     limb[%2d] %-12s %dx%dx%d nt=%d\n", l,
                        g_app.vxl.Header(l).name.c_str(), t.x_size, t.y_size, t.z_size,
                        t.normals_type);
        }
        // 动画：--hva 0xID 强制指定，否则按"第 0 帧矩阵 == 肢体尾姿态"的内容指纹自动配。
        if (hva_force) {
            const std::vector<uint8_t> hd = Read_Any(hva_force_id);
            g_app.hva_linked = g_app.hva.Load(hd.data(), hd.size());
            g_app.hva_id = hva_force_id;
            std::printf("     HVA 指定 0x%08X -> %s（%d 帧 × %d 肢）\n", hva_force_id,
                        g_app.hva_linked ? "载入成功" : "不是 HVA", g_app.hva.Frame_Count(),
                        g_app.hva.Limb_Count());
        } else {
            g_app.hva_linked =
                Find_Hva_For_Vxl(g_app.mix, g_app.vxl, &g_app.hva, &g_app.hva_id);
            if (!g_app.hva_linked && g_app.has_mix2) {
                Find_Hva_For_Vxl(g_app.mix2, g_app.vxl, &g_app.hva, &g_app.hva_id);
            }
        }
        g_app.hva_frame = hva_frame;

        // 炮塔 / 炮管：独立的 VXL，和车体共享模型空间原点。
        // 实测 GTNK 车体顶面 z=11.01、GTNKTUR 底面 z=11.02 —— 直接叠即可。
        if (has_turret) {
            const std::vector<uint8_t> td = Read_Any(turret_id);
            g_app.has_turret = !td.empty() && g_app.turret.Load(td.data(), td.size());
            std::printf("     炮塔 0x%08X -> %s（%d 肢）\n", turret_id,
                        g_app.has_turret ? "载入成功" : "不是 VXL",
                        g_app.has_turret ? g_app.turret.Limb_Count() : 0);
        }
        if (has_barrel) {
            const std::vector<uint8_t> bd = Read_Any(barrel_id);
            g_app.has_barrel = !bd.empty() && g_app.barrel.Load(bd.data(), bd.size());
            std::printf("     炮管 0x%08X -> %s（%d 肢）\n", barrel_id,
                        g_app.has_barrel ? "载入成功" : "不是 VXL",
                        g_app.has_barrel ? g_app.barrel.Limb_Count() : 0);
        }
        g_app.turret_yaw = turret_yaw;
        g_app.barrel_pitch = barrel_pitch;

        Palette vpal;
        vpal.Load_Expanded(g_app.vxl.Palette(), 768);
        g_app_frame_palette = vpal;
        g_app.pal_name = "(VXL 内嵌调色板)";
        std::printf("调色板 %s\n", g_app.pal_name.c_str());
        std::printf("SHP  (VXL 模式，跳过)\n");

        std::printf("     remap 区间 %d..%d（VXL 头自报）\n", g_app.vxl.Remap_Start(),
                    g_app.vxl.Remap_End());
    } else {

    std::printf("[2] 载入素材\n");
    int best_i = -1;
    if (!Load_Asset(g_app.mix, shp_name, pal_name, &g_app.shp, &g_app.pal_name, &best_i)) {
        return 1;
    }
    const ShpFrameInfo& f0 = g_app.shp.Frame_Info(0);
    std::printf("SHP  %dx%d 帧=%d  帧0 flags=0x%X 调色板=%s\n",
                g_app.shp.Width(), g_app.shp.Height(), g_app.shp.Frame_Count(),
                f0.flags, g_app.pal_name.c_str());
    if (want_frame > 0 && want_frame < g_app.shp.Frame_Count()) {
        g_app.frame = want_frame;
    }
    {
        const ShpFrameInfo& f = g_app.shp.Frame_Info(g_app.frame);
        std::printf("渲染帧=%d  %dx%d flags=0x%X\n", g_app.frame, f.w, f.h, f.flags);
    }
    }   // end else（SHP 模式）

    // ---- 离屏模式：不开窗口，渲一帧回读就退出 ----
    if (offscreen) {
        std::printf("[3'] 离屏渲染（无窗口）\n");
        if (!g_app.r.Init_Offscreen(1024, 768)) {
            std::printf("[x] DX12 离屏初始化失败: %s\n", g_app.r.Last_Error());
            return 1;
        }
        Upload_Current_Frame();
        // 光栅化结果的自检信息。无窗口时这是唯一的判据来源：
        // 开阴影后画布必然变大（bbox 纳入了地面落点），阴影像素数 > 0。
        if (g_app.vxl.Limb_Count() > 0) {
            long opaque = 0;
            for (uint8_t v : g_app.vxl_idx) {
                if (v != 0) ++opaque;
            }
            std::printf("     体素画布 %dx%d 本体像素 %ld", g_app.vxl_w, g_app.vxl_h,
                        opaque);
            if (g_app.shadow.on && !g_app.vxl_shadow.empty()) {
                long sh = 0;
                long visible = 0;
                for (size_t i = 0; i < g_app.vxl_shadow.size(); ++i) {
                    if (g_app.vxl_shadow[i] == 0) continue;
                    ++sh;
                    if (g_app.vxl_idx[i] == 0) ++visible;   // 没被本体盖住的
                }
                std::printf("，阴影 %ld（露出 %ld，%.0f%% 被本体盖住）", sh, visible,
                            sh ? 100.0 * (sh - visible) / sh : 0.0);
            }
            std::printf("\n");
        }
        const float clear[4] = {0.05f, 0.05f, 0.08f, 1.0f};
        g_app.r.Request_Capture();
        g_app.r.Begin_Frame(clear);
        if (g_app.sprite >= 0) {
            g_app.r.Draw_Sprite(g_app.sprite, 8, 8, Map_Draw_Scale());
        }
        g_app.r.End_Frame();
        std::vector<uint8_t> rgba;
        int w = 0, h = 0;
        if (!g_app.r.Get_Capture(rgba, w, h)) {
            std::printf("[x] 画面回读失败\n");
            return 1;
        }
        const char* out = "build/frame.raw";
        FILE* f = std::fopen(out, "wb");
        if (!f) {
            std::printf("[x] 写不出 %s\n", out);
            return 1;
        }
        std::fwrite(rgba.data(), 1, rgba.size(), f);
        std::fclose(f);
        std::printf("[OK] 离屏渲染 %dx%d -> %s (%zu 字节)\n", w, h, out, rgba.size());
        return 0;
    }

    std::printf("[3] 创建窗口\n");
    const wchar_t* kCls = L"RA2ViewClass";
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = kCls;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, kCls, L"RA2 素材查看器 (DX12)", WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, kWinW, kWinH,
                                nullptr, nullptr, hInst, nullptr);
    if (!hwnd) {
        std::printf("[x] 创建窗口失败\n");
        return 1;
    }
    ShowWindow(hwnd, SW_SHOW);

    std::printf("[4] 初始化 DX12\n");
    if (!g_app.r.Init(hwnd, kWinW, kWinH)) {
        std::printf("[x] DX12 初始化失败: %s\n", g_app.r.Last_Error());
        return 1;
    }
    std::printf("[5] 上传精灵\n");
    Upload_Current_Frame();
    g_app.ok = true;

    std::printf("[6] 进入消息循环\n");
    std::printf("[OK] 窗口已打开。空格=下一帧  +/-=缩放  ESC=退出\n");

    // --frames N：只渲染 N 帧就退出。用于自动化验证"能不能画出东西"，
    // 平时不开窗口盯着。
    int max_frames = 0;
    {
        for (int i = 0; i < kMaxArgs; ++i) {
            if (std::strncmp(args[i], "--frames", 8) == 0) {
                max_frames = std::atoi(args[i] + 8);
            }
        }
    }
    if (max_frames > 0) {
        std::printf("[--frames %d] 渲染 %d 帧后自动退出\n", max_frames, max_frames);
    }

    MSG msg = {};
    int drawn = 0;
    while (msg.message != WM_QUIT) {
        if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            continue;
        }
        if (!g_app.ok) {
            continue;
        }
        const float clear[4] = {0.05f, 0.05f, 0.08f, 1.0f};
        g_app.r.Begin_Frame(clear);
        if (g_app.sprite >= 0) {
            g_app.r.Draw_Sprite(g_app.sprite,
                                8 + static_cast<int>(g_app.pan_x),
                                8 + static_cast<int>(g_app.pan_y),
                                Map_Draw_Scale());
        }
        if (max_frames > 0 && drawn + 1 >= max_frames) {
            g_app.r.Request_Capture();   // 最后一帧拷回 CPU
        }
        g_app.r.End_Frame();
        if (max_frames > 0 && ++drawn >= max_frames) {
            std::printf("[OK] 已渲染 %d 帧，DX12 链路通\n", drawn);
            std::vector<uint8_t> rgba;
            int w = 0, h = 0;
            if (g_app.r.Get_Capture(rgba, w, h)) {
                // 原始 RGBA 落盘：由 tools/framecheck.py 转成人能看的形式。
                const char* out = "build/frame.raw";
                FILE* f = std::fopen(out, "wb");
                if (f) {
                    std::fwrite(rgba.data(), 1, rgba.size(), f);
                    std::fclose(f);
                    std::printf("[OK] 已回读画面 %dx%d -> %s (%zu 字节)\n", w, h, out,
                                rgba.size());
                }
            } else {
                std::printf("[x] 画面回读失败\n");
            }
            break;
        }
    }
    return 0;
}
