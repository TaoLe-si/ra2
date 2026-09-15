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

void Upload_Current_Frame() {
    const ShpFrameInfo& f = g_app.shp.Frame_Info(g_app.frame);
    g_app.r.Set_Palette(g_app_frame_palette);
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

    // 命令行（宽字符 -> 多字节，够用）
    char args[4][MAX_PATH] = {};
    {
        std::wstring w(cmdline ? cmdline : L"");
        std::string a(w.begin(), w.end());
        // 简单按空格切，路径带引号就去掉引号
        int n = 0, k = 0;
        std::string cur;
        bool q = false;
        for (char c : a) {
            if (c == '"') {
                q = !q;
            } else if ((c == ' ' || c == '\t') && !q) {
                if (!cur.empty() && n < 4) {
                    std::snprintf(args[n++], MAX_PATH, "%s", cur.c_str());
                }
                cur.clear();
            } else {
                cur += c;
            }
        }
        if (!cur.empty() && n < 4) {
            std::snprintf(args[n++], MAX_PATH, "%s", cur.c_str());
        }
    }
    const char* mix_path = args[0][0] ? args[0] : "D:\\westwood\\RA2YR\\ra2md.mix";
    const char* shp_name = args[1];
    const char* pal_name = args[2];
    // 剩下的参数里找开关（--offscreen / --framesN）
    bool offscreen = false;
    for (int i = 0; i < 4; ++i) {
        if (std::strncmp(args[i], "--offscreen", 11) == 0) {
            offscreen = true;
        }
    }

    std::printf("[1] 打开 MIX: %s\n", mix_path);
    if (!g_app.mix.Open(mix_path)) {
        std::printf("[x] MIX 打不开: %s\n", mix_path);
        return 1;
    }
    std::printf("MIX  %s  条目=%d\n", mix_path, g_app.mix.Count());

    std::printf("[2] 载入素材\n");
    int best_i = -1;
    if (!Load_Asset(g_app.mix, shp_name, pal_name, &g_app.shp, &g_app.pal_name, &best_i)) {
        return 1;
    }
    const ShpFrameInfo& f0 = g_app.shp.Frame_Info(0);
    std::printf("SHP  %dx%d 帧=%d  帧0 flags=0x%X 调色板=%s\n",
                g_app.shp.Width(), g_app.shp.Height(), g_app.shp.Frame_Count(),
                f0.flags, g_app.pal_name.c_str());

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
        for (int i = 0; i < 4; ++i) {
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
