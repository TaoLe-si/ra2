// ViewerMain.cpp -- RA2 素材查看器（Win32 + DX12）
//
// 全量还原的第一个"看得见"的里程碑：
//   加密 MIX -> SHP(TS) 解码 -> 调色板 -> DX12 上屏
//
// 用法：
//   ra2view.exe <mix路径> [SHP名] [PAL名]
//   不带名字时自动挑归档里最大的那个 SHP，调色板用帧表 FrameColor 自动匹配。
//
// 操作： 空格/点击 = 下一帧；ESC = 退出；滚轮/+- = 缩放。

#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

#include "gfx/Palette.h"
#include "gfx/ShpFile.h"
#include "gfx/TmpFile.h"
#include "gfx/dx12/Dx12Renderer.h"
#include "io/FileSystem.h"

using namespace ra2;

namespace {

constexpr int kWinW = 1024;
constexpr int kWinH = 768;

struct App {
    MixFileClass mix;
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

    Dx12Renderer r;
    bool ok = false;
};

App g_app;
Palette g_app_frame_palette;   ///< 当前帧配的调色板

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

void Upload_Current_Frame() {
    g_app.r.Set_Palette(g_app_frame_palette);
    if (!g_app.terrain.empty()) {
        g_app.sprite = g_app.r.Upload_Sprite(g_app.terrain.data(),
                                             g_app.terrain_w, g_app.terrain_h);
        return;
    }
    const ShpFrameInfo& f = g_app.shp.Frame_Info(g_app.frame);
    g_app.sprite = g_app.r.Upload_Sprite(g_app.shp.Frame_Pixels(g_app.frame).data(), f.w, f.h);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) {
                PostQuitMessage(0);
            } else if (wp == VK_SPACE || wp == VK_RIGHT) {
                if (g_app.shp.Frame_Count() > 0) {
                    g_app.frame = (g_app.frame + 1) % g_app.shp.Frame_Count();
                    Upload_Current_Frame();
                }
            } else if (wp == VK_LEFT) {
                if (g_app.shp.Frame_Count() > 0) {
                    g_app.frame = (g_app.frame + g_app.shp.Frame_Count() - 1) %
                                  g_app.shp.Frame_Count();
                    Upload_Current_Frame();
                }
            } else if (wp == VK_OEM_PLUS || wp == VK_ADD) {
                g_app.scale *= 1.25f;
            } else if (wp == VK_OEM_MINUS || wp == VK_SUBTRACT) {
                g_app.scale /= 1.25f;
                if (g_app.scale < 0.05f) {
                    g_app.scale = 0.05f;
                }
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
    constexpr int kMaxArgs = 8;
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
        if (!cur.empty() && n < kMaxArgs) {
            std::snprintf(args[n++], MAX_PATH, "%s", cur.c_str());
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
    int grid = 15;
    bool random_pick = false;
    const char* tmp_pal_name = "TEMPERAT.PAL";
    for (int i = 0; i < kMaxArgs; ++i) {
        if (std::strcmp(args[i], "--tmp") == 0 && i + 1 < kMaxArgs && args[i + 1][0]) {
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
        }
    }
    if (grid < 1 || grid > 64) {
        grid = 15;
    }
    if (tmp_arch >= 0) {
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
        const float clear[4] = {0.05f, 0.05f, 0.08f, 1.0f};
        g_app.r.Request_Capture();
        g_app.r.Begin_Frame(clear);
        if (g_app.sprite >= 0) {
            g_app.r.Draw_Sprite(g_app.sprite, 8, 8, 1.0f);
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
            g_app.r.Draw_Sprite(g_app.sprite, 8, 8, g_app.scale);
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
