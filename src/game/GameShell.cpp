// GameShell.cpp

#include "game/GameShell.h"

#include <cmath>
#include <cstdio>

namespace ra2 {
namespace {

// 界面配色。取的是原版 RA2 那套"深灰金属 + 阵营色点缀"的观感，
// 具体数值没有从 exe 里逆（原版界面是 PCX 贴图，不是纯色），
// 等界面贴图接进来再换成精灵。
constexpr float kUiBackdrop[4] = {0.16f, 0.16f, 0.18f, 1.0f};   // 边栏底
constexpr float kUiTopBar[4] = {0.10f, 0.10f, 0.12f, 1.0f};     // 顶栏底
constexpr float kUiEdge[4] = {0.35f, 0.35f, 0.38f, 1.0f};       // 分隔线
constexpr float kUiSlot[4] = {0.24f, 0.24f, 0.27f, 1.0f};       // 按钮格
constexpr float kUiGold[4] = {1.0f, 0.84f, 0.0f, 1.0f};         // 资金文字色
constexpr float kUiSelect[4] = {0.0f, 1.0f, 0.0f, 1.0f};        // 框选绿
constexpr float kUiRadarFog[4] = {0.05f, 0.05f, 0.06f, 1.0f};   // 未探索
constexpr float kUiRadarView[4] = {1.0f, 1.0f, 1.0f, 1.0f};     // 视野框

}  // namespace

// ---------------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------------

void Camera::Set_Viewport(int w, int h) {
    view_x_ = 0;
    view_y_ = kTopBarH;
    view_w_ = (w > kSidebarW) ? w - kSidebarW : 1;
    view_h_ = (h > kTopBarH) ? h - kTopBarH : 1;
}

void Camera::Set_World_Size(int w, int h) {
    world_w_ = w;
    world_h_ = h;
}

void Camera::Set(float x, float y) {
    x_ = x;
    y_ = y;
    Clamp();
}

void Camera::Scroll(float dx, float dy) {
    x_ += dx;
    y_ += dy;
    Clamp();
}

void Camera::Zoom_By(float factor) {
    const float old = scale_;
    scale_ *= factor;
    if (scale_ < 0.25f) scale_ = 0.25f;
    if (scale_ > 4.0f) scale_ = 4.0f;
    // 以视口中心为锚点缩放：中心对应的世界点不动
    const float cxw = x_ + view_w_ / (2.0f * old);
    const float cyw = y_ + view_h_ / (2.0f * old);
    x_ = cxw - view_w_ / (2.0f * scale_);
    y_ = cyw - view_h_ / (2.0f * scale_);
    Clamp();
}

void Camera::Clamp() {
    if (world_w_ <= 0 || world_h_ <= 0) {
        return;
    }
    // 允许的世界坐标范围：视口不能完全跑到世界外面
    const float vw = static_cast<float>(view_w_) / scale_;
    const float vh = static_cast<float>(view_h_) / scale_;
    if (vw >= world_w_) {
        x_ = (world_w_ - vw) * 0.5f;
    } else {
        if (x_ < -world_w_ * 0.25f) x_ = -world_w_ * 0.25f;
        if (x_ > world_w_ - vw + world_w_ * 0.25f) {
            x_ = world_w_ - vw + world_w_ * 0.25f;
        }
    }
    if (vh >= world_h_) {
        y_ = (world_h_ - vh) * 0.5f;
    } else {
        if (y_ < -world_h_ * 0.25f) y_ = -world_h_ * 0.25f;
        if (y_ > world_h_ - vh + world_h_ * 0.25f) {
            y_ = world_h_ - vh + world_h_ * 0.25f;
        }
    }
}

// ---------------------------------------------------------------------------
// GameShell
// ---------------------------------------------------------------------------

bool GameShell::Init(void* hwnd, int width, int height) {
    win_w_ = width;
    win_h_ = height;
    const bool ok = (hwnd == nullptr)
                        ? renderer_.Init_Offscreen(width, height)
                        : renderer_.Init(static_cast<HWND>(hwnd), width, height);
    if (!ok) {
        std::printf("[x] 渲染器初始化失败: %s\n", renderer_.Last_Error());
        return false;
    }
    camera_.Set_Viewport(width, height);
    ready_ = true;
    return true;
}

bool GameShell::Load_Map(const std::vector<std::string>& mix_paths,
                         const char* map_path, std::string* err) {
    screen_ = GameScreen::Loading;

    for (const std::string& p : mix_paths) {
        auto m = std::make_unique<MixFileClass>();
        if (!m->Open(p.c_str())) {
            if (err) *err = "打不开 " + p;
            return false;
        }
        roots_.push_back(m.get());
        mixes_.push_back(std::move(m));
    }

    if (!map_.Load_Path(map_path, err)) {
        return false;
    }

    // 阵营色表：rules.ini 的 [Colors]
    for (MixFileClass* m : roots_) {
        std::vector<uint8_t> ini = m->Read_Deep("rules.ini");
        if (ini.empty()) ini = m->Read_Deep("rulesmd.ini");
        if (ini.empty()) continue;
        IniFile rules;
        if (rules.Load(ini.data(), ini.size())) {
            remap_.Load_From_Ini(rules);
        }
        break;
    }

    if (!map_renderer_.Bind(roots_, map_, err)) {
        return false;
    }
    if (!map_renderer_.Render(map_, 0, 0, 0, 0, &terrain_rgba_, &terrain_w_,
                              &terrain_h_, err)) {
        return false;
    }
    terrain_sprite_ = renderer_.Upload_Sprite_RGBA(
        reinterpret_cast<const uint8_t*>(terrain_rgba_.data()), terrain_w_,
        terrain_h_);
    if (terrain_sprite_ < 0) {
        if (err) *err = "战场图上传失败";
        return false;
    }

    camera_.Set_World_Size(terrain_w_, terrain_h_);
    // 开局镜头对准地图中心 —— 原版是对准你的基地，等有基地/起始点解析了再改。
    // "中心"是让视口中心对上世界中心，所以要减掉半个视口（按当前缩放换算成世界单位）。
    const float half_vw = static_cast<float>(win_w_ - kSidebarW) * 0.5f / camera_.Scale();
    const float half_vh = static_cast<float>(win_h_ - kTopBarH) * 0.5f / camera_.Scale();
    camera_.Set(static_cast<float>(terrain_w_) * 0.5f - half_vw,
                static_cast<float>(terrain_h_) * 0.5f - half_vh);

    screen_ = GameScreen::Battle;
    return true;
}

void GameShell::Set_Viewport(int w, int h) {
    win_w_ = w;
    win_h_ = h;
    camera_.Set_Viewport(w, h);
}

void GameShell::Update(float dt) {
    if (!ready_) {
        return;
    }
    ++frame_;

    // 边缘滚动（鼠标贴到视口边上就平移）。原版就是这么做的。
    if (edge_scroll_ && screen_ == GameScreen::Battle) {
        const int margin = 8;
        const float speed = 600.0f * dt;
        int dx = 0, dy = 0;
        if (mouse_x_ >= 0 && mouse_x_ < win_w_ - kSidebarW) {
            if (mouse_x_ < margin) dx = -1;
            if (mouse_x_ > win_w_ - kSidebarW - margin) dx = 1;
        }
        if (mouse_y_ > kTopBarH && mouse_y_ < win_h_) {
            if (mouse_y_ < kTopBarH + margin) dy = -1;
            if (mouse_y_ > win_h_ - margin) dy = 1;
        }
        if (dx || dy) {
            camera_.Scroll(dx * speed, dy * speed);
        }
    }

    // 固定步长逻辑帧
    logic_accum_ += dt;
    int steps = 0;
    while (logic_accum_ >= kLogicDt && steps < 5) {
        ++logic_frame_;
        logic_accum_ -= kLogicDt;
        ++steps;
    }
}

void GameShell::Render() {
    if (!ready_) {
        return;
    }
    const float clear[4] = {0.02f, 0.02f, 0.03f, 1.0f};
    renderer_.Begin_Frame(clear);
    if (screen_ == GameScreen::Battle) {
        Draw_Battlefield();
        Draw_Top_Bar();
        Draw_Sidebar();
        Draw_Radar();
        Draw_Selection();
    }
    renderer_.End_Frame();
}

void GameShell::Draw_Battlefield() {
    if (terrain_sprite_ < 0) {
        return;
    }
    float sx = 0.0f, sy = 0.0f;
    camera_.World_To_Screen(0.0f, 0.0f, &sx, &sy);
    renderer_.Draw_Sprite(terrain_sprite_, static_cast<int>(sx),
                          static_cast<int>(sy), camera_.Scale());
}

void GameShell::Draw_Top_Bar() {
    renderer_.Draw_Rect(0, 0, win_w_, kTopBarH, kUiTopBar);
    renderer_.Draw_Rect(0, kTopBarH - 1, win_w_, 1, kUiEdge);
    // 资金条：先画一个占位色块，等字体/数字精灵接进来换成真数字。
    renderer_.Draw_Rect(12, 8, 96, 16, kUiGold);
    // 电力条：右侧
    renderer_.Draw_Rect(win_w_ - kSidebarW - 140, 8, 128, 16, kUiSlot);
}

void GameShell::Draw_Sidebar() {
    const int x = win_w_ - kSidebarW;
    renderer_.Draw_Rect(x, kTopBarH, kSidebarW, win_h_ - kTopBarH, kUiBackdrop);
    renderer_.Draw_Rect(x, kTopBarH, 1, win_h_ - kTopBarH, kUiEdge);

    // 建造按钮：4 列 × 4 行的网格，每格 32×32，间距 4。
    // 真实按钮列表要从 rules 读可建造项，这里先把骨架和命中区域定下来。
    const int pad = 8;
    const int slot = 32;
    const int gap = 4;
    const int cols = 4;
    const int grid_y = kTopBarH + pad;
    for (int i = 0; i < cols * 4; ++i) {
        const int col = i % cols;
        const int row = i / cols;
        const int bx = x + pad + col * (slot + gap);
        const int by = grid_y + row * (slot + gap);
        renderer_.Draw_Rect(bx, by, slot, slot, kUiSlot);
        renderer_.Draw_Rect_Outline(bx, by, slot, slot, kUiEdge, 1);
    }
}

void GameShell::Draw_Radar() {
    const int x = win_w_ - kSidebarW + 8;
    const int y = win_h_ - kRadarSize + 8;
    const int size = kRadarSize - 16;
    renderer_.Draw_Rect(x, y, size, size, kUiRadarFog);
    renderer_.Draw_Rect_Outline(x, y, size, size, kUiEdge, 1);

    // 当前视口在小地图上的位置
    if (terrain_w_ > 0 && terrain_h_ > 0) {
        const float vx = camera_.X() / static_cast<float>(terrain_w_);
        const float vy = camera_.Y() / static_cast<float>(terrain_h_);
        const float vw = static_cast<float>(win_w_ - kSidebarW) /
                         camera_.Scale() / static_cast<float>(terrain_w_);
        const float vh = static_cast<float>(win_h_ - kTopBarH) /
                         camera_.Scale() / static_cast<float>(terrain_h_);
        const int rx = x + static_cast<int>(vx * size);
        const int ry = y + static_cast<int>(vy * size);
        const int rw = (vw * size < 4.0f) ? 4 : static_cast<int>(vw * size);
        const int rh = (vh * size < 4.0f) ? 4 : static_cast<int>(vh * size);
        renderer_.Draw_Rect_Outline(rx, ry, rw, rh, kUiRadarView, 1);
    }
}

void GameShell::Draw_Selection() {
    if (!dragging_) {
        return;
    }
    const int x = (drag_x0_ < mouse_x_) ? drag_x0_ : mouse_x_;
    const int y = (drag_y0_ < mouse_y_) ? drag_y0_ : mouse_y_;
    const int w = (drag_x0_ < mouse_x_) ? mouse_x_ - drag_x0_ : drag_x0_ - mouse_x_;
    const int h = (drag_y0_ < mouse_y_) ? mouse_y_ - drag_y0_ : drag_y0_ - mouse_y_;
    renderer_.Draw_Rect_Outline(x, y, w, h, kUiSelect, 1);
}

// ---------------------------------------------------------------------------
// 输入
// ---------------------------------------------------------------------------

bool GameShell::Pick_Cell(int sx, int sy, int* cx, int* cy) const {
    if (terrain_w_ <= 0) {
        return false;
    }
    if (sx < 0 || sx >= win_w_ - kSidebarW || sy < kTopBarH || sy >= win_h_) {
        return false;
    }
    float wx = 0.0f, wy = 0.0f;
    camera_.Screen_To_World(static_cast<float>(sx), static_cast<float>(sy), &wx, &wy);
    MapRenderer::Canvas_To_Cell(map_renderer_.Origin_X(), map_renderer_.Origin_Y(),
                                static_cast<int>(wx), static_cast<int>(wy), cx, cy);
    if (*cx < 0 || *cy < 0 || *cx >= map_.Width() || *cy >= map_.Height()) {
        return false;
    }
    return true;
}

void GameShell::On_Key_Down(int vk, bool /*ctrl*/, bool /*shift*/) {
    if (vk >= 0 && vk < 256) {
        keys_down_[vk] = true;
    }
    const float step = 120.0f;
    switch (vk) {
        case VK_LEFT:  camera_.Scroll(-step, 0.0f); break;
        case VK_RIGHT: camera_.Scroll(step, 0.0f); break;
        case VK_UP:    camera_.Scroll(0.0f, -step); break;
        case VK_DOWN:  camera_.Scroll(0.0f, step); break;
        default: break;
    }
}

void GameShell::On_Mouse_Move(int x, int y) {
    mouse_x_ = x;
    mouse_y_ = y;
}

void GameShell::On_Mouse_Down(int button, int x, int y, bool /*shift*/) {
    mouse_x_ = x;
    mouse_y_ = y;
    if (button == 0) {
        dragging_ = true;
        drag_x0_ = x;
        drag_y0_ = y;
    }
}

void GameShell::On_Mouse_Up(int button, int x, int y) {
    mouse_x_ = x;
    mouse_y_ = y;
    if (button == 0 && dragging_) {
        dragging_ = false;
        int cx0 = 0, cy0 = 0, cx1 = 0, cy1 = 0;
        if (Pick_Cell(drag_x0_, drag_y0_, &cx0, &cy0) &&
            Pick_Cell(x, y, &cx1, &cy1)) {
            std::printf("框选 格 (%d,%d) -> (%d,%d)\n", cx0, cy0, cx1, cy1);
        }
    }
}

bool GameShell::Offscreen_Frame(std::vector<uint8_t>* rgba, int* w, int* h) {
    if (!ready_) {
        return false;
    }
    renderer_.Request_Capture();
    Render();
    return renderer_.Get_Capture(*rgba, *w, *h);
}

}  // namespace ra2
