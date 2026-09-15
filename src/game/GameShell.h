// GameShell.h -- 游戏外壳：状态机 + 相机 + 界面布局
//
// 目标不是"能看素材"，是**打开 exe 就是原版那个游戏**：主菜单、载入、
// 战场、侧栏、小地图、框选、右键命令、快捷键。素材层和渲染层已经就位
// （MIX/SHP/TMP/VXL/地图/光影），这一层把它们组织成一个可玩的壳。
//
// 【分辨率与布局】照原版 800×600 的观感：
//   顶部资源条  y = 0..32，整宽
//   主视图      x = 0..W-kSidebarW, y = kTopBarH..H
//   右侧边栏    x = W-kSidebarW..W，宽 160
//   小地图      边栏底部，160×160
// 原版更高分辨率是同一套布局按比例放大，不是换结构。
//
// 【逻辑帧】原版是 **15 FPS 逻辑帧**（每帧 1/15 秒）配渲染插值。
// 锁步联机要求逻辑帧严格定步，所以这里也用固定步长累加器，
// 见 Update()。渲染每帧都跑，逻辑可能跑 0 次或多次。

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "data/Ini.h"
#include "gfx/RemapTable.h"
#include "gfx/dx12/Dx12Renderer.h"
#include "io/FileSystem.h"
#include "map/MapFile.h"
#include "map/MapRenderer.h"

namespace ra2 {

/// 原版界面尺寸（800×600 基准）。
constexpr int kTopBarH = 32;     ///< 顶部资源条高度
constexpr int kSidebarW = 160;   ///< 右侧边栏宽度
constexpr int kRadarSize = 160;  ///< 小地图边长

/// 逻辑帧率。原版 RA2 是 15 FPS 逻辑帧。
constexpr int kLogicFps = 15;
constexpr float kLogicDt = 1.0f / static_cast<float>(kLogicFps);

enum class GameScreen {
    Menu,      ///< 主菜单
    Loading,   ///< 载入中
    Battle,    ///< 战场（主要状态）
};

/// 等距相机。
///
/// 战场先整张铺成一张大图（MapRenderer::Render），相机就是"这张图的
/// 平移 + 缩放"。后面要支持局部刷新和逐格剔除时再换成按需铺图，
/// 但屏幕 <-> 世界的换算接口不变，上层不用改。
class Camera {
public:
    void Set_Viewport(int w, int h);
    void Set_World_Size(int w, int h);

    float X() const noexcept { return x_; }
    float Y() const noexcept { return y_; }
    float Scale() const noexcept { return scale_; }
    void Set(float x, float y);
    void Scroll(float dx, float dy);
    void Zoom_By(float factor);
    void Clamp();

    /// 世界（画布像素）-> 屏幕。屏幕坐标原点在窗口左上角。
    void World_To_Screen(float wx, float wy, float* sx, float* sy) const {
        *sx = (wx - x_) * scale_ + view_x_;
        *sy = (wy - y_) * scale_ + view_y_;
    }
    /// 屏幕 -> 世界。
    void Screen_To_World(float sx, float sy, float* wx, float* wy) const {
        *wx = (sx - view_x_) / scale_ + x_;
        *wy = (sy - view_y_) / scale_ + y_;
    }

private:
    float x_ = 0.0f, y_ = 0.0f;      ///< 视口左上角对应的世界坐标
    float scale_ = 1.0f;
    int view_x_ = 0, view_y_ = 0;    ///< 视口在窗口里的位置
    int view_w_ = 0, view_h_ = 0;
    int world_w_ = 0, world_h_ = 0;
};

class GameShell {
public:
    /// 初始化渲染器。hwnd 为空走离屏（自检用）。
    bool Init(void* hwnd, int width, int height);

    /// 挂载 MIX 并载入地图。失败时 err 里是人话。
    bool Load_Map(const std::vector<std::string>& mix_paths, const char* map_path,
                  std::string* err);

    /// 每帧调用。dt 是真实经过的秒数，内部按 kLogicDt 切成逻辑帧。
    void Update(float dt);
    /// 画一帧。
    void Render();

    // ---- 输入。由窗口过程转发进来 ----
    void On_Key_Down(int vk, bool ctrl, bool shift);
    void On_Mouse_Move(int x, int y);
    void On_Mouse_Down(int button, int x, int y, bool shift);
    void On_Mouse_Up(int button, int x, int y);

    void Set_Viewport(int w, int h);

    GameScreen Screen() const noexcept { return screen_; }
    int Frame() const noexcept { return frame_; }
    int Logic_Frame() const noexcept { return logic_frame_; }

    /// 离屏自检用。
    void* Renderer() { return &renderer_; }
    bool Offscreen_Frame(std::vector<uint8_t>* rgba, int* w, int* h);

private:
    void Update_Camera(float dt);
    void Draw_Battlefield();
    void Draw_Top_Bar();
    void Draw_Sidebar();
    void Draw_Radar();
    void Draw_Selection();

    /// 屏幕坐标 -> 地图格子。落在视口外或地图外返回 false。
    bool Pick_Cell(int sx, int sy, int* cx, int* cy) const;

    GameScreen screen_ = GameScreen::Battle;

    // 素材
    std::vector<std::unique_ptr<MixFileClass>> mixes_;
    std::vector<MixFileClass*> roots_;
    MapFile map_;
    MapRenderer map_renderer_;
    std::vector<uint32_t> terrain_rgba_;
    int terrain_w_ = 0, terrain_h_ = 0;
    int terrain_sprite_ = -1;
    RemapTable remap_;
    int player_color_ = 5;   ///< [Colors] 下标，5 = DarkRed（苏军红）

    // 状态
    Camera camera_;
    int win_w_ = 800, win_h_ = 600;
    int frame_ = 0;
    int logic_frame_ = 0;
    float logic_accum_ = 0.0f;

    // 输入状态
    int mouse_x_ = 0, mouse_y_ = 0;
    bool dragging_ = false;
    int drag_x0_ = 0, drag_y0_ = 0;
    bool edge_scroll_ = true;
    bool keys_down_[256] = {};

    Dx12Renderer renderer_;
    bool ready_ = false;
};

}  // namespace ra2
