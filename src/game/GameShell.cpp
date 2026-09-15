// GameShell.cpp

#include "game/GameShell.h"

#include <algorithm>

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
constexpr float kUiRadarDot[4] = {1.0f, 0.9f, 0.2f, 1.0f};      // 小地图上的单位

/// 单位标记色：按 kind 区分。等真精灵接进来这些就被贴图取代。
constexpr float kObjVehicle[4] = {0.95f, 0.75f, 0.15f, 1.0f};
constexpr float kObjInfantry[4] = {0.35f, 0.85f, 0.35f, 1.0f};
constexpr float kObjBuilding[4] = {0.55f, 0.65f, 0.95f, 1.0f};
constexpr float kObjAircraft[4] = {0.85f, 0.45f, 0.95f, 1.0f};
constexpr float kObjTerrain[4] = {0.30f, 0.36f, 0.26f, 1.0f};

const float* Object_Color(MapObjectKind kind) {
    switch (kind) {
        case MapObjectKind::Unit:     return kObjVehicle;
        case MapObjectKind::Infantry: return kObjInfantry;
        case MapObjectKind::Building: return kObjBuilding;
        case MapObjectKind::Aircraft: return kObjAircraft;
        default:                      return kObjTerrain;
    }
}

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
    // 战场统计：黑洞（没瓦片 / 瓦片全透明）必须打得出来，不然只能靠眼睛数
    {
        int no_tile = 0, total = 0;
        for (const IsoCell& c : map_.Cells()) {
            ++total;
            if (c.tile < 0) {
                ++no_tile;
            }
        }
        std::printf("  战场 %dx%d：格 %d（无瓦片 %d），画上 %d，"
                    "取不到瓦片 %d，取到但全空 %d\n",
                    terrain_w_, terrain_h_, total, no_tile,
                    map_renderer_.Cells_Drawn(), map_renderer_.Tiles_Missing(),
                    map_renderer_.Cells_Empty());
        // 空格的分布图：黑块到底是"地图边界外的空白"（正常）还是"中间破了个洞"
        // （解析错），看这张图一眼就知道。每个字符代表 2×4 格。
        const int W = map_.Width(), H = map_.Height();
        if (W > 0 && H > 0 && no_tile > 0) {
            std::printf("     空瓦片分布（. 有瓦片 / 空 没有）每格 2x4 单元：\n");
            for (int cy = 0; cy < H; cy += 4) {
                std::string line = "     ";
                for (int cx = 0; cx < W; cx += 2) {
                    int filled = 0, n = 0;
                    for (int dy = 0; dy < 4 && cy + dy < H; ++dy) {
                        for (int dx = 0; dx < 2 && cx + dx < W; ++dx) {
                            const size_t i =
                                static_cast<size_t>(cy + dy) * W + cx + dx;
                            if (i < map_.Cells().size()) {
                                ++n;
                                if (map_.Cells()[i].tile >= 0) {
                                    ++filled;
                                }
                            }
                        }
                    }
                    line += (n == 0) ? ' ' : (filled * 2 >= n ? '.' : 'o');
                }
                std::printf("%s\n", line.c_str());
            }
        }
    }
    terrain_sprite_ = renderer_.Upload_Sprite_RGBA(
        reinterpret_cast<const uint8_t*>(terrain_rgba_.data()), terrain_w_,
        terrain_h_);
    if (terrain_sprite_ < 0) {
        if (err) *err = "战场图上传失败";
        return false;
    }

    if (!world_.Build(map_)) {
        // 没有对象也让它进战场（有些图真是空的），但记一笔
        std::printf("[!] 地图里没有对象\n");
    }

    // 单位真图：体素走 VXL、步兵/建筑走 SHP，都带阵营色 remap
    if (sprites_.Bind(roots_)) {
        sprites_.Set_Renderer(&renderer_);   // 体素精灵是 GPU 烘出来的
        sprites_.Set_Remap(remap_);
        sprites_.Set_Theater(map_.Theater());
        std::printf("  精灵库就绪（单位模型 %d 个）\n", sprites_.Models().Unit_Count());
    } else {
        std::printf("[!] 精灵库绑定失败，单位退化成色块\n");
    }

    // 原版界面贴图（侧栏/页签/雷达/资金条）。失败不致命，退回自绘的色块。
    if (!Load_UI()) {
        std::printf("[!] 界面贴图载入失败，侧栏退回色块\n");
    }

    // 小地图底图：把战场图抽稀到雷达显示区大小。不抽的话雷达里只有黑底加点，
    // 和原版那个能看出街区轮廓的小地图差太远。
    {
        int rx = 0, ry = 0, rw = 0, rh = 0;
        if (Radar_Inner(&rx, &ry, &rw, &rh) && terrain_w_ > 0 && terrain_h_ > 0 &&
            rw > 0 && rh > 0 &&
            static_cast<size_t>(terrain_w_) * terrain_h_ == terrain_rgba_.size()) {
            std::vector<uint8_t> mm(static_cast<size_t>(rw) * rh * 4, 0);
            for (int y = 0; y < rh; ++y) {
                const int sy = static_cast<int>(static_cast<float>(y) / rh *
                                                terrain_h_);
                for (int x = 0; x < rw; ++x) {
                    const int sx = static_cast<int>(static_cast<float>(x) / rw *
                                                    terrain_w_);
                    const uint32_t px =
                        terrain_rgba_[static_cast<size_t>(sy) * terrain_w_ + sx];
                    const size_t o = (static_cast<size_t>(y) * rw + x) * 4;
                    // 压暗一档：原版小地图比战场暗，不然会抢视线
                    const float k = 0.55f;
                    mm[o + 0] = static_cast<uint8_t>((px & 0xFF) * k);
                    mm[o + 1] = static_cast<uint8_t>(((px >> 8) & 0xFF) * k);
                    mm[o + 2] = static_cast<uint8_t>(((px >> 16) & 0xFF) * k);
                    mm[o + 3] = 255;
                }
            }
            minimap_sprite_ = renderer_.Upload_Sprite_RGBA(mm.data(), rw, rh);
            minimap_w_ = rw;
            minimap_h_ = rh;
        }
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
        world_.Update(kLogicDt);
        logic_accum_ -= kLogicDt;
        ++steps;
    }

    // 键盘持续按住的方向键滚动（原版是按住就一直卷）
    if (screen_ == GameScreen::Battle) {
        const float step = 600.0f * dt;
        int dx = 0, dy = 0;
        if (keys_down_[VK_LEFT])  dx -= 1;
        if (keys_down_[VK_RIGHT]) dx += 1;
        if (keys_down_[VK_UP])    dy -= 1;
        if (keys_down_[VK_DOWN])  dy += 1;
        if (dx || dy) {
            camera_.Scroll(dx * step, dy * step);
        }
    }

    // 相机命令（H 回基地 / 空格去事件 / F1-F4 书签）
    float order_x = 0.0f, order_y = 0.0f;
    if (world_.Consume_Camera_Order(&order_x, &order_y)) {
        Center_On_Cell(order_x, order_y);
    }

    // F：镜头跟随选中的单位
    if (follow_) {
        float fx = 0.0f, fy = 0.0f;
        int n = 0;
        for (const Object& o : world_.Objects()) {
            if (o.selected) {
                fx += o.x;
                fy += o.y;
                ++n;
            }
        }
        if (n > 0) {
            Center_On_Cell(fx / n, fy / n);
        }
    }
}

void GameShell::Render() {
    if (!ready_) {
        return;
    }
    // 精灵必须在**帧外**备好。体素烘焙（Bake_Voxels）和 SHP 上传都会自己
    // reset 命令分配器再提交一段命令列表，放到 Begin_Frame 之后做，会把
    // 这一帧已经录进去、还没提交的绘制命令直接冲掉。
    Warm_Sprites();
    const float clear[4] = {0.02f, 0.02f, 0.03f, 1.0f};
    renderer_.Begin_Frame(clear);
    if (screen_ == GameScreen::Battle) {
        Draw_Battlefield();
        Draw_Objects();
        Draw_Top_Bar();
        Draw_Sidebar();
        Draw_Radar();
        Draw_Selection();
    }
    renderer_.End_Frame();
}

// ---------------------------------------------------------------------------
// 坐标换算
// ---------------------------------------------------------------------------

int GameShell::Cell_Level(int cx, int cy) const {
    if (cx < 0 || cy < 0 || cx >= map_.Width() || cy >= map_.Height()) {
        return 0;
    }
    const std::vector<IsoCell>& cells = map_.Cells();
    const size_t idx = static_cast<size_t>(cy) * map_.Width() + cx;
    if (idx >= cells.size()) {
        return 0;
    }
    return cells[idx].level;
}

void GameShell::Cell_To_Screen(float cx, float cy, float* sx, float* sy) const {
    // 先算格子的画布坐标（含高度抬升），再过相机
    const int lx = Cell_Level(static_cast<int>(cx), static_cast<int>(cy));
    int px = 0, py = 0;
    MapRenderer::Cell_To_Canvas(map_renderer_.Origin_X(), map_renderer_.Origin_Y(),
                                static_cast<int>(cx), static_cast<int>(cy), lx,
                                &px, &py);
    // 加上格内的小数偏移（单位在格之间时）
    const float fx = cx - std::floor(cx);
    const float fy = cy - std::floor(cy);
    const float wx = px + kCellHalfW * (fx - fy);
    const float wy = py + kCellHalfH * (fx + fy);
    camera_.World_To_Screen(wx + kCellHalfW,
                            wy + kCellHalfH, sx, sy);
}

/// 相机对准某个格。
void GameShell::Center_On_Cell(float cx, float cy) {
    int px = 0, py = 0;
    MapRenderer::Cell_To_Canvas(map_renderer_.Origin_X(), map_renderer_.Origin_Y(),
                                static_cast<int>(cx), static_cast<int>(cy),
                                Cell_Level(static_cast<int>(cx), static_cast<int>(cy)),
                                &px, &py);
    const float half_vw = static_cast<float>(win_w_ - kSidebarW) * 0.5f / camera_.Scale();
    const float half_vh = static_cast<float>(win_h_ - kTopBarH) * 0.5f / camera_.Scale();
    camera_.Set(px + kCellHalfW - half_vw,
                py + kCellHalfH - half_vh);
}

/// 把这一帧要用的精灵备好。必须放在 Begin_Frame **之前**（见 Render 里的注释）。
///
/// 分两遍：先单位/建筑（Techno），再装饰。不然 200 多个树先来，
/// 每帧 4 个预算全被它们吃掉，坦克要等几十帧才出得来。
void GameShell::Warm_Sprites() {
    sprites_.Reset_Budget(4);
    const int view_w = win_w_ - kSidebarW;
    auto warm = [&](bool techno_only) {
        for (const Object& o : world_.Objects()) {
            if (sprites_.Budget_Left() <= 0) {
                return;
            }
            if (techno_only != o.Is_Techno()) {
                continue;
            }
            float sx = 0.0f, sy = 0.0f;
            Cell_To_Screen(o.x, o.y, &sx, &sy);
            if (sx < -kCellW || sx > view_w + kCellW ||
                sy < kTopBarH - kCellH || sy > win_h_ + kCellH) {
                continue;
            }
            const int house_color = (o.house >= 0 && o.is_mine) ? player_color_ : 11;
            sprites_.Get(o.type.c_str(), o.facing, house_color);
        }
    };
    warm(true);
    warm(false);
}

std::vector<int> GameShell::Objects_In_Painter_Order() const {
    // 【为什么对象也要排序】地图对象是按文件里的段顺序来的（Terrain 段在前、
    // 然后是 Units/Structures…），跟屏幕上的远近毫无关系。
    // 直接照这个顺序画，会出现"近处的树被远处的楼盖住"、
    // "同一格的兵和车谁压谁随机" —— 看着就是"摆放位置乱"。
    //
    // 等距投影下屏幕 y = 15*(cx+cy)，所以按 (cx+cy) 升序；同一斜列里
    // 再按 level 升序、cx 升序，和地形用的是同一套画家序。
    std::vector<int> order;
    const std::vector<Object>& objs = world_.Objects();
    order.reserve(objs.size());
    for (size_t i = 0; i < objs.size(); ++i) {
        order.push_back(static_cast<int>(i));
    }
    const std::vector<Object>& o = objs;
    std::sort(order.begin(), order.end(), [&o](int a, int b) {
        const int sa = static_cast<int>(o[a].x) + static_cast<int>(o[a].y);
        const int sb = static_cast<int>(o[b].x) + static_cast<int>(o[b].y);
        if (sa != sb) {
            return sa < sb;
        }
        if (static_cast<int>(o[a].x) != static_cast<int>(o[b].x)) {
            return static_cast<int>(o[a].x) < static_cast<int>(o[b].x);
        }
        return o[a].id < o[b].id;
    });
    return order;
}

/// 画一个单位的真精灵。返回 false 表示没有可用素材（调用方退成色块）。
///
/// 精灵本体已经在显存里（sprite_id）：体素是 GPU 光栅化烘出来的，
/// SHP 是 CPU 解完索引图传上去的。这里只负责 blit。
bool GameShell::Draw_Object_Sprite(const Object& o, int sx, int sy, float scale) {
    const int house_color = (o.house >= 0 && o.is_mine) ? player_color_ : 11;
    const ObjectSprite* sp = sprites_.Get(o.type.c_str(), o.facing, house_color);
    if (sp == nullptr || !sp->ok || sp->sprite_id < 0) {
        ++sprites_miss_;
        return false;
    }
    ++sprites_ok_;

    const int dx = sx + static_cast<int>(sp->off_x * scale);
    const int dy = sy + static_cast<int>(sp->off_y * scale);
    renderer_.Draw_Sprite(sp->sprite_id, dx, dy, scale);
    return true;
}

void GameShell::Draw_Objects() {
    objects_drawn_ = 0;
    sprites_ok_ = 0;
    sprites_miss_ = 0;
    const int view_w = win_w_ - kSidebarW;
    // 按画家序画：近处的对象后画才会盖住远处的（见 Objects_In_Painter_Order）
    const std::vector<int> order = Objects_In_Painter_Order();
    const std::vector<Object>& objs = world_.Objects();
    for (int idx : order) {
        if (idx < 0 || idx >= static_cast<int>(objs.size())) {
            continue;
        }
        const Object& o = objs[static_cast<size_t>(idx)];
        float sx = 0.0f, sy = 0.0f;
        Cell_To_Screen(o.x, o.y, &sx, &sy);
        // 视口裁剪（留一格余量，免得贴边的突然消失）
        if (sx < -kCellW || sx > view_w + kCellW ||
            sy < kTopBarH - kCellH || sy > win_h_ + kCellH) {
            continue;
        }
        const float scale = camera_.Scale();
        const int w = static_cast<int>(kCellW * scale);
        const int h = static_cast<int>(kCellH * scale);
        const int x = static_cast<int>(sx) - w / 2;
        const int y = static_cast<int>(sy) - h / 2;

        // 先试真精灵（体素车 / SHP 步兵建筑），画不出来才退成色块。
        const bool drew = Draw_Object_Sprite(o, static_cast<int>(sx),
                                             static_cast<int>(sy), scale);
        if (!drew) {
            const int h2 = (h > 8) ? h / 2 : 4;
            renderer_.Draw_Rect(x + w / 4, y + h / 2 - h2 / 2, w / 2, h2,
                                Object_Color(o.kind));
            if (o.Is_Techno()) {
                renderer_.Draw_Rect_Outline(x + w / 4, y + h / 2 - h2 / 2, w / 2,
                                            h2, kUiEdge, 1);
            }
        }
        if (o.selected) {
            // 选中框：比本体大一圈的绿框
            renderer_.Draw_Rect_Outline(x, y, w, h, kUiSelect, 1);
            // 血条
            const int hp_w = (o.hp_max > 0) ? (w * o.hp / o.hp_max) : 0;
            const float green[4] = {0.2f, 0.9f, 0.2f, 1.0f};
            const float red[4] = {0.9f, 0.2f, 0.2f, 1.0f};
            renderer_.Draw_Rect(x, y - 4, w, 2, red);
            if (hp_w > 0) {
                renderer_.Draw_Rect(x, y - 4, hp_w, 2, green);
            }
        }
        ++objects_drawn_;
    }
}

void GameShell::Draw_Battlefield() {
    if (terrain_sprite_ < 0) {
        return;
    }
    float sx = 0.0f, sy = 0.0f;
    camera_.World_To_Screen(0.0f, 0.0f, &sx, &sy);
    renderer_.Draw_Sprite(terrain_sprite_, static_cast<int>(sx),
                          static_cast<int>(sy), camera_.Scale());
    if (grid_debug_) {
        Draw_Grid_Debug();
    }
}

bool GameShell::Dump_Terrain_RGBA(const char* path) const {
    if (path == nullptr || terrain_rgba_.empty() || terrain_w_ <= 0 ||
        terrain_h_ <= 0) {
        return false;
    }
    FILE* f = std::fopen(path, "wb");
    if (f == nullptr) {
        return false;
    }
    std::fwrite(terrain_rgba_.data(), 4,
                static_cast<size_t>(terrain_w_) * terrain_h_, f);
    std::fclose(f);
    std::printf("[OK] 战场画布 %dx%d -> %s\n", terrain_w_, terrain_h_, path);
    return true;
}

/// 诊断层：格子中心网（灰点）+ 对象落点（黄十字）。
///
/// "对象摆放位置乱"有两种可能：坐标换算错了（十字不落在格心），
/// 或者坐标对但压盖顺序错了（十字对得上、图被别的盖住）。
/// 把格心画出来就能把这两种分开。
void GameShell::Draw_Grid_Debug() {
    const float dot[4] = {0.35f, 0.35f, 0.45f, 1.0f};
    const float mark[4] = {1.0f, 0.95f, 0.2f, 1.0f};
    const float cx[4] = {0.9f, 0.3f, 0.9f, 1.0f};
    for (int cy = 0; cy < map_.Height(); cy += 2) {
        for (int cxi = 0; cxi < map_.Width(); cxi += 2) {
            float px = 0.0f, py = 0.0f;
            Cell_To_Screen(static_cast<float>(cxi), static_cast<float>(cy), &px, &py);
            if (px < -8 || px > win_w_ - kSidebarW + 8 || py < -8 || py > win_h_ + 8) {
                continue;
            }
            renderer_.Draw_Rect(static_cast<int>(px), static_cast<int>(py), 2, 2, dot);
        }
    }
    for (const Object& o : world_.Objects()) {
        if (!o.Is_Techno()) {
            continue;
        }
        float px = 0.0f, py = 0.0f;
        Cell_To_Screen(o.x, o.y, &px, &py);
        if (px < -8 || px > win_w_ - kSidebarW + 8 || py < -8 || py > win_h_ + 8) {
            continue;
        }
        const int ix = static_cast<int>(px);
        const int iy = static_cast<int>(py);
        renderer_.Draw_Rect(ix - 6, iy, 13, 1, mark);
        renderer_.Draw_Rect(ix, iy - 6, 1, 13, mark);
    }
}

// ---------------------------------------------------------------------------
// 原版界面贴图
//
// 侧栏不是"画几个色块凑个样子"就算复刻的 —— 原版把这些件全放在 MIX 里：
//   SIDE1(168×69) 头部，下缘自带 4 个页签凹槽
//   SIDE2(168×50) 建造格一行（两列），SIDE2B 是末行变体
//   SIDE3(168×26) 收尾装饰（鹰徽）
//   RADAR(168×110, 33 帧) 雷达外框；第 32 帧中间镂空，就是显示区
//   CREDITS(168×16) 资金条、POWER(27×30) 电力表
//   TAB00..03(28×27, 5 帧) 四个页签、BUTTON00..11(52×32) 单位指令按钮
// 位置也不是猜的：对 SIDE1 第 48~64 行做"蓝色像素游程"扫描量出 4 个凹槽
// 在 x = 26/55/85/114（28 宽、间隔 29），对 SIDE2 做暗区扫描量出两列凹槽
// 在 x = 24/86。见 tools/uidump.py。
// ---------------------------------------------------------------------------

bool GameShell::Load_UI() {
    if (roots_.empty()) {
        return false;
    }
    std::vector<uint8_t> pal6;
    for (MixFileClass* m : roots_) {
        pal6 = m->Read_Deep("SIDEBAR.PAL");
        if (pal6.size() >= 768) {
            break;
        }
        pal6.clear();
    }
    uint8_t pal768[768];
    if (pal6.size() >= 768) {
        SpriteCache::Expand_Pal768(pal6.data(), pal768);
    } else {
        for (int i = 0; i < 256; ++i) {
            pal768[i * 3 + 0] = pal768[i * 3 + 1] = pal768[i * 3 + 2] =
                static_cast<uint8_t>(i);
        }
    }

    auto load = [&](const char* name, UiPiece* dst) -> bool {
        std::vector<uint8_t> data;
        for (MixFileClass* m : roots_) {
            data = m->Read_Deep(name);
            if (!data.empty()) {
                break;
            }
        }
        if (data.empty()) {
            return false;
        }
        ShpFile shp;
        if (!shp.Load(data.data(), data.size()) || shp.Frame_Count() <= 0) {
            return false;
        }
        dst->frames = shp.Frame_Count();
        dst->sprite.clear();
        for (int f = 0; f < dst->frames; ++f) {
            const std::vector<uint8_t>& px = shp.Frame_Pixels(f);
            const ShpFrameInfo& fi = shp.Frame_Info(f);
            if (fi.w <= 0 || fi.h <= 0 ||
                px.size() < static_cast<size_t>(fi.w) * fi.h) {
                dst->sprite.push_back(-1);
                continue;
            }
            if (f == 0) {
                dst->w = fi.w;
                dst->h = fi.h;
            }
            std::vector<uint8_t> rgba;
            SpriteCache::Index_To_RGBA(px.data(), fi.w * fi.h, pal768, &rgba);
            dst->sprite.push_back(
                renderer_.Upload_Sprite_RGBA(rgba.data(), fi.w, fi.h));
        }
        return true;
    };

    int ok = 0;
    ok += load("SIDE1.SHP", &ui_side1_) ? 1 : 0;
    ok += load("SIDE2.SHP", &ui_side2_) ? 1 : 0;
    ok += load("SIDE2B.SHP", &ui_side2b_) ? 1 : 0;
    ok += load("SIDE3.SHP", &ui_side3_) ? 1 : 0;
    ok += load("RADAR.SHP", &ui_radar_) ? 1 : 0;
    ok += load("CREDITS.SHP", &ui_credits_) ? 1 : 0;
    ok += load("Power.SHP", &ui_power_) ? 1 : 0;
    ok += load("SIDEBTTN.SHP", &ui_sidebttn_) ? 1 : 0;
    for (int i = 0; i < 4; ++i) {
        char n[24];
        std::snprintf(n, sizeof(n), "TAB%02d.SHP", i);
        ok += load(n, &ui_tab_[i]) ? 1 : 0;
    }
    for (int i = 0; i < 12; ++i) {
        char n[24];
        std::snprintf(n, sizeof(n), "Button%02d.SHP", i);
        ok += load(n, &ui_btn_[i]) ? 1 : 0;
    }
    std::printf("  界面贴图 %d 件就绪（侧栏 %d 宽，头部 %dx%d，雷达 %dx%d）\n", ok,
                kSidebarW, ui_side1_.w, ui_side1_.h, ui_radar_.w, ui_radar_.h);
    return ok > 0;
}

void GameShell::Draw_Ui(const UiPiece& p, int x, int y, int frame) {
    const int id = p.Frame(frame);
    if (id >= 0) {
        renderer_.Draw_Sprite(id, x, y, 1.0f);
    }
}

bool GameShell::Radar_Rect(int* x, int* y, int* w, int* h) const {
    if (!ui_radar_.ok()) {
        return false;
    }
    // 贴着屏幕底边钉死，原版就是这样
    const int rx = win_w_ - kSidebarW;
    const int ry = win_h_ - ui_radar_.h;
    if (x) *x = rx;
    if (y) *y = ry;
    if (w) *w = ui_radar_.w;
    if (h) *h = ui_radar_.h;
    return true;
}

bool GameShell::Radar_Inner(int* x, int* y, int* w, int* h) const {
    int rx = 0, ry = 0, rw = 0, rh = 0;
    if (!Radar_Rect(&rx, &ry, &rw, &rh)) {
        return false;
    }
    // RADAR.SHP 第 32 帧量出来的镂空区：x 12..153, y 1..108
    if (x) *x = rx + 13;
    if (y) *y = ry + 2;
    if (w) *w = 140;
    if (h) *h = 105;
    return true;
}

void GameShell::Draw_Top_Bar() {
    // 原版没有顶部资源条：资金/电力都在侧栏里。这里留着函数是为了不改
    // 调用点（Render 里那一串），但什么都不画。
}

void GameShell::Draw_Sidebar() {
    const int sx = win_w_ - kSidebarW;
    // 先铺一层底：贴图没到的地方不能露出战场的花地
    renderer_.Draw_Rect(sx, 0, kSidebarW, win_h_, kUiBackdrop);
    renderer_.Draw_Rect(sx, 0, 1, win_h_, kUiEdge);

    // 头部 + 页签。页签凹槽位置是从 SIDE1 实测的（见文件头注释）
    const int head_h = ui_side1_.ok() ? ui_side1_.h : 69;
    if (ui_side1_.ok()) {
        Draw_Ui(ui_side1_, sx, 0);
    }
    for (int i = 0; i < 4; ++i) {
        if (!ui_tab_[i].ok()) {
            continue;
        }
        // 第 1 帧是"选中"态（平均亮度最高，实测 (93,160,204) vs 常态 (56,104,192)）
        Draw_Ui(ui_tab_[i], sx + 26 + i * 29, 42, (i == sidebar_tab_) ? 1 : 0);
    }

    // ---- 纵向顺序（自上而下，和原版一致）----
    //   SIDE1    头部：蓝色面板 + 4 个页签
    //   CREDITS  资金条（原版资金就在页签正下方，不在屏幕底部）
    //   SIDE2    建造格，两列一行，一直铺到电力带
    //   SIDE3    电力/鹰徽带（原版电力表在雷达正上方）
    //   RADAR    贴屏幕底边
    int y = head_h;

    if (ui_credits_.ok()) {
        Draw_Ui(ui_credits_, sx, y);
        // 数字精灵还没接（要 SIDEFNT3 位图字体），先用色带占位
        renderer_.Draw_Rect(sx + 8, y + 5, 60, 6, kUiGold);
        y += ui_credits_.h;
    }

    // 底部自下往上钉死：雷达 -> 电力带
    const int radar_h = ui_radar_.ok() ? ui_radar_.h : 110;
    const int radar_y = win_h_ - radar_h;
    const int band_h = ui_side3_.ok() ? ui_side3_.h : 26;
    const int band_y = radar_y - band_h;
    if (ui_side3_.ok()) {
        Draw_Ui(ui_side3_, sx, band_y);
    }
    // 电力表画在电力带的左侧蓝底上（原版电力表就长这样）
    if (ui_power_.ok()) {
        // 有富余画第 0 帧、欠费画第 1 帧（原版这 2 帧就是这两种状态）
        Draw_Ui(ui_power_, sx + 4, band_y + (band_h - ui_power_.h) / 2,
                power_warn_ ? 1 : 0);
    }

    // 中间铺建造格：一行 50 像素、两列凹槽在 x = 24 / 86。
    // 最后一行**贴着电力带对齐**，否则余数会留出一条黑缝。
    const int row_h = ui_side2_.ok() ? ui_side2_.h : 50;
    if (row_h > 0 && band_y > y) {
        int gy = y;
        while (gy + row_h <= band_y) {
            if (ui_side2_.ok()) {
                Draw_Ui(ui_side2_, sx, gy);
            } else {
                renderer_.Draw_Rect(sx + 24, gy + 5, 60, 40, kUiSlot);
                renderer_.Draw_Rect(sx + 86, gy + 5, 60, 40, kUiSlot);
            }
            gy += row_h;
        }
        if (gy < band_y) {
            const int tail = band_y - row_h;
            if (tail >= y) {
                const UiPiece& row = ui_side2b_.ok() ? ui_side2b_ : ui_side2_;
                Draw_Ui(row, sx, tail);
            }
        }
    }

    // 光标命令提示（K 修理 / L 变卖）
    if (cursor_mode_ != 0) {
        const float c[4] = {1.0f, 0.3f, 0.3f, 1.0f};
        renderer_.Draw_Rect(sx + 4, y + 2, kSidebarW - 8, 8, c);
    }
}

void GameShell::Draw_Radar() {
    int x = 0, y = 0, size = 0, size_h = 0;
    if (!Radar_Inner(&x, &y, &size, &size_h)) {
        // 没有雷达贴图就退回自绘（至少别让功能消失）
        x = win_w_ - kSidebarW + 8;
        y = win_h_ - kRadarSize + 8;
        size = kRadarSize - 16;
        size_h = size;
        renderer_.Draw_Rect(x, y, size, size_h, kUiRadarFog);
        renderer_.Draw_Rect_Outline(x, y, size, size_h, kUiEdge, 1);
    } else {
        renderer_.Draw_Rect(x, y, size, size_h, kUiRadarFog);
    }

    // 整张地图等比压进显示区，所以格 -> 点就是线性映射
    if (map_.Width() <= 0 || map_.Height() <= 0) {
        return;
    }
    const float scalex = static_cast<float>(size) / map_.Width();
    const float scaley = static_cast<float>(size_h) / map_.Height();

    // 当前视口在小地图上的位置
    if (terrain_w_ > 0 && terrain_h_ > 0) {
        const float vw = static_cast<float>(win_w_ - kSidebarW) / camera_.Scale();
        const float vh = static_cast<float>(win_h_ - kTopBarH) / camera_.Scale();
        const int rx = x + static_cast<int>(camera_.X() / terrain_w_ * size);
        const int ry = y + static_cast<int>(camera_.Y() / terrain_h_ * size_h);
        const int rw = (vw / terrain_w_ * size < 4.0f)
                           ? 4 : static_cast<int>(vw / terrain_w_ * size);
        const int rh = (vh / terrain_h_ * size_h < 4.0f)
                           ? 4 : static_cast<int>(vh / terrain_h_ * size_h);
        renderer_.Draw_Rect_Outline(rx, ry, rw, rh, kUiRadarView, 1);
    }

    for (const Object& o : world_.Objects()) {
        if (!o.Is_Techno()) {
            continue;                      // 树和路灯不上小地图
        }
        const int dx = x + static_cast<int>(static_cast<float>(o.x) * scalex);
        const int dy = y + static_cast<int>(static_cast<float>(o.y) * scaley);
        renderer_.Draw_Rect(dx, dy, 2, 2, o.selected ? kUiSelect : kUiRadarDot);
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

/// 屏幕 -> 连续格坐标。先把屏幕换算成画布像素，再反解等距菱形。
bool GameShell::Pick_Cell_F(int sx, int sy, float* fx, float* fy) const {
    if (terrain_w_ <= 0) {
        return false;
    }
    if (sx < 0 || sx >= win_w_ - kSidebarW || sy < kTopBarH || sy >= win_h_) {
        return false;
    }
    float wx = 0.0f, wy = 0.0f;
    camera_.Screen_To_World(static_cast<float>(sx), static_cast<float>(sy), &wx, &wy);
    // 反解：cx - cy = (x-ox)/30, cx + cy = (y-oy)/15（不含高度，
    // 高度只影响显示位置，拾取时按 level 迭代校正一次就够）
    const float a = (wx - kCellHalfW -
                     static_cast<float>(map_renderer_.Origin_X())) /
                    kCellHalfW;
    const float b = (wy - kCellHalfH -
                     static_cast<float>(map_renderer_.Origin_Y())) /
                    kCellHalfH;
    float cx = (a + b) * 0.5f;
    float cy = (b - a) * 0.5f;
    // 高度校正：按当前格的高度把 y 往下补回来再解一次
    const int lvl = Cell_Level(static_cast<int>(cx), static_cast<int>(cy));
    const float b2 = (wy - kCellHalfH +
                      static_cast<float>(lvl) * kLevelHeightPx -
                      static_cast<float>(map_renderer_.Origin_Y())) /
                     kCellHalfH;
    cx = (a + b2) * 0.5f;
    cy = (b2 - a) * 0.5f;
    if (cx < -0.5f || cy < -0.5f || cx > map_.Width() - 0.5f ||
        cy > map_.Height() - 0.5f) {
        return false;
    }
    *fx = cx;
    *fy = cy;
    return true;
}

// ---------------------------------------------------------------------------
// 快捷键
//
// 这张表是按《红色警戒 2 / 尤里的复仇》官方手册 + 玩家实测整理的，
// 不是我自己拍的。整理时几个容易搞混的地方：
//   * Q/W/E/R 是侧栏四个页签（建筑 / 防御 / 步兵 / 车辆）。
//   * M / N 的"上一个/下一个"两份资料说反了，按手册（defkey 那份）取
//     N = 下一个部队、M = 上一个部队。
//   * Ctrl + 数字 = 存编队，数字 = 选编队，**再按一次同一个数字 = 居中**。
//   * Shift + 数字 = 加进编队。
//   * Ctrl + 点击 = 强制攻击；Alt + 点击 = 强制移动；Ctrl+Shift = 移动攻击。
// ---------------------------------------------------------------------------

void GameShell::On_Key_Down(int vk, bool ctrl, bool shift) {
    if (vk >= 0 && vk < 256) {
        keys_down_[vk] = true;
    }

    // Ctrl+F1..F4 存书签 / F1..F4 去书签
    if (vk >= VK_F1 && vk <= VK_F4) {
        const int slot = vk - VK_F1;
        if (ctrl) {
            // 存当前视口中心
            const float cxw = camera_.X() +
                              static_cast<float>(win_w_ - kSidebarW) * 0.5f /
                                  camera_.Scale();
            const float cyw = camera_.Y() +
                              static_cast<float>(win_h_ - kTopBarH) * 0.5f /
                                  camera_.Scale();
            bookmarks_[slot][0] = cxw;
            bookmarks_[slot][1] = cyw;
            bookmark_set_[slot] = true;
            std::printf("书签 %d 已存\n", slot + 1);
        } else if (bookmark_set_[slot]) {
            camera_.Set(bookmarks_[slot][0] -
                            static_cast<float>(win_w_ - kSidebarW) * 0.5f /
                                camera_.Scale(),
                        bookmarks_[slot][1] -
                            static_cast<float>(win_h_ - kTopBarH) * 0.5f /
                                camera_.Scale());
        }
        return;
    }

    // 数字键：编队
    if (vk >= '0' && vk <= '9') {
        const int slot = (vk == '0') ? 9 : (vk - '1');
        if (ctrl) {
            world_.Team_Set(slot);
            std::printf("编队 %d：%d 个单位\n", slot + 1, world_.Selected_Count());
        } else if (shift) {
            world_.Team_Add(slot);
        } else {
            world_.Team_Select(slot);
        }
        return;
    }

    if (ctrl) {
        return;                      // 其余 Ctrl 组合先不处理
    }

    switch (vk) {
        case 'Q': sidebar_tab_ = 0; break;   // 建筑页
        case 'W': sidebar_tab_ = 1; break;   // 防御页
        case 'E': sidebar_tab_ = 2; break;   // 步兵页
        case 'R': sidebar_tab_ = 3; break;   // 车辆页

        case 'S': world_.Order_Stop(); break;      // 停止
        case 'G': world_.Order_Guard(); break;     // 警戒
        case 'X': world_.Order_Scatter(); break;   // 散开
        case 'D': world_.Order_Deploy(); break;    // 部署

        case 'T': world_.Select_Same_Type(); break;    // 选同类型
        case 'P': world_.Select_All_Combat(); break;   // 集结全部战斗部队
        case 'U': world_.Select_By_Health(true); break;// 按生命值选（最低的）
        case 'Y': world_.Select_By_Health(false); break;
        case 'N': Cycle_Selection(1); break;        // 下一个部队
        case 'M': Cycle_Selection(-1); break;       // 上一个部队

        case 'H': {                                  // 回主基地
            float hx = 0.0f, hy = 0.0f;
            if (!world_.Home_Cell(&hx, &hy) && !world_.Spawn_Cell(0, &hx, &hy)) {
                break;
            }
            world_.Request_Camera(hx, hy);
            break;
        }
        case VK_SPACE: {                             // 去最近的雷达事件
            // 没有事件系统前，退化成"去最近的战斗单位"
            const Object* best = nullptr;
            for (const Object& o : world_.Objects()) {
                if (o.selectable && o.mission != Mission::Sleep) {
                    best = &o;
                    break;
                }
            }
            if (best) {
                world_.Request_Camera(best->x, best->y);
            }
            break;
        }
        case 'F': follow_ = !follow_; break;         // 跟随镜头
        case 'K': cursor_mode_ = (cursor_mode_ == 1) ? 0 : 1; break;  // 修理
        case 'L': cursor_mode_ = (cursor_mode_ == 2) ? 0 : 2; break;  // 变卖

        case VK_NUMPAD5:                             // 回屏幕中央
        case VK_CLEAR: {
            const float cxw = static_cast<float>(terrain_w_) * 0.5f;
            const float cyw = static_cast<float>(terrain_h_) * 0.5f;
            camera_.Set(cxw - static_cast<float>(win_w_ - kSidebarW) * 0.5f /
                                  camera_.Scale(),
                        cyw - static_cast<float>(win_h_ - kTopBarH) * 0.5f /
                                  camera_.Scale());
            break;
        }
        case VK_ESCAPE:
            world_.Select_None();
            cursor_mode_ = 0;
            break;
        case VK_ADD:      camera_.Zoom_By(1.25f); break;
        case VK_SUBTRACT: camera_.Zoom_By(0.8f); break;
        default: break;
    }
    (void)shift;
}

void GameShell::On_Key_Up(int vk) {
    if (vk >= 0 && vk < 256) {
        keys_down_[vk] = false;
    }
}

/// M / N：在当前可选项里循环选下一个（上一个）。
void GameShell::Cycle_Selection(int dir) {
    const std::vector<Object>& objs = world_.Objects();
    if (objs.empty()) {
        return;
    }
    int cur = -1;
    for (size_t i = 0; i < objs.size(); ++i) {
        if (objs[i].selected) {
            cur = static_cast<int>(i);
            break;
        }
    }
    int next = cur;
    for (int step = 0; step < static_cast<int>(objs.size()); ++step) {
        next += dir;
        if (next < 0) next = static_cast<int>(objs.size()) - 1;
        if (next >= static_cast<int>(objs.size())) next = 0;
        if (objs[static_cast<size_t>(next)].selectable) {
            break;
        }
    }
    world_.Select_None();
    world_.Objects()[static_cast<size_t>(next)].selected = true;
    world_.Request_Camera(objs[static_cast<size_t>(next)].x,
                          objs[static_cast<size_t>(next)].y);
}

void GameShell::On_Mouse_Move(int x, int y) {
    mouse_x_ = x;
    mouse_y_ = y;
}

void GameShell::On_Mouse_Down(int button, int x, int y, bool shift) {
    mouse_x_ = x;
    mouse_y_ = y;
    if (button == 0) {
        dragging_ = true;
        drag_x0_ = x;
        drag_y0_ = y;
        drag_shift_ = shift;
    }
}

void GameShell::On_Mouse_Up(int button, int x, int y) {
    mouse_x_ = x;
    mouse_y_ = y;

    if (button == 0 && dragging_) {
        dragging_ = false;
        // 拖动距离小于 4 像素算"点选"，否则算"框选"。
        // 原版也是这个判据 —— 没有它，每次点单位都会变成一次 0 面积的框选。
        const int dx = x - drag_x0_;
        const int dy = y - drag_y0_;
        if (dx * dx + dy * dy < 16) {
            float fx = 0.0f, fy = 0.0f;
            if (Pick_Cell_F(x, y, &fx, &fy)) {
                world_.Select_At(fx, fy, drag_shift_);
            } else if (!drag_shift_) {
                world_.Select_None();
            }
        } else {
            float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
            if (Pick_Cell_F(drag_x0_, drag_y0_, &x0, &y0) &&
                Pick_Cell_F(x, y, &x1, &y1)) {
                const int n = world_.Select_In_Rect(x0, y0, x1, y1, drag_shift_);
                std::printf("框选 (%.1f,%.1f)->(%.1f,%.1f) 选中 %d\n", x0, y0, x1, y1, n);
            }
        }
        return;
    }

    if (button == 1) {
        // 右键：命令。Ctrl = 强制攻击，Alt = 强制移动，Ctrl+Shift = 移动攻击。
        float fx = 0.0f, fy = 0.0f;
        if (!Pick_Cell_F(x, y, &fx, &fy)) {
            return;
        }
        const int hit = world_.Pick_At(fx, fy);
        const bool ctrl = keys_down_[VK_CONTROL];
        const bool alt = keys_down_[VK_MENU];
        if (hit >= 0 && (ctrl || !alt)) {
            world_.Order_Attack(hit);
        } else if (ctrl && (keys_down_[VK_SHIFT])) {
            world_.Order_Attack_Move(fx, fy);
        } else {
            world_.Order_Move(fx, fy);
        }
        // 右键同时取消光标命令（原版行为）
        cursor_mode_ = 0;
    }
}

// ---------------------------------------------------------------------------
// 行为自检
// ---------------------------------------------------------------------------

bool GameShell::Self_Test() {
    bool ok = true;
    auto check = [&ok](bool cond, const char* what) {
        std::printf("  [%s] %s\n", cond ? "OK" : "FAIL", what);
        if (!cond) ok = false;
    };

    std::printf("== 对象层 ==\n");
    int techno = 0;
    for (const Object& o : world_.Objects()) {
        if (o.Is_Techno()) ++techno;
    }
    check(world_.Count() > 0, "地图里有对象");
    std::printf("     对象 %d 个，可交互 %d 个，阵营 %zu 个\n", world_.Count(),
                techno, world_.Houses().size());
    if (techno == 0) {
        // 官方图里有纯装饰的（Ice_Age / RiverRam 一个单位都没有，全是树和石头），
        // 这不是解析失败，跳过后面依赖单位的检查。
        std::printf("     （纯装饰地图，没有可交互对象，跳过单位相关检查）\n");
        Render();
        check(objects_drawn_ > 0, "这一帧画出了对象");
        std::printf(ok ? "[OK] 行为自检全过\n" : "[x] 行为自检有不过的\n");
        return ok;
    }
    check(techno > 0, "其中有可交互对象（车/兵/建筑/飞机）");

    std::printf("== 选择 ==\n");
    // 框选整张地图，应该把所有可交互对象选上
    const int n_box = world_.Select_In_Rect(-1.0f, -1.0f,
                                            static_cast<float>(map_.Width()),
                                            static_cast<float>(map_.Height()), false);
    check(n_box == techno, "框选全图 == 全部可交互对象");
    std::printf("     框选 %d / 可交互 %d\n", n_box, techno);

    world_.Select_None();
    check(world_.Selected_Count() == 0, "取消选择后没有选中");

    // 点选：拿第一个可交互对象的位置去点
    const Object* first = nullptr;
    for (const Object& o : world_.Objects()) {
        if (o.Is_Techno()) {
            first = &o;
            break;
        }
    }
    if (first) {
        const int hit = world_.Pick_At(first->x, first->y);
        check(hit == first->id, "点选能命中脚下那个对象");
        world_.Select_At(first->x, first->y, false);
        check(world_.Selected_Count() == 1, "点选后正好选中 1 个");
    }

    std::printf("== 命令与逻辑帧 ==\n");
    // 挑一个能动的单位，下令移动，推进逻辑帧看它有没有真的走过去
    Object* mover = nullptr;
    for (Object& o : world_.Objects()) {
        if (o.Is_Techno() && o.speed > 0.0f && o.mission != Mission::Sleep &&
            o.mission != Mission::Sticky) {
            mover = &o;
            break;
        }
    }
    if (mover != nullptr) {
        world_.Select_None();
        mover->selected = true;
        const float x0 = mover->x;
        const float y0 = mover->y;
        const float tx = x0 + 3.0f;
        const float ty = y0 + 2.0f;
        const int ordered = world_.Order_Move(tx, ty);
        check(ordered == 1, "移动命令下达成功");
        const float before = std::sqrt((x0 - tx) * (x0 - tx) + (y0 - ty) * (y0 - ty));
        for (int i = 0; i < kLogicFps * 3; ++i) {   // 3 秒 = 45 个逻辑帧
            world_.Update(kLogicDt);
        }
        const float after = std::sqrt((mover->x - tx) * (mover->x - tx) +
                                      (mover->y - ty) * (mover->y - ty));
        check(after < before - 0.5f, "推进 3 秒后离目标更近了");
        std::printf("     距目标 %.2f -> %.2f 格\n", before, after);
        check(mover->mission == Mission::Move || after < 0.05f,
              "任务状态是 Move（或已到达）");
    } else {
        std::printf("     （这张图没有能动的单位，跳过）\n");
    }

    std::printf("== 编队 ==\n");
    int combat = world_.Select_All_Combat();
    if (combat == 0) {
        std::printf("     （这张图没有车辆/步兵，跳过编队检查）\n");
    } else {
        world_.Team_Set(0);
        world_.Select_None();
        const int back = world_.Team_Select(0);
        check(back == combat, "Ctrl+1 存编队、1 取回，数量一致");
        std::printf("     编队 1：%d 个单位\n", back);
    }

    std::printf("== 快捷键 ==\n");
    const int tab0 = sidebar_tab_;
    On_Key_Down('W', false, false);
    On_Key_Down('E', false, false);
    check(sidebar_tab_ == 2, "E 切到步兵页");
    On_Key_Down('R', false, false);
    check(sidebar_tab_ == 3, "R 切到车辆页");
    On_Key_Down('Q', false, false);
    check(sidebar_tab_ == 0, "Q 切回建筑页");
    (void)tab0;

    On_Key_Down('K', false, false);
    check(cursor_mode_ == 1, "K 进入修理模式");
    On_Key_Down('L', false, false);
    check(cursor_mode_ == 2, "L 进入变卖模式");
    On_Key_Down(VK_ESCAPE, false, false);
    check(cursor_mode_ == 0, "Esc 取消光标命令");

    // 编队热键：Ctrl+2 存、2 取
    if (world_.Select_All_Combat() > 0) {
        world_.Team_Set(1);
        world_.Select_None();
        On_Key_Down('2', false, false);
        check(world_.Selected_Count() > 0, "数字键 2 取回编队");
    }

    std::printf("== 渲染 ==\n");
    world_.Select_None();
    Render();
    check(objects_drawn_ > 0, "这一帧画出了对象");
    std::printf("     画出 %d 个对象\n", objects_drawn_);

    std::printf(ok ? "[OK] 行为自检全过\n" : "[x] 行为自检有不过的\n");
    return ok;
}

bool GameShell::Self_Test_Voxel_GPU() {
    bool ok = true;
    auto check = [&ok](bool cond, const char* what) {
        std::printf("  [%s] %s\n", cond ? "OK" : "FAIL", what);
        if (!cond) ok = false;
    };
    std::printf("== 体素：GPU 光栅化 vs CPU 软光栅 ==\n");

    // 拿地图里第一个体素单位当样本（比硬编码一个名字稳，换图也不怕）
    const char* type = nullptr;
    for (const Object& o : world_.Objects()) {
        if (sprites_.Is_Voxel(o.type.c_str())) {
            type = o.type.c_str();
            break;
        }
    }
    if (type == nullptr) {
        std::printf("     （这张图没有体素单位，跳过）\n");
        return true;
    }
    std::printf("     样本单位：%s（朝向 0，即 yaw=0）\n", type);

    std::vector<uint8_t> cpu;
    int cw = 0, ch = 0;
    float cx0 = 0.0f, cy0 = 0.0f;
    if (!sprites_.Bake_CPU_Reference(type, 0.0f, player_color_, &cpu, &cw, &ch,
                                     &cx0, &cy0)) {
        check(false, "CPU 参考图渲染成功");
        return false;
    }
    std::printf("     CPU 参考图 %dx%d，画布原点 (%.3f, %.3f)\n", cw, ch, cx0, cy0);

    std::printf("     烘焙前设备状态 0x%08lX（0 = 只是还没烘过，不等于有问题）\n",
                renderer_.Removal_Reason());
    sprites_.Reset_Budget(4);
    const ObjectSprite* sp = sprites_.Get(type, 0, player_color_);
    if (sp == nullptr || sp->sprite_id < 0) {
        check(false, "GPU 烘焙成功");
        return false;
    }
    std::printf("     GPU 烘焙图 %dx%d，画布原点 (%.3f, %.3f)\n", sp->w, sp->h,
                sp->bbox[0], sp->bbox[1]);

    // 把 GPU 精灵单独画一帧回读。左上角留 8px，免得贴边被裁。
    const int kOff = 8;
    if (sp->w + kOff * 2 > win_w_ || sp->h + kOff * 2 > win_h_) {
        check(false, "GPU 精灵尺寸没超过视口");
        return false;
    }
    std::printf("     烘焙后设备状态 0x%08lX\n", renderer_.Removal_Reason());
    renderer_.Request_Capture();
    // 底色用深蓝而不是黑：清屏是黑的话，"着色器吐出全黑"和"一个像素都没画"
    // 在回读图里长得一模一样，白绕一圈。深蓝能把这两件事分开。
    const float clear[4] = {0.0f, 0.0f, 0.25f, 1.0f};
    renderer_.Begin_Frame(clear);
    renderer_.Draw_Sprite(sp->sprite_id, kOff, kOff, 1.0f);
    renderer_.End_Frame();
    std::printf("     出图后设备状态 0x%08lX\n", renderer_.Removal_Reason());
    std::vector<uint8_t> cap;
    int gw = 0, gh = 0;
    if (!renderer_.Get_Capture(cap, gw, gh)) {
        check(false, "GPU 出图能回读");
        return false;
    }

    // 诊断：把精灵那一块从大帧里裁出来，连同 CPU 参考图一起 dump 成裸 RGBA。
    // 像素对不上时，看这两张图比看百分比有用得多。
    int gpu_nonblack = 0;
    {
        std::vector<uint8_t> cut(static_cast<size_t>(sp->w) * sp->h * 4, 0);
        for (int y = 0; y < sp->h; ++y) {
            const size_t g = (static_cast<size_t>(y + kOff) * gw + kOff) * 4;
            std::memcpy(&cut[static_cast<size_t>(y) * sp->w * 4], &cap[g],
                        static_cast<size_t>(sp->w) * 4);
        }
        long long sum[3] = {0, 0, 0};
        for (size_t i = 0; i < cut.size(); i += 4) {
            // 底色是 (0,0,64)；和它不一样就说明这里被画过了
            if (cut[i] != 0 || cut[i + 1] != 0 || cut[i + 2] != 64) {
                ++gpu_nonblack;
                sum[0] += cut[i];
                sum[1] += cut[i + 1];
                sum[2] += cut[i + 2];
            }
        }
        if (gpu_nonblack > 0) {
            std::printf("     被画过的像素 %d，平均色 (%lld, %lld, %lld)\n",
                        gpu_nonblack, sum[0] / gpu_nonblack, sum[1] / gpu_nonblack,
                        sum[2] / gpu_nonblack);
        }
        auto dump = [](const char* path, const uint8_t* p, int w, int h) {
            FILE* f = std::fopen(path, "wb");
            if (f == nullptr) {
                return;
            }
            std::fwrite(p, 1, static_cast<size_t>(w) * h * 4, f);
            std::fclose(f);
        };
        dump("build/vxl_cpu.raw", cpu.data(), cw, ch);
        dump("build/vxl_gpu.raw", cut.data(), sp->w, sp->h);
        std::printf("     GPU 画布非黑像素 %d（0 = 着色器没出东西）\n", gpu_nonblack);
        std::printf("     已 dump build/vxl_cpu.raw(%dx%d) / build/vxl_gpu.raw(%dx%d)\n",
                    cw, ch, sp->w, sp->h);
    }

    // 对位：CPU 像素 (i,j) 的投影坐标是 (cx0 + i/s, cy0 + j/s)，
    // 换到 GPU 画布上就是 u = i + (cx0 - bbox[0])*s。
    //
    // 【为什么要搜偏移而不是算一次】两条路的画布原点是**故意不一样**的：
    // CPU 用逐体素求出的精确包围盒，GPU 用每根肢体 AABB 的 8 个角求的保守
    // 上界（O(肢体数) 而不是 O(体素数)，代价是边缘多一圈透明像素）。
    // 两个原点差的是个非整数，量化到像素格上就会差 0~2 个像素。
    // 所以这里在 ±3 像素里搜一遍最佳对齐 —— 对齐之后剩下多少差异，
    // 才是真正需要解释的差异。
    //
    // 别叫 near —— windows.h 里 `#define near` 是个宏，用作变量名会炸。
    int total = 0;
    for (int j = 0; j < ch; ++j) {
        for (int i = 0; i < cw; ++i) {
            if (cpu[(static_cast<size_t>(j) * cw + i) * 4 + 3] != 0) {
                ++total;
            }
        }
    }
    if (total == 0) {
        check(false, "CPU 参考图有非透明像素");
        return false;
    }

    // CPU 这边某像素的 8 邻域是否全不透明 —— 用来把"轮廓上那一圈"
    // 挑出来单独看：那圈天然会有 1 像素级的差异（画布原点差的是个非整数），
    // 混在一起算会把真正的内容差异淹掉。
    auto interior = [&](int i, int j) {
        for (int dj = -1; dj <= 1; ++dj) {
            for (int di = -1; di <= 1; ++di) {
                const int x = i + di, y = j + dj;
                if (x < 0 || y < 0 || x >= cw || y >= ch) {
                    return false;
                }
                if (cpu[(static_cast<size_t>(y) * cw + x) * 4 + 3] == 0) {
                    return false;
                }
            }
        }
        return true;
    };

    // 返回"误差 <=2 的像素数"，同时给出完全一致的个数与参与比较的个数。
    auto score = [&](int dx, int dy, int* exact_out, int* cmp_out) {
        int exact = 0, near_ok = 0, cmp = 0;
        for (int j = 0; j < ch; ++j) {
            for (int i = 0; i < cw; ++i) {
                const size_t o = (static_cast<size_t>(j) * cw + i) * 4;
                if (cpu[o + 3] == 0) {
                    continue;   // CPU 这边是透明的，不比
                }
                const int u = i + dx;
                const int v = j + dy;
                if (u < 0 || v < 0 || u >= sp->w || v >= sp->h) {
                    continue;
                }
                ++cmp;
                const size_t g =
                    (static_cast<size_t>(v + kOff) * gw + (u + kOff)) * 4;
                int dmax = 0;
                for (int c = 0; c < 3; ++c) {
                    const int d = std::abs(static_cast<int>(cpu[o + c]) -
                                           static_cast<int>(cap[g + c]));
                    if (d > dmax) dmax = d;
                }
                if (dmax == 0) ++exact;
                if (dmax <= 2) ++near_ok;
            }
        }
        if (exact_out) *exact_out = exact;
        if (cmp_out) *cmp_out = cmp;
        return near_ok;
    };

    // 先按解析式算出偏移，再在它周围搜 —— 只在 ±3 里搜会漏掉真实偏移
    // （STANG 的偏移是 17,45，搜不到就得出"全不对"的错误结论）。
    const float s = sp->scale;
    const int base_dx = static_cast<int>(std::lround((cx0 - sp->bbox[0]) * s));
    const int base_dy = static_cast<int>(std::lround((cy0 - sp->bbox[1]) * s));
    int best_dx = base_dx, best_dy = base_dy, best_near = -1;
    for (int dy = base_dy - 3; dy <= base_dy + 3; ++dy) {
        for (int dx = base_dx - 3; dx <= base_dx + 3; ++dx) {
            const int n = score(dx, dy, nullptr, nullptr);
            if (n > best_near) {
                best_near = n;
                best_dx = dx;
                best_dy = dy;
            }
        }
    }
    std::printf("     解析偏移 (%d, %d) -> 搜到最佳 (%d, %d)\n", base_dx, base_dy,
                best_dx, best_dy);

    // 只比"内部"像素：这些像素的 3×3 邻域在 CPU 图上全是不透明的，
    // 不受轮廓对齐误差影响。差异只剩下"8 位量化 CPU 截断 / GPU 四舍五入"。
    int inner_total = 0, inner_near = 0, inner_exact = 0;
    for (int j = 0; j < ch; ++j) {
        for (int i = 0; i < cw; ++i) {
            if (!interior(i, j)) {
                continue;
            }
            const int u = i + best_dx;
            const int v = j + best_dy;
            if (u < 0 || v < 0 || u >= sp->w || v >= sp->h) {
                continue;
            }
            const size_t o = (static_cast<size_t>(j) * cw + i) * 4;
            const size_t g = (static_cast<size_t>(v + kOff) * gw + (u + kOff)) * 4;
            int dmax = 0;
            for (int c = 0; c < 3; ++c) {
                const int d = std::abs(static_cast<int>(cpu[o + c]) -
                                       static_cast<int>(cap[g + c]));
                if (d > dmax) dmax = d;
            }
            ++inner_total;
            if (dmax == 0) ++inner_exact;
            if (dmax <= 2) ++inner_near;
        }
    }
    if (inner_total == 0) {
        check(false, "有可比的内部像素");
        return false;
    }
    const double r_inner = static_cast<double>(inner_near) / inner_total;
    std::printf("     总非透明 %d；内部像素 %d：误差<=2 的 %.3f%%，完全一致 %.2f%%\n",
                total, inner_total, r_inner * 100.0,
                100.0 * inner_exact / inner_total);
    check(r_inner >= 0.99, "内部像素 GPU 与 CPU 一致（误差<=2）>= 99%");

    // 轮廓覆盖率：两张图的"被画过"像素数应该很接近（画布大小不同，
    // 但内容面积应该基本一样）。
    const double cover = static_cast<double>(gpu_nonblack) / total;
    std::printf("     轮廓面积 GPU/CPU = %d/%d = %.3f\n", gpu_nonblack, total, cover);
    check(cover > 0.95 && cover < 1.1, "轮廓面积与 CPU 相当（0.95~1.10）");

    // 8 个朝向都得烘得出来，而且不能全都长一样（证明 yaw 真的生效了）
    int distinct = 0;
    int prev_id = -1;
    for (int f = 0; f < 256; f += 32) {
        sprites_.Reset_Budget(4);
        const ObjectSprite* s2 = sprites_.Get(type, f, player_color_);
        if (s2 == nullptr || s2->sprite_id < 0) {
            check(false, "朝向档能烘出来");
            break;
        }
        if (s2->sprite_id != prev_id) {
            ++distinct;
        }
        prev_id = s2->sprite_id;
    }
    check(distinct == 8, "8 个朝向各烘出一张（互不相同）");
    std::printf("     朝向数：%d/8\n", distinct);

    std::printf(ok ? "[OK] GPU 体素管线与 CPU 一致\n" : "[x] GPU 体素管线有偏差\n");
    return ok;
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
