// GameMain.cpp -- ra2game.exe：真正"打开就是游戏"的入口
//
// 和 ra2view.exe 的区别：
//   ra2view 是**素材查看器**（给一个 ID 看一张图），ra2game 是**游戏** ——
//   有主循环、有界面、有鼠标键盘、有逻辑帧。
//
// 用法：
//   ra2game.exe --gamedir <游戏目录> [--map <地图>] [--offscreen [out.raw]]
//   ra2game.exe <ra2.mix> [--addmix <ra2md.mix>] [--map <地图>]
//               [--offscreen [out.raw]]
//   不传 --map 时挑一张地图：--gamedir 走安装里的松散 Maps/ 目录
//   （见 GameInstall::Find_First_Map），否则找同目录的 Arena.mmx。
//   取不到就报出来，不会假装成功。

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "core/GameVersion.h"
#include "game/GameShell.h"

namespace {

constexpr int kMaxArgs = 32;

bool File_Exists(const std::string& p) {
    FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) {
        return false;
    }
    std::fclose(f);
    return true;
}

std::string Dir_Of(const std::string& p) {
    const size_t s = p.find_last_of("\\/");
    if (s == std::string::npos) {
        return std::string();
    }
    return p.substr(0, s + 1);
}

void Add_Mix_If_Missing(std::vector<std::string>* mixes, const std::string& path) {
    if (mixes == nullptr || path.empty() || !File_Exists(path)) {
        return;
    }
    for (const std::string& m : *mixes) {
        if (_stricmp(m.c_str(), path.c_str()) == 0) {
            return;
        }
    }
    mixes->push_back(path);
}

int Run_Offscreen(ra2::GameShell& game, const char* out_path, int warm_frames) {
    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    // 进图已经把精灵预热完了，这里再渲几帧只是让 GPU 命令队列走一遍。
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
    // 重定向到文件时 stdout 是块缓冲 —— 窗口模式的调试日志要等退出才落盘。
    setvbuf(stdout, nullptr, _IONBF, 0);
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
    std::string gamedir;
    bool offscreen = false;
    bool selftest = false;
    bool menu_only = false;
    std::string aud_test;
    bool vxlgpu = false;
    bool grid_debug = false;
    std::string dump_map_path;
    int dump_tile_tile = -1, dump_tile_sub = -1;
    std::string dump_tile_path;
    int warm_frames = 0;
    std::string out_path = "build/game.raw";
    for (int i = 0; i < argc; ++i) {
        if (std::strcmp(args[i], "--gamedir") == 0 && i + 1 < argc) {
            gamedir = args[++i];
        } else if (std::strcmp(args[i], "--addmix") == 0 && i + 1 < argc) {
            mixes.push_back(args[++i]);
        } else if (std::strcmp(args[i], "--map") == 0 && i + 1 < argc) {
            map_path = args[++i];
        } else if (std::strcmp(args[i], "--menu") == 0) {
            menu_only = true;
        } else if (std::strcmp(args[i], "--audtest") == 0 && i + 1 < argc) {
            aud_test = args[++i];
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
        } else if (std::strcmp(args[i], "--grid") == 0) {
            grid_debug = true;
        } else if (std::strcmp(args[i], "--dumptmap") == 0 && i + 1 < argc) {
            dump_map_path = args[++i];
        } else if (std::strcmp(args[i], "--dumptile") == 0 && i + 2 < argc) {
            dump_tile_tile = std::atoi(args[++i]);
            dump_tile_sub = std::atoi(args[++i]);
        } else if (std::strcmp(args[i], "--dumptileout") == 0 && i + 1 < argc) {
            dump_tile_path = args[++i];
        } else if (args[i][0] == '-') {
            // 未知开关，忽略
        } else {
            mixes.push_back(args[i]);
        }
    }
    // ---- --gamedir：按游戏自己的挂载顺序挂整套素材包 ----
    // 硬编码 "D:\westwood\RA2YR\ra2.mix" 那条默认路径在这台机器上不存在，
    // 表现是"打开就报打不开 ra2.mix"。真实安装用 GameInstall::Resolve 认，
    // 顺带拿到宽松地图目录，用来挑默认图。
    ra2::GamePaths gp;
    bool have_gp = false;
    if (!gamedir.empty()) {
        const std::vector<ra2::GameVersion> vs =
            ra2::GameInstall::Detect_Installed(gamedir.c_str());
        if (vs.empty()) {
            std::printf("[x] %s 里没认出 RA2 / YR 安装\n", gamedir.c_str());
            return 1;
        }
        std::string missing;
        // Detect_Installed 先 RA2 后 YR：取最后一个 = 有 YR 就优先 YR。
        if (!ra2::GameInstall::Resolve(gamedir.c_str(), vs.back(), &gp,
                                       &missing)) {
            std::printf("[x] 关键文件缺失: %s\n", missing.c_str());
            return 1;
        }
        ra2::GameInstall::Dump(gp);
        mixes = gp.mixes;
        have_gp = true;
    }
    if (mixes.empty()) {
        mixes.push_back("D:\\westwood\\RA2YR\\ra2.mix");
    }
    {
        const std::string dir = Dir_Of(mixes.front());
        if (!dir.empty()) {
            Add_Mix_If_Missing(&mixes, dir + "ra2md.mix");
            Add_Mix_If_Missing(&mixes, dir + "language.mix");
            Add_Mix_If_Missing(&mixes, dir + "langmd.mix");
            // expandmd 只补这一个：老路径本来就是"给两个包就能跑"的用法。
            // 要完整挂载请走 --gamedir（那边枚举 expandmd01..99）。
            Add_Mix_If_Missing(&mixes, dir + "expandmd01.mix");
            // 背景音乐：THEME.MIX（16 曲）/thememd.mix（10 曲），
            // TS 老格式明文 MIX（逐字节验算过——这是 RE）。
            Add_Mix_If_Missing(&mixes, dir + "THEME.MIX");
            Add_Mix_If_Missing(&mixes, dir + "thememd.mix");
        }
        // --gamedir 时默认图取安装里的第一张松散地图 —— 本装 root 下
        // 一个地图归档都没有，地图全在 Maps/ 的子目录里。
        if (map_path.empty() && have_gp) {
            map_path = ra2::GameInstall::Find_First_Map(gp);
            if (!map_path.empty()) {
                std::printf("默认地图: %s\n", map_path.c_str());
            } else {
                std::printf("[!] %s 下没找到任何 .map/.mmx/.yro\n",
                            gamedir.c_str());
            }
        }
        // 没给 --map 时默认 Arena（本地安装里有），才能直接开窗口进战场。
        if (map_path.empty() && !dir.empty()) {
            const std::string cand = dir + "Arena.mmx";
            DWORD attr = GetFileAttributesA(cand.c_str());
            if (attr != INVALID_FILE_ATTRIBUTES &&
                (attr & FILE_ATTRIBUTE_DIRECTORY) == 0) {
                // 有 Arena 时留给主菜单 SinglePlayer；离屏/自检仍要显式 --map。
                if (offscreen) {
                    map_path = cand;
                }
            }
        }
    }

    ra2::GameShell game;

    if (!aud_test.empty()) {
        // --audtest <文件名>：读一个 AUD、按逆向出的真格式解码、落 WAV。
        std::vector<std::string> m2;
        for (int i = 0; i < argc; ++i) {
            if (args[i][0] != '-') {
                m2.push_back(args[i]);
            }
        }
        ra2::GameShell gs;
        if (!gs.Init(nullptr, 256, 256)) {
            return 1;
        }
        return gs.Test_Aud_Decode(m2, aud_test.c_str()) ? 0 : 1;
    }

    if (menu_only) {
        // --menu：离屏渲一帧主菜单（无窗口复现菜单观感用）。
        if (!game.Init(nullptr, 1024, 768)) {
            return 1;
        }
        std::string err;
        if (!game.Enter_Title_Menu(mixes, nullptr, &err)) {
            std::printf("[x] 主菜单失败: %s\n", err.c_str());
            return 1;
        }
        return Run_Offscreen(game, out_path.c_str(), warm_frames);
    }

    if (offscreen) {
        if (!game.Init(nullptr, 1024, 768)) {
            return 1;
        }
        game.Set_Grid_Debug(grid_debug);
        if (map_path.empty()) {
            std::printf("[x] 离屏模式要给 --map（还没有内置地图列表）\n");
            return 1;
        }
        std::string err;
        if (!game.Load_Map(mixes, map_path.c_str(), &err)) {
            std::printf("[x] 载入失败: %s\n", err.c_str());
            return 1;
        }
        if (!dump_map_path.empty()) {
            game.Dump_Terrain_RGBA(dump_map_path.c_str());
        }
        if (dump_tile_tile >= 0 && !dump_tile_path.empty()) {
            game.Dump_Tile_RGBA(dump_tile_tile, dump_tile_sub, dump_tile_path.c_str());
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

    // 客户区 = 1024×768（外框加标题栏/边框）。把外框尺寸当客户区会让
    // 鼠标坐标（客户区 ~1008×729）与渲染坐标（1024×768）错位——按钮
    // 画在哪和点在哪不是同一处，越靠下偏得越多，表现为"点了没反应"。
    const int w = 1024, h = 768;
    RECT rc = {0, 0, w, h};
    AdjustWindowRect(&rc,
                     WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                     FALSE);
    HWND hwnd = CreateWindowExW(0, kClass, L"Red Alert 2",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
                                    WS_MINIMIZEBOX,
                                CW_USEDEFAULT, CW_USEDEFAULT,
                                rc.right - rc.left, rc.bottom - rc.top,
                                nullptr, nullptr, hInst, &game);
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
        std::string skirmish;
        const std::string dir = Dir_Of(mixes.front());
        if (!dir.empty()) {
            const std::string cand = dir + "Arena.mmx";
            DWORD attr = GetFileAttributesA(cand.c_str());
            if (attr != INVALID_FILE_ATTRIBUTES &&
                (attr & FILE_ATTRIBUTE_DIRECTORY) == 0) {
                skirmish = cand;
            }
        }
        std::string err;
        if (!game.Enter_Title_Menu(mixes, skirmish.c_str(), &err)) {
            std::printf("[x] 主菜单失败: %s\n", err.c_str());
            return 1;
        }
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg = {};
    DWORD last = GetTickCount();
    while (msg.message != WM_QUIT) {
        if (game.Exit_Requested()) {
            PostQuitMessage(0);
        }
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
