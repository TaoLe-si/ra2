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
#include "game/World.h"
#include "gfx/ObjectSprite.h"
#include "gfx/RemapTable.h"
#include "gfx/dx12/Dx12Renderer.h"
#include "io/FileSystem.h"
#include "map/MapFile.h"
#include "map/MapRenderer.h"

namespace ra2 {

/// 原版界面尺寸（800×600 基准）。
///
/// 【为什么是 0 和 168】不是估的：原版侧栏贴图 SIDE1/SIDE2/SIDE3/RADAR/CREDITS
/// 全部是 **168 宽**（tools/uidump.py 量的），所以侧栏就是 168。
/// 而且原版**没有顶部资源条** —— 资金/电力都在侧栏里，屏幕上半部分全是战场。
/// 之前那个 32 像素的顶栏是我自己加的，会把战场压掉一条。
constexpr int kTopBarH = 0;      ///< 无顶栏（保留常量，相机换算还在用）
constexpr int kSidebarW = 168;   ///< 右侧边栏宽度（= 原版贴图宽度）
constexpr int kRadarSize = 168;  ///< 小地图外框边长（RADAR.SHP 是 168×110）

/// 逻辑帧率。原版 RA2 是 15 FPS 逻辑帧。
constexpr int kLogicFps = 15;
constexpr float kLogicDt = 1.0f / static_cast<float>(kLogicFps);

enum class GameScreen {
    Menu,      ///< 主菜单
    Loading,   ///< 载入中
    Battle,    ///< 战场（主要状态）
};

/// 一件原版界面贴图（SIDE1 / TAB00 / RADAR …）。
///
/// 它们是**索引色 SHP + SIDEBAR.PAL**，但一律烘成 RGBA8 再上传：
/// 渲染器只有一张全局调色板（索引色精灵共用），侧栏件和地形/单位会互相顶掉，
/// 每帧来回 Set_Palette 又要多一次等 GPU。这些件本来就是静态的，烘一次最省。
struct UiPiece {
    int w = 0, h = 0;
    int frames = 0;
    std::vector<int> sprite;   ///< 每帧一个渲染器精灵句柄
    bool ok() const noexcept { return frames > 0; }
    /// 取第 i 帧的精灵句柄，越界就取模（动画循环用）。
    int Frame(int i) const {
        if (frames <= 0) {
            return -1;
        }
        return sprite[static_cast<size_t>(i) % static_cast<size_t>(frames)];
    }
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
    void On_Key_Up(int vk);
    /// M/N 循环选下一个/上一个单位。
    void Cycle_Selection(int dir);
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

    int Object_Count() const noexcept { return world_.Count(); }
    int Selected_Count() const noexcept { return world_.Selected_Count(); }
    const World& World_() const noexcept { return world_; }
    World& Mutable_World() noexcept { return world_; }

    /// 不开窗口也能验证"操作"对不对：选中 / 下令 / 推进逻辑帧 / 编队 / 热键。
    /// 返回 false 表示有不通过的判据。
    bool Self_Test();

    /// 体素管线自检：GPU 光栅化 vs CPU 软光栅，逐像素比。
    ///
    /// 判据很硬：GPU 路径是新写的，必须证明它和已经验过的 CPU 路径出一样的图，
    /// 否则"解码交给 DX12"就只是把 bug 搬了个地方。
    bool Self_Test_Voxel_GPU();

    /// 建造页签：0=建筑 1=防御 2=步兵 3=车辆（对应热键 Q/W/E/R）。
    int Sidebar_Tab() const noexcept { return sidebar_tab_; }
    /// 当前挂起的光标命令（K 修理 / L 变卖）。
    int Cursor_Mode() const noexcept { return cursor_mode_; }
    int Objects_Drawn() const noexcept { return objects_drawn_; }

    /// 诊断：把格子中心网和对象落点画出来（`--grid`）。
    /// "对象摆放位置乱"这种问题光看成品图分不清是坐标错还是压盖顺序错，
    /// 把格心画出来一眼就能对。
    void Set_Grid_Debug(bool v) noexcept { grid_debug_ = v; }

    /// 把整张战场画布落盘成 RGBA（`--dumptmap <路径>`）。
    /// 只截屏局部的话，"大片黑楔形"到底是地图边界还是漏画，看不出比例。
    bool Dump_Terrain_RGBA(const char* path) const;
    /// 把指定瓦片变体落盘（`--dumptile <tile> <sub> <路径>`）。
    bool Dump_Tile_RGBA(int tile, int sub, const char* path);

private:
    void Update_Camera(float dt);
    /// 帧外准备精灵（体素烘焙 + SHP 上传）。见 Render 里为什么要放在帧外。
    void Warm_Sprites();
    /// 对象的下标按画家序排好（等距投影：远的先画）。
    std::vector<int> Objects_In_Painter_Order() const;
    /// 载入原版界面贴图（SIDE1/SIDE2/SIDE3/TAB/RADAR/CREDITS/POWER…）。
    bool Load_UI();
    /// 画一件界面贴图（贴图缺失时静默跳过）。
    void Draw_Ui(const UiPiece& p, int x, int y, int frame = 0);
    /// 在给定横条里**居中**画资金数字（原版就是这样，不是色带）。
    void Draw_Money(int x, int y, int w, int amount);
    /// 雷达所在矩形（外框）。返回 false = 没有雷达贴图，调用方退回自绘。
    bool Radar_Rect(int* x, int* y, int* w, int* h) const;
    /// 雷达**显示区**在屏幕上的矩形（外框里镂空的那块，实测自 RADAR.SHP 第 32 帧）。
    bool Radar_Inner(int* x, int* y, int* w, int* h) const;
    void Draw_Battlefield();
    void Draw_Grid_Debug();
    /// 画一个单位的真精灵（体素/SHP）。返回 false = 没有素材，退成色块。
    bool Draw_Object_Sprite(const Object& o, int sx, int sy, float scale);
    void Draw_Objects();
    void Draw_Top_Bar();
    void Draw_Sidebar();
    void Draw_Radar();
    void Draw_Selection();

    /// 屏幕坐标 -> 地图格子。落在视口外或地图外返回 false。
    bool Pick_Cell(int sx, int sy, int* cx, int* cy) const;
    /// 屏幕坐标 -> 连续的格坐标（浮点，用于下移动命令）。
    bool Pick_Cell_F(int sx, int sy, float* fx, float* fy) const;
    /// 格子 -> 屏幕（已考虑相机）。超出视口也返回 true，调用方自己裁。
    void Cell_To_Screen(float cx, float cy, float* sx, float* sy) const;
    /// 某格的高度级。越界返回 0。
    int Cell_Level(int cx, int cy) const;
    /// 把相机中心挪到某个格（H / 空格 / F1-F4 / 跟随都走这条）。
    void Center_On_Cell(float cx, float cy);

    GameScreen screen_ = GameScreen::Battle;

    // 素材
    std::vector<std::unique_ptr<MixFileClass>> mixes_;
    std::vector<MixFileClass*> roots_;
    MapFile map_;
    MapRenderer map_renderer_;
    std::vector<uint32_t> terrain_rgba_;
    int terrain_w_ = 0, terrain_h_ = 0;
    int terrain_sprite_ = -1;
    /// 小地图底图：战场图按雷达显示区尺寸抽稀一张。不抽稀的话雷达里只有黑底加点。
    int minimap_sprite_ = -1;
    int minimap_w_ = 0, minimap_h_ = 0;
    RemapTable remap_;
    int player_color_ = 5;   ///< [Colors] 下标，5 = DarkRed（苏军红）

    // 逻辑层
    World world_;
    SpriteCache sprites_;                  ///< 单位名 -> 真图（体素/SHP）
    int sprites_ok_ = 0, sprites_miss_ = 0;
    int sidebar_tab_ = 0;    ///< Q/W/E/R
    int cursor_mode_ = 0;    ///< 0=普通 1=修理 2=变卖
    bool follow_ = false;    ///< F：镜头跟随选中的单位
    bool power_warn_ = false;  ///< 电力欠费（表盘画第 1 帧）
    bool grid_debug_ = false;  ///< --grid：画格子中心网 + 对象落点十字
    int objects_drawn_ = 0;  ///< 自检用：这一帧画了几个对象

    // 状态
    Camera camera_;
    int win_w_ = 800, win_h_ = 600;
    int frame_ = 0;
    int logic_frame_ = 0;
    float logic_accum_ = 0.0f;

    // 输入状态
    int mouse_x_ = 0, mouse_y_ = 0;
    bool dragging_ = false;
    bool drag_shift_ = false;
    int drag_x0_ = 0, drag_y0_ = 0;
    bool edge_scroll_ = true;
    bool keys_down_[256] = {};
    float bookmarks_[4][2] = {};      ///< Ctrl+F1..F4 设的书签
    bool bookmark_set_[4] = {};

    // ---- 原版界面贴图（SIDEBAR.PAL 上色，烘成 RGBA 后复用）----
    UiPiece ui_side1_;      ///< 侧栏头部（168×69），底下自带 4 个页签凹槽
    UiPiece ui_side2_;      ///< 建造格一行（168×50，两列）
    UiPiece ui_side2b_;     ///< 末行变体（168×50）
    UiPiece ui_side3_;      ///< 收尾装饰（168×26）
    UiPiece ui_radar_;      ///< 雷达外框（168×110，33 帧动画；第 32 帧中间是显示区）
    UiPiece ui_credits_;    ///< 资金条（168×16）
    UiPiece ui_power_;      ///< 电力表（27×30，2 帧）
    UiPiece ui_tab_[4];     ///< 四个页签（28×27，5 帧）
    UiPiece ui_btn_[12];    ///< 单位指令按钮（52×32，2 帧）
    UiPiece ui_sidebttn_;   ///< 侧栏文字按钮（125×25，3 帧）
    /// 资金数字字形：number0.pcx..number9.pcx（PCX，索引色 + SIDEFNT3.PAL）。
    /// 原版的资金是**在条里居中显示数字**，不是一条色带。
    UiPiece ui_digit_[10];
    int credits_ = 0;       ///< 当前资金（rules.ini 的 StartCredits 起手）

    Dx12Renderer renderer_;
    bool ready_ = false;
};

}  // namespace ra2
