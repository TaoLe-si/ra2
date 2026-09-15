// GameMain.cpp -- ra2game.exe：真正"打开就是游戏"的入口
//
// 和 ra2view.exe 的区别：
//   ra2view 是**素材查看器**（给一个 ID 看一张图），ra2game 是**游戏** ——
//   有主循环、有界面、有鼠标键盘、有逻辑帧。
//
// 用法：
//   ra2game.exe <ra2.mix> [--addmix <ra2md.mix>] [--map <地图.mmx>]
//               [--offscreen [out.raw]]
//   不传 --map 时挑一张内置地图（先取挂上来的第一个 MAPS*.MIX 里的第一张，
//   取不到就报出来，不会假装成功）。

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "game/GameShell.h"

namespace {

constexpr int kMaxArgs = 32;

int Run_Offscreen(ra2::GameShell& game, const char* out_path, int warm_frames) {
    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    // 精灵是"用到才烘"的，每帧只补几张（见 SpriteCache::Reset_Budget）。
    // 只渲一帧的话画面上绝大多数单位还是占位色块，出不了能看的截图。
    // 所以先空跑几帧把精灵补齐，再取帧。
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < warm_frames; ++i) {
        game.Update(1.0f / 60.0f);
        game.Render();
    }
    if (warm_frames > 0) {
        const double ms =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - t0).count();
        std::printf("[perf] 预热 %d 帧共 %.1f ms（%.1f ms/帧）\n", warm_frames, ms,
                    ms / warm_frames);
    }
    if (!game.Offscreen_Frame(&rgba, &w, &h)) {
        std::printf("[x] 离屏取帧失败\n");
        return 1;
    }
    FILE* f = std::fopen(out_path, "wb");
    if (!f) {
        std::printf("[x] 写不出 %s\n", out_path);
        return 1;
    }
    std::fwrite(rgba.data(), 1, rgba.size(), f);
    std::fclose(f);
    std::printf("[OK] 离屏一帧 %dx%d -> %s (%zu 字节)\n", w, h, out_path,
                rgba.size());
    return 0;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    static ra2::GameShell* g = nullptr;
    switch (msg) {
        case WM_CREATE: {
            const CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            g = static_cast<ra2::GameShell*>(cs->lpCreateParams);
            return 0;
        }
        case WM_KEYDOWN:
            if (g) {
                const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                g->On_Key_Down(static_cast<int>(wp), ctrl, shift);
            }
            if (wp == VK_ESCAPE) {
                PostQuitMessage(0);
            }
            return 0;
        case WM_KEYUP:
            // 松开要通知外壳：方向键是"按住就一直卷屏"的，
            // 不把抬起记下来，方向键一按就再也停不下来。
            if (g) {
                g->On_Key_Up(static_cast<int>(wp));
                if (wp == VK_OEM_PLUS || wp == VK_ADD) {
                    g->On_Key_Down(VK_ADD, false, false);
                }
            }
            return 0;
        case WM_MOUSEMOVE:
            if (g) {
                g->On_Mouse_Move(static_cast<short>(LOWORD(lp)),
                                 static_cast<short>(HIWORD(lp)));
            }
            return 0;
        case WM_LBUTTONDOWN:
            if (g) {
                SetCapture(hwnd);
                g->On_Mouse_Down(0, static_cast<short>(LOWORD(lp)),
                                 static_cast<short>(HIWORD(lp)),
                                 (GetKeyState(VK_SHIFT) & 0x8000) != 0);
            }
            return 0;
        case WM_LBUTTONUP:
            if (g) {
                ReleaseCapture();
                g->On_Mouse_Up(0, static_cast<short>(LOWORD(lp)),
                               static_cast<short>(HIWORD(lp)));
            }
            return 0;
        case WM_RBUTTONDOWN:
            if (g) {
                g->On_Mouse_Down(1, static_cast<short>(LOWORD(lp)),
                                 static_cast<short>(HIWORD(lp)),
                                 (GetKeyState(VK_SHIFT) & 0x8000) != 0);
            }
            return 0;
        case WM_RBUTTONUP:
            if (g) {
                g->On_Mouse_Up(1, static_cast<short>(LOWORD(lp)),
                               static_cast<short>(HIWORD(lp)));
            }
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR cmdline, int) {
    // 拆命令行。宽->窄，按空格切，支持引号。
    char args[kMaxArgs][MAX_PATH] = {};
    int argc = 0;
    {
        std::wstring a = cmdline ? cmdline : L"";
        std::string s(a.begin(), a.end());
        std::string cur;
        bool q = false;
        for (char c : s) {
            if (c == '"') {
                q = !q;
            } else if ((c == ' ' || c == '\t') && !q) {
                if (!cur.empty() && argc < kMaxArgs) {
                    std::snprintf(args[argc++], MAX_PATH, "%s", cur.c_str());
                }
                cur.clear();
            } else {
                cur += c;
            }
        }
        if (!cur.empty() && argc < kMaxArgs) {
            std::snprintf(args[argc++], MAX_PATH, "%s", cur.c_str());
        }
    }

    std::vector<std::string> mixes;
    std::string map_path;
    bool offscreen = false;
    bool selftest = false;
    bool vxlgpu = false;
    int warm_frames = 0;
    std::string out_path = "build/game.raw";
    for (int i = 0; i < argc; ++i) {
        if (std::strcmp(args[i], "--addmix") == 0 && i + 1 < argc) {
            mixes.push_back(args[++i]);
        } else if (std::strcmp(args[i], "--map") == 0 && i + 1 < argc) {
            map_path = args[++i];
        } else if (std::strcmp(args[i], "--selftest") == 0) {
            selftest = true;
            offscreen = true;
        } else if (std::strcmp(args[i], "--vxlgpu") == 0) {
            vxlgpu = true;
            offscreen = true;
        } else if (std::strncmp(args[i], "--offscreen", 11) == 0) {
            offscreen = true;
        } else if (std::strcmp(args[i], "--out") == 0 && i + 1 < argc) {
            out_path = args[++i];
        } else if (std::strcmp(args[i], "--frames") == 0 && i + 1 < argc) {
            warm_frames = std::atoi(args[++i]);
        } else if (args[i][0] == '-') {
            // 未知开关，忽略
        } else {
            mixes.push_back(args[i]);
        }
    }
    if (mixes.empty()) {
        mixes.push_back("D:\\westwood\\RA2YR\\ra2.mix");
    }

    ra2::GameShell game;

    if (offscreen) {
        if (!game.Init(nullptr, 1024, 768)) {
            return 1;
        }
        if (map_path.empty()) {
            std::printf("[x] 离屏模式要给 --map（还没有内置地图列表）\n");
            return 1;
        }
        std::string err;
        if (!game.Load_Map(mixes, map_path.c_str(), &err)) {
            std::printf("[x] 载入失败: %s\n", err.c_str());
            return 1;
        }
        if (vxlgpu) {
            return game.Self_Test_Voxel_GPU() ? 0 : 1;
        }
        if (selftest) {
            return game.Self_Test() ? 0 : 1;
        }
        return Run_Offscreen(game, out_path.c_str(), warm_frames);
    }

    // ---- 窗口模式 ----
    const wchar_t* kClass = L"RA2GameWindow";
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = kClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassW(&wc)) {
        std::printf("[x] 注册窗口类失败\n");
        return 1;
    }

    const int w = 1024, h = 768;
    HWND hwnd = CreateWindowExW(0, kClass, L"Red Alert 2",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
                                    WS_MINIMIZEBOX,
                                CW_USEDEFAULT, CW_USEDEFAULT, w, h, nullptr,
                                nullptr, hInst, &game);
    if (!hwnd) {
        std::printf("[x] 创建窗口失败\n");
        return 1;
    }
    if (!game.Init(hwnd, w, h)) {
        return 1;
    }

    if (!map_path.empty()) {
        std::string err;
        if (!game.Load_Map(mixes, map_path.c_str(), &err)) {
            std::printf("[x] 载入失败: %s\n", err.c_str());
            return 1;
        }
    } else {
        std::printf("[i] 没给 --map，先进空战场（主菜单还没做）\n");
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg = {};
    DWORD last = GetTickCount();
    while (msg.message != WM_QUIT) {
        if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            continue;
        }
        const DWORD now = GetTickCount();
        float dt = static_cast<float>(now - last) / 1000.0f;
        last = now;
        if (dt > 0.25f) {
            dt = 0.25f;   // 切走再切回来别一次补几百帧
        }
        game.Update(dt);
        game.Render();
    }
    return 0;
}
