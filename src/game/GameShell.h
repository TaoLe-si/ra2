// GameShell.h -- 游戏外壳：状态机 + 相机 + 界面布局
//
// 目标不是"能看素材"，是**打开 exe 就是原版那个游戏**：主菜单、载入、
// 战场、侧栏、小地图、框选、右键命令、快捷键。素材层和渲染层已经就位
// （MIX/SHP/TMP/VXL/地图/光影），这一层把它们组织成一个可玩的壳。
//
// 【分辨率与布局】照原版 800×600 的观感，侧栏件全部 168 宽：
//   主视图      x = 0..W-kSidebarW, y = 0..H
//   右侧边栏    CREDITS → TOP → RADAR → SIDE1 → SIDE2 → SIDE3（gamemd 0x72fc60）
//   电力竖条    POWERP.SHP，自下而上点亮
// 原版更高分辨率是同一套布局按比例放大，不是换结构。
//
// 【逻辑帧】原版是 **15 FPS 逻辑帧**（每帧 1/15 秒）配渲染插值。
// 锁步联机要求逻辑帧严格定步，所以这里也用固定步长累加器，
// 见 Update()。渲染每帧都跑，逻辑可能跑 0 次或多次。

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "data/Ini.h"
#include "data/CsfFile.h"
#include "game/World.h"
#include "gfx/ObjectSprite.h"
#include "gfx/RemapTable.h"
#include "gfx/dx12/Dx12Renderer.h"
#include "io/FileSystem.h"
#include "map/Map.h"
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
/// PowerClass::Draw_It @0x0063FB20：POWERP.SHP 每片 12×2，Y 步进 3。
constexpr int kPowerPipStep = 3;
constexpr int kPowerPipX = 5;    ///< 相对侧栏左缘（Draw_It 里 ebx=0/5）
/// StripClass 初始化（0x006A5310）：第二列 X = 0x55 = 85，cameo 宽 imul 0x3C = 60，
/// 可见行数用 0x51EB851F 做有符号除以 50（SIDE2 行高）。第一列 = 85-60-1 = 24。
constexpr int kCameoX0 = 24;
constexpr int kCameoX1 = 85;
constexpr int kCameoW = 60;
constexpr int kCameoRowH = 50;

/// 逻辑帧率。原版 RA2 是 15 FPS 逻辑帧。
constexpr int kLogicFps = 15;
constexpr float kLogicDt = 1.0f / static_cast<float>(kLogicFps);

enum class GameScreen {
    Menu,      ///< 主菜单（未接线前可跳过）
    Loading,   ///< 载入中
    Battle,    ///< 战场
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

    /// 进战场后的背景音乐：THEME(MD).INI 选曲 → THEME(MD).MIX 读
    /// IMA ADPCM WAV → 解码循环播放（ThemeClass 的压缩版）。
    void Start_Battle_Music();

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

    /// 仅挂 MIX/CSF/UI，进主菜单（无地图）。skirmish_map = SinglePlayer 默认图。
    bool Enter_Title_Menu(const std::vector<std::string>& mix_paths,
                          const char* skirmish_map, std::string* err = nullptr);
    /// 主菜单 ExitGame 置位；窗口泵应退出。
    bool Exit_Requested() const noexcept { return exit_requested_; }

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
    /// 载入原版界面贴图（SIDE1/2/3/TAB/RADAR/CREDITS/POWERP…）。
    bool Load_UI();
    /// gamemd 0x00534FA0：按 Side 挂载 SIDENC/SIDEC/SIDEC*MD，供界面 SHP 优先命中。
    bool Mount_Player_Sidenc();
    /// 画一件界面贴图（贴图缺失时静默跳过）。
    void Draw_Ui(const UiPiece& p, int x, int y, int frame = 0);
    /// 在给定横条里**居中**画资金数字（原版就是这样，不是色带）。
    void Draw_Money(int x, int y, int w, int amount);
    /// FULLFNT3.SHP 拉丁字形（帧下标=码点；胜负条 MessageList 文案）。
    void Draw_Fullfnt_Text(int x, int y, const std::string& utf8);
    /// CJK 单字：用 Windows GDI 把一个 codepoint 出成 RGBA（alpha=0 = 透明），
    /// 上传 GPU 后再 blit。这是 GAME.FNT 严格逆向没做完之前的兜底：
    /// 系统字体的视觉效果与 GAME.FNT 在屏幕上几乎一致（同字体名 + 同色）。
    bool Draw_CJK_Glyph(wchar_t cp, int w, int h,
                        std::vector<uint8_t>* rgba, int* out_w, int* out_h);
    /// GOptions 对话框（.rsrc @0x489860）：画暂停面板。
    void Draw_Pause_Options();
    /// 点中暂停钮：返回 true = 已消费点击。
    bool Hit_Pause_Options(int x, int y);
    /// 对话框 182：AskAbortMission（Leave/Restart/Resume）。
    void Draw_Ask_Abort();
    bool Hit_Ask_Abort(int x, int y);
    /// 主菜单（RT_DIALOG 226 / 0xE2）：Title.PCX + 按钮列。
    void Draw_Title_Menu();
    int Hovered_Title_Button() const;   ///< 悬停的菜单按钮下标（-1 无）
    /// 点中主菜单钮。返回 true = 已消费。
    bool Hit_Title_Menu(int x, int y);
    /// 从 MAPS*.MIX 抽出 Brief:ALL01 战役图到临时 .map（NewCampaign）。
    bool Extract_Campaign_Map(const char* briefing_key, std::string* out_path,
                              std::string* err);
    /// 雷达所在矩形（外框）。返回 false = 没有雷达贴图，调用方退回自绘。
    bool Radar_Rect(int* x, int* y, int* w, int* h) const;
    /// 雷达**显示区**（RadarClass::Init_For_House @0x00652E90：锚在 TOP，缓冲 0x8C×0x6C）。
    bool Radar_Inner(int* x, int* y, int* w, int* h) const;
    /// rulesmd [Sides] 顺序：GDI=0 / Nod=1 / ThirdSide=2（与 Init_For_House 分支一致）。
    int Player_Side() const;
    void Draw_Battlefield();
    void Draw_Grid_Debug();
    /// 画一个单位的真精灵（体素/SHP）。返回 false = 没有素材，这一帧跳过。
    bool Draw_Object_Sprite(const Object& o, int sx, int sy, float scale);
    void Draw_Objects();
    void Draw_Top_Bar();
    void Draw_Sidebar();
    void Draw_Radar();
    /// RADAR 外框 + CREDITS（预览之后；TOP 必须在 Draw_Radar 预览之前画）。
    void Draw_Radar_Chrome();
    void Draw_Power_Pips(int sx, int y0, int y1);
    void Draw_Cameos(int sx, int y0, int y1);
    void Refresh_Sidebar();
    void Refresh_Power();
    /// 点侧栏 cameo：Event#14 → Begin_Production @0x4FA350 简化入口
    ///（FindSuitableFactory + BuildSpeed 帧；单槽，无 FactoryClass 队列）。
    bool Queue_Build(const std::string& type);
    /// 生产完成：建筑进放置模式，单位直接在基地旁生成。
    void Finish_Build(const std::string& type, int tab);
    bool Place_Pending_Building(float cx, float cy);
    /// cameo 槽位命中：返回 sidebar_items_ 下标，-1 = 未点中。
    int Cameo_At(int sx, int sy) const;
    /// 修理 / 变卖光标落到己方建筑上。
    bool Try_Repair_Or_Sell(float cx, float cy);
    const UiPiece* Ensure_Cameo(const std::string& name);
    /// 弹道 SHP（Projectile Image=.shp）；未命中则返回 nullptr。
    const UiPiece* Ensure_Projectile(const std::string& image);
    void Draw_Selection();
    /// MOUSE.SHA 光标（gamemd MouseClass @0x87F7E8，表 @0x82D028）。
    void Draw_Mouse();
    /// 选中单位血条：PIPS.SHP，对齐 TechnoClass::DrawHealthBar @0x006F64A0。
    void Draw_Health_Pips(const Object& o, int sx, int sy, float scale);

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

    GameScreen screen_ = GameScreen::Menu;

    // 素材
    std::vector<std::unique_ptr<MixFileClass>> mixes_;
    std::vector<MixFileClass*> roots_;
    std::vector<std::string> pending_mixes_;  ///< 主菜单后 Load_Map 用
    std::string skirmish_map_;                ///< GUI:SinglePlayer → 默认图
    std::string current_map_path_;            ///< 当前战场图（AskAbort Restart 重载）
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
    MapClass logic_map_;
    SpriteCache sprites_;                  ///< 单位名 -> 真图（体素/SHP）
    CsfFile csf_;                          ///< ra2.csf / ra2md.csf（@0x734770）
    MatchOutcome last_outcome_ = MatchOutcome::Playing;
    int sprites_ok_ = 0, sprites_miss_ = 0;
    int sidebar_tab_ = 0;    ///< Q/W/E/R
    int cameo_scroll_ = 0;   ///< 建造格行滚动（R-UP/R-DN / PgUp/PgDn）
    int cursor_mode_ = 0;    ///< 0=普通 1=修理 2=变卖
    bool paused_ = false;    ///< Esc 选项意图（GOptions；无对话框模板前仅停逻辑+文案）
    bool ask_abort_ = false; ///< 对话框 182 AskAbortMission（Abort 后确认）
    int title_page_ = 0;     ///< 0=主菜单226 / 1=单人256
    /// MouseCursorType（gamemd 表 0x82D028 / YRpp 同序）：Default=0 Repair=0x21 Sell=0x1E。
    int mouse_cursor_ = 0;
    bool hide_os_cursor_ = false;
    /// Init 时 hwnd 为空 → 离屏模式（选曲要确定性，不 rand）。
    bool offscreen_ = false;
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
    UiPiece ui_radar_;      ///< 雷达外框（168×110，33 帧；第 32 帧镂空；来自 SIDENC）
    UiPiece ui_top_;        ///< TOP.SHP（168×32，堆在 CREDITS 下；阵营外观随 SIDENC）
    UiPiece ui_credits_;    ///< CREDITS.SHP（168×16 资金条，y=0；阵营外观随 SIDENC）
    UiPiece ui_power_;      ///< Power.SHP 27×30，2 帧（SIDE1 上的小表，不是竖条）
    UiPiece ui_powerp_;     ///< POWERP.SHP 12×2×5 帧，PowerClass 竖条的每一格
    UiPiece ui_rup_;        ///< R-UP.SHP 46×25×3，建造格上翻（Sidebar.CPP 0x006A59CA）
    UiPiece ui_rdn_;        ///< R-DN.SHP 46×25×3，建造格下翻
    UiPiece ui_shroud_;     ///< SHROUD.SHP（FogOfWar 地形遮罩帧）
    UiPiece ui_fog_;        ///< FOG.SHP（0x47F01F 位选：Scenario flags bit4 → FOG）
    UiPiece ui_title_;      ///< Title.PCX 主菜单背景（GraphicMenu Background）
    UiPiece ui_logo_;       ///< LOGO.PCX 主菜单 Logo 叠层（'Logo' 键，索引0透明）
    UiPiece ui_menubtn_;    ///< 主菜单按钮底板（ra2.mix 深层 0x1BB65278，
                            ///< 126×25×3：青铜渐变+五角星，正常/高亮/禁用）
    bool exit_requested_ = false;  ///< GUI:ExitGame
    UiPiece ui_tab_[4];     ///< 四个页签（28×27，5 帧）
    UiPiece ui_btn_[12];    ///< 单位指令按钮（52×32，2 帧）
    UiPiece ui_sidebttn_;   ///< 侧栏文字按钮（125×25，3 帧）
    /// 资金数字：number0.pcx..number9.pcx。PCX 内嵌调色板是 8 位，
    /// 不能拿 SIDEBAR.PAL 的 6 位展开去套，否则整串发黑。
    UiPiece ui_digit_[10];
    UiPiece ui_fullfnt_;    ///< FULLFNT3.SHP（侧栏/消息拉丁字；@0x5D2F08）
    UiPiece ui_mouse_;      ///< MOUSE.SHA + MOUSEPAL.PAL（517 帧）
    UiPiece ui_pips_;       ///< PIPS.SHP 16×16×21（血条格）
    std::unordered_map<std::string, UiPiece> ui_cameo_;
    std::unordered_map<std::string, UiPiece> ui_proj_;  ///< 弹道 SHP 缓存
    std::vector<std::string> sidebar_items_;
    int credits_ = 0;       ///< 当前资金（rules.ini 的 StartCredits 起手）
    CsfFile csf_en_;        ///< language.mix ra2.csf（FULLFNT3 可画的英文胜负串）
    std::string outcome_msg_;  ///< Flag 后 MessageList 文案（0x6D4DB0 语义）

    /// 生产队列（每页签一条；YR 每厂一条，这里先简化为全局一条）。
    struct BuildJob {
        std::string type;
        int tab = 0;
        int progress = 0;       ///< 已推进逻辑帧（整数显示）
        double progress_fp = 0; ///< 欠电小数累加（rules 倍率）
        int needed = 1;         ///< 完成所需逻辑帧
        int cost = 0;
    };
    BuildJob build_job_;
    bool build_active_ = false;
    /// 建筑放置模式：生产完成后挂起，左键落点。
    std::string place_type_;
    bool placing_ = false;

    Dx12Renderer renderer_;
    bool ready_ = false;
};

}  // namespace ra2
