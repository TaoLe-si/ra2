// GameShell.cpp

#include "game/GameShell.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include <windows.h>

#include "game/AudioDevice.h"
#include "game/SaveLoad.h"
#include "gfx/MouseShapeTable.h"
#include "gfx/PcxFile.h"
#include "gfx/ShpFile.h"
#include "io/FileSystem.h"
#include "map/Cell.h"

namespace ra2 {
namespace {

// 全局播放入口：World 通过 Set_Sound_Player 注入。
// 这里要从 World 里拿不到 roots_，所以走一个反向注入：
//   GameShell::Init 里用 s_sound_player_target 设进去，
//   World::Dispatch_TAction 命中 19/99/108/113 时再回调。
//   注意 s_sound_player_target 是指向 GameShell 实例的指针，World 完全不知道 GameShell。
struct Sound_Play_Ctx {
    const std::vector<MixFileClass*>* roots = nullptr;
};
static Sound_Play_Ctx s_audio_ctx;

void On_Sound_Request(const char* voc_name) {
    if (voc_name == nullptr || voc_name[0] == '\0') {
        return;
    }
    if (s_audio_ctx.roots == nullptr || s_audio_ctx.roots->empty()) {
        return;  // 没挂 MIX 也发声就是凭空响，留作 debug 桩
    }
    // 原版约定名：'_Voc_Xxx' / '_Amb_Xxx' / '_EVA_Xxx'。混音包名都带前缀 `_`，
    // 文件名约定是去掉前缀后小写、扩展名 .AUD。所以 _Voc_Cheer → cheer.aud。
    std::string base = voc_name;
    if (!base.empty() && base[0] == '_') {
        base.erase(0, 1);  // 去 `_`
    }
    // _Voc_Cheer → "Voc_Cheer" → 文件名大写开头小写续。Westwood 实际是全大写 + .AUD。
    // 规则："vocname.aud" 全小写找不到就用全大写 + .AUD。
    std::string lc_name = base;
    for (char& c : lc_name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::vector<uint8_t> aud;
    for (MixFileClass* m : *s_audio_ctx.roots) {
        aud = m->Read_Deep_By_ID(MixFileClass::CRC_Of((lc_name + ".aud").c_str()));
        if (!aud.empty()) break;
        aud = m->Read_Deep_By_ID(MixFileClass::CRC_Of((base + ".AUD").c_str()));
        if (!aud.empty()) break;
    }
    if (aud.empty()) {
        std::printf("  [aud] %s 找不到 AUD（已尝试 %s.aud / %s.AUD）\n",
                    voc_name, lc_name.c_str(), base.c_str());
        return;
    }
    std::vector<uint8_t> wav = Aud_To_Wav(aud.data(), aud.size());
    if (wav.empty()) {
        std::printf("  [aud] %s AUD 解码失败（可能 ADPCM 或 magic 非 0）\n", voc_name);
        return;
    }
    Play_Wav_Memory(wav, voc_name);
    std::printf("  [aud] 播放 %s (%zu 字节)\n", voc_name, wav.size());
}

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

std::string Dir_Of_Path(const std::string& p) {
    const size_t slash = p.find_last_of("\\/");
    if (slash == std::string::npos) {
        return {};
    }
    return p.substr(0, slash + 1);
}

/// 从 MAPS01.MIX 抽出 Brief:ALL01 战役图到 out_path（证据：MIX 条目含该串）。
bool Extract_Campaign_All01(const std::string& game_dir, std::string* out_path,
                            std::string* err) {
    if (out_path == nullptr) {
        return false;
    }
    const char* names[] = {"MAPS01.MIX", "maps01.mix", "Maps01.mix"};
    MixFileClass mix;
    bool opened = false;
    for (const char* n : names) {
        const std::string p = game_dir + n;
        if (mix.Open(p.c_str())) {
            opened = true;
            break;
        }
    }
    if (!opened) {
        if (err) {
            *err = "打不开 MAPS01.MIX";
        }
        return false;
    }
    for (const MixEntry& e : mix.Entries()) {
        std::vector<uint8_t> data = mix.Read_Entry(e);
        if (data.size() < 64) {
            continue;
        }
        const char* b = reinterpret_cast<const char*>(data.data());
        const char* end = b + data.size();
        const char needle[] = "Brief:ALL01";
        bool hit = false;
        for (const char* p = b; p + sizeof(needle) - 1 <= end; ++p) {
            if (_strnicmp(p, needle, sizeof(needle) - 1) == 0) {
                hit = true;
                break;
            }
        }
        if (!hit) {
            continue;
        }
        char tmp[MAX_PATH];
        if (GetTempPathA(MAX_PATH, tmp) == 0) {
            if (err) {
                *err = "GetTempPath 失败";
            }
            return false;
        }
        std::string path = std::string(tmp) + "ra2_campaign_all01.map";
        HANDLE hf = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hf == INVALID_HANDLE_VALUE) {
            if (err) {
                *err = "写临时战役图失败";
            }
            return false;
        }
        DWORD written = 0;
        const BOOL ok = WriteFile(hf, data.data(),
                                  static_cast<DWORD>(data.size()), &written,
                                  nullptr);
        CloseHandle(hf);
        if (!ok || written != data.size()) {
            if (err) {
                *err = "写临时战役图不完整";
            }
            return false;
        }
        *out_path = path;
        return true;
    }
    if (err) {
        *err = "MAPS01.MIX 无 Brief:ALL01";
    }
    return false;
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
    // 原版 MouseClass 自己画 MOUSE.SHA，系统箭头要藏起来（Init Mouse @0x0052BA60）。
    if (hwnd != nullptr) {
        ShowCursor(FALSE);
        hide_os_cursor_ = true;
    }
    ready_ = true;
    return true;
}

bool GameShell::Load_Map(const std::vector<std::string>& mix_paths,
                         const char* map_path, std::string* err) {
    screen_ = GameScreen::Loading;
    pending_mixes_ = mix_paths;
    current_map_path_ = map_path ? map_path : "";
    paused_ = false;
    ask_abort_ = false;
    cursor_mode_ = 0;
    sidebar_tab_ = 0;
    cameo_scroll_ = 0;

    for (const std::string& p : mix_paths) {
        auto m = std::make_unique<MixFileClass>();
        if (!m->Open(p.c_str())) {
            if (err) *err = "打不开 " + p;
            return false;
        }
        roots_.push_back(m.get());
        mixes_.push_back(std::move(m));
    }

    // CSF：language.mix / langmd.mix 里的 ra2.csf / ra2md.csf（加载器 @0x734770）
    {
        std::vector<uint8_t> csf;
        for (MixFileClass* m : roots_) {
            csf = m->Read_Deep("ra2md.csf");
            if (csf.empty()) {
                csf = m->Read_Deep("ra2.csf");
            }
            if (!csf.empty()) {
                break;
            }
        }
        if (!csf.empty() && csf_.Load(csf.data(), csf.size())) {
            const std::string won = csf_.Get("TXT_SCENARIO_WON");
            const std::string lost = csf_.Get("TXT_SCENARIO_LOST");
            std::printf("  CSF 已载入（TXT_SCENARIO_WON=%s）\n",
                        won.empty() ? "?" : won.c_str());
            if (!lost.empty()) {
                std::printf("  TXT_SCENARIO_LOST=\"%s\"\n", lost.c_str());
            }
        }
        // 英文 CSF：FULLFNT3 只能画拉丁码点；MessageList 胜负条用英文键文案。
        {
            std::vector<uint8_t> en;
            for (MixFileClass* m : roots_) {
                en = m->Read_Deep("ra2.csf");
                if (!en.empty()) {
                    break;
                }
            }
            if (!en.empty()) {
                csf_en_.Load(en.data(), en.size());
            }
        }
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

    if (!sprites_.Bind(roots_)) {
        std::printf("[!] 精灵库绑定失败，装饰/单位 SHP 可能缺失\n");
    } else {
        sprites_.Set_Renderer(&renderer_);
        sprites_.Set_Remap(remap_);
        sprites_.Set_Theater(map_.Theater());
        std::printf("  精灵库就绪（单位 %d，覆盖物 %d）\n",
                    sprites_.Models().Unit_Count(),
                    sprites_.Models().Overlay_Count());
    }

    if (!map_renderer_.Bind(roots_, map_, err)) {
        return false;
    }
    map_renderer_.Set_Overlay_Images(sprites_.Models().Overlay_Images(),
                                     sprites_.Models().Overlay_New_Theater());
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
                    "取不到瓦片 %d，取到但全空 %d，SubTile 越界退 0 %d 次\n",
                    terrain_w_, terrain_h_, total, no_tile,
                    map_renderer_.Cells_Drawn(), map_renderer_.Tiles_Missing(),
                    map_renderer_.Cells_Empty(), map_renderer_.Sub_Clamped());
        {
            int sub_hist[16] = {};
            int sub_hi = 0;
            for (const IsoCell& c : map_.Cells()) {
                const int s = static_cast<int>(c.sub);
                if (s > sub_hi) {
                    sub_hi = s;
                }
                if (s >= 0 && s < 16) {
                    ++sub_hist[s];
                }
            }
            std::printf("  SubTile:");
            for (int i = 0; i < 16 && i <= sub_hi; ++i) {
                std::printf(" %d=%d", i, sub_hist[i]);
            }
            std::printf(" max=%d  Tile0=%s\n", sub_hi,
                        map_renderer_.Tile_File_Name(0).c_str());
        }
        std::printf("  OverlayPack %zu 字节 OverlayData %zu 字节，画上 %d 格，缺 SHP %d 种\n",
                    map_.Overlay().size(), map_.Overlay_Data().size(),
                    map_renderer_.Overlays_Drawn(),
                    map_renderer_.Overlays_Missing());
        // 空格的分布图：黑块到底是"地图边界外的空白"（正常）还是"中间破了个洞"
        // （解析错），看这张图一眼就知道。每个字符代表 2×4 格。
        const int iso_w = map_.Iso_Width(), H = map_.Height();
        if (iso_w > 0 && H > 0 && no_tile > 0) {
            std::printf("     空瓦片分布（. 有瓦片 / 空 没有）每格 2x4 单元：\n");
            for (int cy = 0; cy < H; cy += 4) {
                std::string line = "     ";
                for (int cx = 0; cx < iso_w; cx += 2) {
                    int filled = 0, n = 0;
                    for (int dy = 0; dy < 4 && cy + dy < H; ++dy) {
                        for (int dx = 0; dx < 2 && cx + dx < iso_w; ++dx) {
                            const size_t i =
                                static_cast<size_t>(cy + dy) *
                                    static_cast<size_t>(iso_w) +
                                static_cast<size_t>(cx + dx);
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

    // 进图就把这一局里会出现的精灵烘完。原版是载入阶段预渲的；
    // 每帧只补 4 张会让 200+ 棵树先变成色块铺满路面，看着像瓦片碎了。
    {
        sprites_.Reset_Budget(100000);
        for (const Object& o : world_.Objects()) {
            const int house_color = (o.house >= 0 && o.is_mine) ? player_color_ : 11;
            sprites_.Get(o.type.c_str(), o.facing, house_color);
        }
        std::printf("  进图预热精灵：建成 %d，缺失 %d\n",
                    sprites_.Built(), sprites_.Failed());
    }

    // 阵营侧栏素材：gamemd 0x00534FA0 挂 SIDEC/SIDENC；CREDITS/TOP/RADAR/SIDE 在 SIDEC。
    // 不挂的话 Read_Deep 会在 ra2.mix 深层里按条目序撞到错阵营的 SIDEC。
    if (!Mount_Player_Sidenc()) {
        std::printf("[!] 未挂上 SIDEC/SIDENC，界面可能串阵营\n");
    }
    std::printf("  玩家阵营 %s（Side=%d）\n",
                (world_.Player_House() >= 0 &&
                 world_.Player_House() < static_cast<int>(world_.Houses().size()))
                    ? world_.Houses()[static_cast<size_t>(world_.Player_House())].c_str()
                    : "?",
                Player_Side());

    // 原版界面贴图（侧栏/页签/雷达/资金条）。失败不致命，退回自绘的色块。
    if (!Load_UI()) {
        std::printf("[!] 界面贴图载入失败，侧栏退回色块\n");
    }
    // 起手资金来自 rules.ini 的 [General] StartCredits（原版默认 10000）
    credits_ = sprites_.Models().Start_Credits();
    std::printf("  起手资金 $%d\n", credits_);

    // 逻辑地图：Iso 网格 (2W-1)×H，与地形格一致。
    logic_map_.Init_Clear(map_.Iso_Width(), map_.Height());
    {
        int blocked = 0;
        for (const IsoCell& c : map_.Cells()) {
            CellClass* cell = logic_map_.Cell_At(
                CellStruct{static_cast<int16_t>(c.cx), static_cast<int16_t>(c.cy)});
            if (cell == nullptr) {
                continue;
            }
            cell->Set_Level(c.level);
            const TerrainTileRGBA* img = map_renderer_.Tile_RGBA(c.tile, c.sub);
            if (img != nullptr) {
                // TMP +41 → LandType：表 0x8288e4（0x00544C05）。
                cell->Set_Land(LandType_From_Tmp_Byte(img->land));
                if (!cell->Is_Clear_To_Move(cell->Land())) {
                    ++blocked;
                }
            }
        }
        std::printf("  逻辑地图 %dx%d，不可通行 %d 格\n", map_.Iso_Width(),
                    map_.Height(), blocked);
    }
    world_.Set_Logic_Map(&logic_map_);
    world_.Set_Map_File(&map_);
    world_.Set_Player_Credits(credits_);
    world_.Apply_Type_Stats(sprites_.Models());
    {
        // 打印 Land.Buildable（与 0x89ea60 表一致），便于核对放置拒因。
        static const char* kLand[12] = {
            "Clear", "Road", "Water", "Rock", "Wall", "Tiberium",
            "Beach", "Rough", "Ice", "Railroad", "Tunnel", "Weeds",
        };
        std::printf("  Land.Buildable:");
        for (int i = 0; i < 12; ++i) {
            if (sprites_.Models().Land_Buildable(i)) {
                std::printf(" %s", kLand[i]);
            }
        }
        std::printf(" | ShortGame=%s FogOfWar=%s MCVDeploy=%s\n",
                    sprites_.Models().Short_Game() ? "yes" : "no",
                    sprites_.Models().Fog_Of_War() ? "yes" : "no",
                    sprites_.Models().MCV_Deploy() ? "yes" : "no");
    }

    // 联机图（Arena）PlayerControl 有阵营但 [Units]/[Structures] 为空；
    // 原版开局在 Waypoints 0..7 为各座位投放 BaseUnit（MCV）。
    // 座位 = 房屋下标（跳过 Neutral/Special）；有对应 Waypoint 才投放。
    {
        int mine = 0;
        for (const Object& o : world_.Objects()) {
            if (o.is_mine && o.Is_Techno() && o.hp > 0) {
                ++mine;
            }
        }
        if (mine == 0) {
            float player_sx = 40.0f, player_sy = 40.0f;
            bool have_player_spawn = false;
            int seat = 0;
            for (size_t hi = 0; hi < world_.Houses().size(); ++hi) {
                const std::string& hn = world_.Houses()[hi];
                if (hn.empty() || hn == "Neutral" || hn == "Special") {
                    continue;
                }
                if (seat > 7) {
                    break;
                }
                float sx = 40.0f, sy = 40.0f;
                if (!world_.Spawn_Cell(seat, &sx, &sy)) {
                    ++seat;
                    continue;
                }
                const char* bu =
                    sprites_.Models().Base_Unit_For_Country(hn.c_str());
                if (bu == nullptr || bu[0] == '\0') {
                    bu = "AMCV";
                }
                const int id = world_.Spawn(bu, MapObjectKind::Unit,
                                            static_cast<int>(hi), sx, sy);
                std::printf("  联机开局：座位%d %s 投放 %s id=%d @ (%.1f,%.1f)\n",
                            seat, hn.c_str(), bu, id, sx, sy);
                if (static_cast<int>(hi) == world_.Player_House()) {
                    player_sx = sx;
                    player_sy = sy;
                    have_player_spawn = true;
                }
                ++seat;
            }
            // MCV 自动展开：电脑走 IQ>=Production → +0x1F3 → Unload @0x7409F8。
            // AIAutoDeployFrameDelay(Rules+0xE2C) 仅 INI/序列化，无 Unload 消费者。
            if (have_player_spawn) {
                Center_On_Cell(player_sx, player_sy);
            }
            // 联机空图：各参战座位起手资金 = rules StartCredits（地图 Credits=0）。
            const int start_c = sprites_.Models().Start_Credits();
            for (size_t hi = 0; hi < world_.Houses().size(); ++hi) {
                const std::string& hn = world_.Houses()[hi];
                if (hn.empty() || hn == "Neutral" || hn == "Special") {
                    continue;
                }
                if (static_cast<int>(hi) == world_.Player_House()) {
                    continue;
                }
                if (world_.House_Credits(static_cast<int>(hi)) <= 0) {
                    world_.Set_House_Credits(static_cast<int>(hi), start_c);
                }
            }
        }
    }

    Refresh_Power();
    Refresh_Sidebar();
    std::printf("  侧栏页 %d 可建 %zu（Q/W/E/R 切页）\n", sidebar_tab_,
                sidebar_items_.size());
    std::printf("  电力 产出 %d / 负荷 %d\n", world_.Power_Output(),
                world_.Power_Drain());

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
    // 已在出生点 Center_On_Cell 则保留；否则对准地图中心。
    float hx = 0.0f, hy = 0.0f;
    if (!world_.Home_Cell(&hx, &hy)) {
        const float half_vw =
            static_cast<float>(win_w_ - kSidebarW) * 0.5f / camera_.Scale();
        const float half_vh =
            static_cast<float>(win_h_ - kTopBarH) * 0.5f / camera_.Scale();
        camera_.Set(static_cast<float>(terrain_w_) * 0.5f - half_vw,
                    static_cast<float>(terrain_h_) * 0.5f - half_vh);
    } else {
        Center_On_Cell(hx, hy);
    }

    screen_ = GameScreen::Battle;
    // 把 MIX 根挂上 + 把音效回调注入 World。回调走 On_Sound_Request
    // （定义在文件顶部 anonymous ns），原版 0x750920 的"按名字拿 AUD
    // 转 PCM 再混音"这里压缩成"AUD → WAV → PlaySound"，
    // 不保证 1:1 等价，但至少能让 TAction 99/108/113 真出声。
    s_audio_ctx.roots = &roots_;
    world_.Set_Sound_Player(&On_Sound_Request);
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
    if (screen_ != GameScreen::Battle) {
        return;
    }

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
        if (world_.Outcome() != MatchOutcome::Playing) {
            logic_accum_ = 0.0f;
            break;
        }
        if (paused_) {
            logic_accum_ = 0.0f;
            break;
        }
        ++logic_frame_;
        world_.Update(kLogicDt);
        if (world_.Outcome() != last_outcome_) {
            last_outcome_ = world_.Outcome();
            if (last_outcome_ == MatchOutcome::Won) {
                const std::string s = csf_.Get("TXT_SCENARIO_WON");
                std::printf("  [UI] %s\n",
                            s.empty() ? "TXT_SCENARIO_WON" : s.c_str());
                // MessageList @0x6D4DB0 + EVA @0x752700（Flag_To_Win @0x4FCB4D）。
                outcome_msg_ = csf_en_.Get("TXT_SCENARIO_WON");
                if (outcome_msg_.empty()) {
                    outcome_msg_ = csf_en_.Get("TXT_VICTORIOUS");
                }
                if (outcome_msg_.empty()) {
                    outcome_msg_ = s;
                }
                world_.Dispatch_TAction(21, world_.Player_House(), 0,
                                        "EVA_MissionAccomplished");
            } else if (last_outcome_ == MatchOutcome::Lost) {
                const std::string s = csf_.Get("TXT_SCENARIO_LOST");
                std::printf("  [UI] %s\n",
                            s.empty() ? "TXT_SCENARIO_LOST" : s.c_str());
                outcome_msg_ = csf_en_.Get("TXT_SCENARIO_LOST");
                if (outcome_msg_.empty()) {
                    outcome_msg_ = csf_en_.Get("TXT_LOST");
                }
                if (outcome_msg_.empty()) {
                    outcome_msg_ = s;
                }
                world_.Dispatch_TAction(21, world_.Player_House(), 0,
                                        "EVA_MissionFailed");
            }
        }
        // 生产推进：BuildSpeed= 分钟/1000 信贷 → 逻辑帧。
        if (build_active_) {
            // 欠电生产倍率：rules Min/MaxLowPowerProductionSpeed + LowPowerPenaltyModifier
            // （注释：short * modifier = penalty；夹在 min..max）。
            double rate = 1.0;
            if (world_.Low_Power() && world_.Power_Drain() > 0) {
                const UnitModelDB& db = sprites_.Models();
                const double shortfall =
                    1.0 - static_cast<double>(world_.Power_Output()) /
                              static_cast<double>(world_.Power_Drain());
                const double penalty = shortfall * db.Low_Power_Penalty_Mod();
                rate = 1.0 - penalty;
                if (rate > db.Max_Low_Power_Prod()) {
                    rate = db.Max_Low_Power_Prod();
                }
                if (rate < db.Min_Low_Power_Prod()) {
                    rate = db.Min_Low_Power_Prod();
                }
            }
            build_job_.progress_fp += rate;
            build_job_.progress = static_cast<int>(build_job_.progress_fp);
            if (build_job_.progress >= build_job_.needed) {
                const std::string done = build_job_.type;
                const int tab = build_job_.tab;
                build_active_ = false;
                Finish_Build(done, tab);
            }
        }
        credits_ += world_.Take_Credit_Delta();
        world_.Set_Player_Credits(credits_);
        // 修理模式持续：每 Repair_Interval 帧对选中己方建筑步进（0x007120D0）。
        if (cursor_mode_ == 1 &&
            (logic_frame_ % sprites_.Models().Repair_Interval_Frames()) == 0) {
            for (Object& o : world_.Objects()) {
                if (!o.selected || !o.is_mine || o.hp <= 0 ||
                    o.hp >= o.hp_max || o.kind != MapObjectKind::Building) {
                    continue;
                }
                Try_Repair_Or_Sell(o.x, o.y);
            }
        }
        Refresh_Power();
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
    // Reset 命令分配器再提交 —— 若在 Begin_Frame 之后触发，会把这一帧已经
    // 录进分配器、还没 Execute 的 Clear/Draw 全部冲掉，离屏回读就全是 0。
    Warm_Sprites();
    sprites_.Reset_Budget(0);
    const float clear[4] = {0.02f, 0.02f, 0.03f, 1.0f};
    renderer_.Begin_Frame(clear);
    if (screen_ == GameScreen::Menu) {
        Draw_Title_Menu();
        Draw_Mouse();
    } else if (screen_ == GameScreen::Battle) {
        Draw_Battlefield();
        Draw_Objects();
        Draw_Top_Bar();
        // 绘制序对齐 RadarClass::Draw_It @0x00653100：
        //   先铺雷达内容，再画 TOP/RADAR 外框，最后 CREDITS（资金条永远在最上）。
        // 之前 Draw_Radar 在 Sidebar 之后，0x8C×0x6C 黑底从 Y_TOP+4 盖住资金条。
        Draw_Sidebar();
        Draw_Radar();
        Draw_Radar_Chrome();
        Draw_Selection();
        Draw_Mouse();
        if (paused_ && world_.Outcome() == MatchOutcome::Playing) {
            if (ask_abort_) {
                Draw_Ask_Abort();
            } else {
                Draw_Pause_Options();
            }
        }
        if (world_.Outcome() != MatchOutcome::Playing) {
            // 结局遮罩：Flag_To_Win/Lose 后停逻辑，战场仍可见。
            const float won[4] = {0.0f, 0.55f, 0.0f, 0.45f};
            const float lost[4] = {0.55f, 0.0f, 0.0f, 0.45f};
            const int vw = win_w_ - kSidebarW;
            renderer_.Draw_Rect(0, 0, vw, win_h_,
                                world_.Outcome() == MatchOutcome::Won ? won
                                                                      : lost);
            // 中央条：MessageList 文案（Flag_To_Win @0x4FCB4D → 0x6D4DB0）。
            // 字形：FULLFNT3.SHP（加载点 @0x5D2F08）；CJK 需 GAME.FNT 未接前用英文 CSF。
            const float bar[4] = {0.0f, 0.0f, 0.0f, 0.65f};
            const int bh = 48;
            const int by = win_h_ / 2 - bh / 2;
            renderer_.Draw_Rect(0, by, vw, bh, bar);
            if (!outcome_msg_.empty() && ui_fullfnt_.ok()) {
                const int tw = static_cast<int>(outcome_msg_.size()) * ui_fullfnt_.w;
                const int tx = (std::max)(0, (vw - tw) / 2);
                const int ty = by + (bh - ui_fullfnt_.h) / 2;
                Draw_Fullfnt_Text(tx, ty, outcome_msg_);
            }
        }
    }
    renderer_.End_Frame();
}

// ---------------------------------------------------------------------------
// 坐标换算
// ---------------------------------------------------------------------------

int GameShell::Cell_Level(int cx, int cy) const {
    const int iso_w = map_.Iso_Width();
    if (cx < 0 || cy < 0 || cx >= iso_w || cy >= map_.Height()) {
        return 0;
    }
    const std::vector<IsoCell>& cells = map_.Cells();
    const size_t idx = static_cast<size_t>(cy) * static_cast<size_t>(iso_w) +
                       static_cast<size_t>(cx);
    if (idx >= cells.size()) {
        return 0;
    }
    return cells[idx].level;
}

void GameShell::Cell_To_Screen(float cx, float cy, float* sx, float* sy) const {
    // cx=dx，cy=dy/2（可带小数）；完整 dy = 2*cy + (floor(cx)&1) 对整数格，
    // 小数移动时按 dx/dy 线性插值。
    const int ix = static_cast<int>(std::floor(cx));
    const int iy = static_cast<int>(std::floor(cy));
    const int lx = Cell_Level(ix, iy);
    int px = 0, py = 0;
    MapRenderer::Cell_To_Canvas(map_renderer_.Origin_X(), map_renderer_.Origin_Y(),
                                ix, iy, lx, &px, &py);
    const float fx = cx - std::floor(cx);
    const float fy = cy - std::floor(cy);
    // 显示格步进：Δdx=+1 → (+30,0)；Δrow=+1 → dy+2 → (0,+30)
    const float wx = static_cast<float>(px) + fx * static_cast<float>(kCellHalfW);
    const float wy = static_cast<float>(py) + fy * static_cast<float>(kCellH);
    camera_.World_To_Screen(wx + kCellHalfW, wy + kCellHalfH, sx, sy);
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
    sprites_.Reset_Budget(16);
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
    // 等距投影下屏幕 y ∝ dy = 2*row+(dx&1)，按 dy 升序与地形画家序一致。
    std::vector<int> order;
    const std::vector<Object>& objs = world_.Objects();
    order.reserve(objs.size());
    for (size_t i = 0; i < objs.size(); ++i) {
        order.push_back(static_cast<int>(i));
    }
    const std::vector<Object>& o = objs;
    std::sort(order.begin(), order.end(), [&o](int a, int b) {
        auto dy_of = [](const Object& obj) {
            const int dx = static_cast<int>(obj.x);
            const int row = static_cast<int>(obj.y);
            return row * 2 + (dx & 1);
        };
        const int da = dy_of(o[a]);
        const int db = dy_of(o[b]);
        if (da != db) {
            return da < db;
        }
        if (static_cast<int>(o[a].x) != static_cast<int>(o[b].x)) {
            return static_cast<int>(o[a].x) < static_cast<int>(o[b].x);
        }
        return o[a].id < o[b].id;
    });
    return order;
}

/// 画一个单位的真精灵。返回 false 表示没有可用素材（调用方跳过，不画色块）。
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
        // FogOfWar：未揭示格不画对象（Sight= 揭开，见 World::Tick_Shroud）。
        if (!world_.Cell_Revealed(static_cast<int>(o.x), static_cast<int>(o.y))) {
            continue;
        }
        float sx = 0.0f, sy = 0.0f;
        Cell_To_Screen(o.x, o.y, &sx, &sy);
        // 视口裁剪（留一格余量，免得贴边的突然消失）
        if (sx < -kCellW || sx > view_w + kCellW ||
            sy < kTopBarH - kCellH || sy > win_h_ + kCellH) {
            continue;
        }
        const float scale = camera_.Scale();

        // 没精灵这一帧就不画。色块会把路面盖成碎块，看起来像瓦片没接上。
        if (!Draw_Object_Sprite(o, static_cast<int>(sx),
                                static_cast<int>(sy), scale)) {
            continue;
        }
        if (o.selected) {
            // 选中反馈 = TechnoClass::DrawHealthBar（PIPS），不是绿框。
            Draw_Health_Pips(o, static_cast<int>(sx), static_cast<int>(sy), scale);
        }
        ++objects_drawn_;
    }
    // 弹道：优先 Projectile Image=.SHP；Arcing 抬屏幕 Y；缺图回退亮点。
    for (const Bullet& b : world_.Bullets()) {
        float sx = 0.0f, sy = 0.0f;
        Cell_To_Screen(b.x, b.y, &sx, &sy);
        sy -= b.arc_z * 8.0f * camera_.Scale();
        if (sx < -8.0f || sx > view_w + 8.0f || sy < kTopBarH - 8.0f ||
            sy > win_h_ + 8.0f) {
            continue;
        }
        const UiPiece* shp = nullptr;
        if (!b.image.empty()) {
            shp = Ensure_Projectile(b.image);
        }
        if (shp != nullptr && shp->ok()) {
            const float sc = camera_.Scale();
            const int id = shp->Frame(0);
            if (id >= 0) {
                renderer_.Draw_Sprite(
                    id,
                    static_cast<int>(sx - shp->w * sc * 0.5f),
                    static_cast<int>(sy - shp->h * sc * 0.5f), sc);
            }
        } else {
            const float c[4] = {1.0f, 0.95f, 0.2f, 1.0f};
            const int s = (std::max)(2, static_cast<int>(3.0f * camera_.Scale()));
            renderer_.Draw_Rect(static_cast<int>(sx) - s / 2,
                                static_cast<int>(sy) - s / 2, s, s, c);
        }
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
    // FogOfWar：0x6D8700 邻接掩码 + LUT@0x7F4194 → SHROUD/FOG 帧；
    // -1→帧0（不画），-2→帧15，其余为帧号（调用点 0x480213）。
    if (sprites_.Models().Fog_Of_War()) {
        static const int8_t kShroudLut[256] = {
            -1, 33, 2, 2, 34, 37, 2, 2, 4, 26, 6, 6, 4, 26, 6, 6,
            35, 45, 17, 17, 38, 41, 17, 17, 4, 26, 6, 6, 4, 26, 6, 6,
            8, 21, 10, 10, 27, 31, 10, 10, 12, 23, 14, 14, 12, 23, 14, 14,
            8, 21, 10, 10, 27, 31, 10, 10, 12, 23, 14, 14, 12, 23, 14, 14,
            32, 36, 25, 25, 44, 40, 25, 25, 19, 30, 20, 20, 19, 30, 20, 20,
            39, 43, 29, 29, 42, 46, 29, 29, 19, 30, 20, 20, 19, 30, 20, 20,
            8, 21, 10, 10, 27, 31, 10, 10, 12, 23, 14, 14, 12, 23, 14, 14,
            8, 21, 10, 10, 27, 31, 10, 10, 12, 23, 14, 14, 12, 23, 14, 14,
            1, 1, 3, 3, 16, 16, 3, 3, 5, 5, 7, 7, 5, 5, 7, 7,
            24, 24, 18, 18, 28, 28, 18, 18, 5, 5, 7, 7, 5, 5, 7, 7,
            9, 9, 11, 11, 22, 22, 11, 11, 13, 13, -2, -2, 13, 13, -2, -2,
            9, 9, 11, 11, 22, 22, 11, 11, 13, 13, -2, -2, 13, 13, -2, -2,
            1, 1, 3, 3, 16, 16, 3, 3, 5, 5, 7, 7, 5, 5, 7, 7,
            24, 24, 18, 18, 28, 28, 18, 18, 5, 5, 7, 7, 5, 5, 7, 7,
            9, 9, 11, 11, 22, 22, 11, 11, 13, 13, -2, -2, 13, 13, -2, -2,
            9, 9, 11, 11, 22, 22, 11, 11, 13, 13, -2, -2, 13, 13, -2, -2,
        };
        const UiPiece* sh = ui_shroud_.ok() ? &ui_shroud_
                                            : (ui_fog_.ok() ? &ui_fog_ : nullptr);
        const int view_w = win_w_ - kSidebarW;
        const float sc = camera_.Scale();
        const int iso_w = map_.Iso_Width();
        const int iso_h = map_.Height();
        auto shrouded = [&](int x, int y) -> bool {
            if (x < 0 || y < 0 || x >= iso_w || y >= iso_h) {
                return true;  // 界外当未揭示
            }
            return !world_.Cell_Revealed(x, y);
        };
        for (int cy = 0; cy < iso_h; ++cy) {
            for (int cx = 0; cx < iso_w; ++cx) {
                int frame = 0;
                if (shrouded(cx, cy)) {
                    frame = 15;  // 中心未揭示 → 满遮（对齐 -2→0xF 路径）
                } else {
                    unsigned mask = 0;
                    if (shrouded(cx - 1, cy - 1)) {
                        mask |= 0x40;
                    }
                    if (shrouded(cx, cy - 1)) {
                        mask |= 0x80;
                    }
                    if (shrouded(cx + 1, cy - 1)) {
                        mask |= 0x01;
                    }
                    if (shrouded(cx - 1, cy)) {
                        mask |= 0x20;
                    }
                    if (shrouded(cx + 1, cy)) {
                        mask |= 0x02;
                    }
                    if (shrouded(cx - 1, cy + 1)) {
                        mask |= 0x10;
                    }
                    if (shrouded(cx, cy + 1)) {
                        mask |= 0x08;
                    }
                    if (shrouded(cx + 1, cy + 1)) {
                        mask |= 0x04;
                    }
                    const int8_t v = kShroudLut[mask & 0xFF];
                    if (v == -1) {
                        continue;  // 帧 0：不画
                    }
                    frame = (v == -2) ? 15 : static_cast<int>(v);
                }
                float px = 0.0f, py = 0.0f;
                Cell_To_Screen(static_cast<float>(cx), static_cast<float>(cy),
                               &px, &py);
                if (px < -kCellW || px > view_w + kCellW || py < -kCellH ||
                    py > win_h_ + kCellH) {
                    continue;
                }
                if (sh != nullptr) {
                    const int id = sh->Frame(frame);
                    if (id >= 0) {
                        renderer_.Draw_Sprite(
                            id,
                            static_cast<int>(px - sh->w * sc * 0.5f),
                            static_cast<int>(py - sh->h * sc * 0.5f), sc);
                    }
                } else if (frame != 0) {
                    const float fog[4] = {0.02f, 0.02f, 0.03f, 0.85f};
                    renderer_.Draw_Rect(static_cast<int>(px - 15 * sc),
                                        static_cast<int>(py - 8 * sc),
                                        static_cast<int>(30 * sc),
                                        static_cast<int>(16 * sc), fog);
                }
            }
        }
    }
    if (grid_debug_) {
        Draw_Grid_Debug();
    }
}

bool GameShell::Dump_Tile_RGBA(int tile, int sub, const char* path) {
    return map_renderer_.Dump_Tile_RGBA(tile, sub, path);
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
        for (int cxi = 0; cxi < map_.Iso_Width(); cxi += 2) {
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
//
// 阵营外观：gamemd 0x00534FA0 按 Side 挂载 SIDENC%02d.MIX（+ SIDEC / SIDEC*MD）。
// SIDE1/RADAR/TOP/CREDITS 都在 SIDENC 里；不挂则 Read_Deep 易命中 LOCAL 盟军套。
// ---------------------------------------------------------------------------

bool GameShell::Mount_Player_Sidenc() {
    // 0x00534FA0：ecx=Side；Side==2（Yuri）先改成 1；文件序号 = Side+1。
    int side = Player_Side();
    if (side == 2) {
        side = 1;
    }
    if (side < 0) {
        side = 0;
    }
    const int idx = side + 1;

    // 只在当前 roots_ 快照里找（避免挂载过程把自己搜进去）。
    const std::vector<MixFileClass*> search = roots_;
    auto try_one = [&](const char* fmt) -> MixFileClass* {
        char name[40];
        std::snprintf(name, sizeof(name), fmt, idx);
        const uint32_t id = MixFileClass::CRC_Of(name);
        for (MixFileClass* root : search) {
            const MixEntry* e = root->Find_By_ID(id);
            if (e == nullptr) {
                continue;
            }
            auto sub = root->Open_Sub(*e);
            if (!sub) {
                continue;
            }
            std::printf("  阵营界面 %s（%d 条目）\n", name, sub->Count());
            mixes_.push_back(std::move(sub));
            return mixes_.back().get();
        }
        for (MixFileClass* root : search) {
            const MixFileClass::Deep_Entry* de = root->Find_Deep(id);
            if (de == nullptr || de->owner == nullptr || de->entry == nullptr) {
                continue;
            }
            auto sub = std::make_unique<MixFileClass>();
            if (!sub->Open_Nested(*de->owner, *de->entry)) {
                continue;
            }
            std::printf("  阵营界面 %s（深层，%d 条目）\n", name, sub->Count());
            mixes_.push_back(std::move(sub));
            return mixes_.back().get();
        }
        return nullptr;
    };

    // 搜索优先序：CREDITS/TOP/RADAR/SIDE* 在 SIDEC%02d.MIX（实测），
    // SIDENC 是旁路素材。roots_ 从前到后 Read_Deep，SIDEC 必须最先。
    int ok = 0;
    if (MixFileClass* m = try_one("SIDEC%02d.MIX")) {
        roots_.insert(roots_.begin(), m);
        ++ok;
    }
    if (MixFileClass* m = try_one("SIDEC%02dMD.MIX")) {
        roots_.insert(roots_.begin(), m);
        ++ok;
    }
    if (MixFileClass* m = try_one("SIDENC%02d.MIX")) {
        roots_.insert(roots_.begin() + ok, m);
        ++ok;
    }
    return ok > 0;
}

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
        // CC_Draw_Shape 画的是整张 SHP 画布，帧头的 (x,y,w,h) 是画布里的子矩形。
        // 只上传子矩形再往 (sx,sy) 贴，等于把偏移丢了。
        const int cw = shp.Width();
        const int ch = shp.Height();
        dst->w = cw;
        dst->h = ch;
        dst->frames = shp.Frame_Count();
        dst->sprite.clear();
        for (int f = 0; f < dst->frames; ++f) {
            const std::vector<uint8_t>& px = shp.Frame_Pixels(f);
            const ShpFrameInfo& fi = shp.Frame_Info(f);
            if (cw <= 0 || ch <= 0 || fi.w <= 0 || fi.h <= 0 ||
                px.size() < static_cast<size_t>(fi.w) * fi.h) {
                dst->sprite.push_back(-1);
                continue;
            }
            std::vector<uint8_t> rgba(static_cast<size_t>(cw) * ch * 4, 0);
            for (int y = 0; y < fi.h; ++y) {
                for (int x = 0; x < fi.w; ++x) {
                    const uint8_t idx = px[static_cast<size_t>(y) * fi.w + x];
                    if (idx == 0) {
                        continue;
                    }
                    const int dx = static_cast<int>(fi.x) + x;
                    const int dy = static_cast<int>(fi.y) + y;
                    if (dx < 0 || dy < 0 || dx >= cw || dy >= ch) {
                        continue;
                    }
                    const size_t p = (static_cast<size_t>(dy) * cw + dx) * 4;
                    const size_t c = static_cast<size_t>(idx) * 3;
                    rgba[p + 0] = pal768[c + 0];
                    rgba[p + 1] = pal768[c + 1];
                    rgba[p + 2] = pal768[c + 2];
                    rgba[p + 3] = 255;
                }
            }
            dst->sprite.push_back(renderer_.Upload_Sprite_RGBA(rgba.data(), cw, ch));
        }
        return true;
    };

    int ok = 0;
    ok += load("SIDE1.SHP", &ui_side1_) ? 1 : 0;
    ok += load("SIDE2.SHP", &ui_side2_) ? 1 : 0;
    ok += load("SIDE2B.SHP", &ui_side2b_) ? 1 : 0;
    ok += load("SIDE3.SHP", &ui_side3_) ? 1 : 0;
    // RADAR.SHP（盟军/苏联共用）；Yuri 另有 RADARY.SHP + RADARYURI.PAL（ra2md）。
    ok += load("RADAR.SHP", &ui_radar_) ? 1 : 0;
    ok += load("TOP.SHP", &ui_top_) ? 1 : 0;
    ok += load("CREDITS.SHP", &ui_credits_) ? 1 : 0;
    ok += load("POWERP.SHP", &ui_powerp_) ? 1 : 0;
    ok += load("Power.SHP", &ui_power_) ? 1 : 0;
    ok += load("R-UP.SHP", &ui_rup_) ? 1 : 0;
    ok += load("R-DN.SHP", &ui_rdn_) ? 1 : 0;
    ok += load("SIDEBTTN.SHP", &ui_sidebttn_) ? 1 : 0;
    ok += load("SHROUD.SHP", &ui_shroud_) ? 1 : 0;
    ok += load("FOG.SHP", &ui_fog_) ? 1 : 0;  // 与 SHROUD 二选一 @0x47F01F
    // FULLFNT3：侧栏/消息拉丁字（gamemd @0x5D2F08 选 FULLFNT3 vs SIDEFNT3）。
    {
        uint8_t fpal[768];
        std::vector<uint8_t> fp6;
        for (MixFileClass* m : roots_) {
            fp6 = m->Read_Deep("FULLFNT3.PAL");
            if (fp6.size() >= 768) {
                break;
            }
            fp6.clear();
        }
        if (fp6.size() >= 768) {
            SpriteCache::Expand_Pal768(fp6.data(), fpal);
            auto load_fnt = [&](const char* name, UiPiece* dst) -> bool {
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
                const int cw = shp.Width();
                const int ch = shp.Height();
                dst->w = cw;
                dst->h = ch;
                dst->frames = shp.Frame_Count();
                dst->sprite.clear();
                for (int f = 0; f < dst->frames; ++f) {
                    const std::vector<uint8_t>& px = shp.Frame_Pixels(f);
                    const ShpFrameInfo& fi = shp.Frame_Info(f);
                    if (cw <= 0 || ch <= 0 || fi.w <= 0 || fi.h <= 0 ||
                        px.size() < static_cast<size_t>(fi.w) * fi.h) {
                        dst->sprite.push_back(-1);
                        continue;
                    }
                    std::vector<uint8_t> rgba(static_cast<size_t>(cw) * ch * 4, 0);
                    for (int y = 0; y < fi.h; ++y) {
                        for (int x = 0; x < fi.w; ++x) {
                            const uint8_t idx =
                                px[static_cast<size_t>(y) * fi.w + x];
                            if (idx == 0) {
                                continue;
                            }
                            const int dx = static_cast<int>(fi.x) + x;
                            const int dy = static_cast<int>(fi.y) + y;
                            if (dx < 0 || dy < 0 || dx >= cw || dy >= ch) {
                                continue;
                            }
                            const size_t p =
                                (static_cast<size_t>(dy) * cw + dx) * 4;
                            const size_t c = static_cast<size_t>(idx) * 3;
                            rgba[p + 0] = fpal[c + 0];
                            rgba[p + 1] = fpal[c + 1];
                            rgba[p + 2] = fpal[c + 2];
                            rgba[p + 3] = 255;
                        }
                    }
                    dst->sprite.push_back(
                        renderer_.Upload_Sprite_RGBA(rgba.data(), cw, ch));
                }
                return true;
            };
            ok += load_fnt("FULLFNT3.SHP", &ui_fullfnt_) ? 1 : 0;
        }
    }
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

    // 资金数字是 PCX，不是 SHP。PcxFile 已核实：8 位 PCX 内嵌调色板是标准 8 位，
    // 不能拿 SIDEBAR.PAL 的 6→8 展开去套（会整串发黑）。索引 0 仍当透明。
    // 加载入口：gamemd 0x00783A90 按 digit 查 number0.pcx..number9.pcx。
    for (int i = 0; i < 10; ++i) {
        char n[24];
        std::snprintf(n, sizeof(n), "number%d.pcx", i);
        std::vector<uint8_t> data;
        for (MixFileClass* m : roots_) {
            data = m->Read_Deep(n);
            if (!data.empty()) {
                break;
            }
        }
        if (data.empty()) {
            continue;
        }
        PcxFile pcx;
        if (!pcx.Load(data.data(), data.size()) || pcx.Width() <= 0 ||
            pcx.Height() <= 0 || !pcx.Is_Indexed()) {
            continue;
        }
        const std::vector<uint8_t>& px = pcx.Indices();
        if (px.size() < static_cast<size_t>(pcx.Width()) * pcx.Height()) {
            continue;
        }
        std::vector<uint8_t> rgba = pcx.To_RGBA();
        for (size_t k = 0; k < px.size(); ++k) {
            if (px[k] == 0) {
                rgba[k * 4 + 3] = 0;
            }
        }
        ui_digit_[i].w = pcx.Width();
        ui_digit_[i].h = pcx.Height();
        ui_digit_[i].frames = 1;
        ui_digit_[i].sprite.assign(
            1, renderer_.Upload_Sprite_RGBA(rgba.data(), pcx.Width(), pcx.Height()));
        ++ok;
    }

    // MOUSE.SHA + MOUSEPAL.PAL：MouseClass 加载 @0x005BDF30 / Init Mouse。
    {
        uint8_t mpal[768];
        std::vector<uint8_t> raw_pal;
        for (MixFileClass* m : roots_) {
            raw_pal = m->Read_Deep("MOUSEPAL.PAL");
            if (raw_pal.size() >= 768) {
                break;
            }
            raw_pal.clear();
        }
        if (raw_pal.size() >= 768) {
            SpriteCache::Expand_Pal768(raw_pal.data(), mpal);
        } else {
            std::memcpy(mpal, pal768, 768);
        }
        auto load_pal = [&](const char* name, UiPiece* dst, const uint8_t* pal) {
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
            const int cw = shp.Width();
            const int ch = shp.Height();
            dst->w = cw;
            dst->h = ch;
            dst->frames = shp.Frame_Count();
            dst->sprite.clear();
            for (int f = 0; f < dst->frames; ++f) {
                const std::vector<uint8_t>& px = shp.Frame_Pixels(f);
                const ShpFrameInfo& fi = shp.Frame_Info(f);
                if (cw <= 0 || ch <= 0 || fi.w <= 0 || fi.h <= 0 ||
                    px.size() < static_cast<size_t>(fi.w) * fi.h) {
                    dst->sprite.push_back(-1);
                    continue;
                }
                std::vector<uint8_t> rgba(static_cast<size_t>(cw) * ch * 4, 0);
                for (int y = 0; y < fi.h; ++y) {
                    for (int x = 0; x < fi.w; ++x) {
                        const uint8_t idx = px[static_cast<size_t>(y) * fi.w + x];
                        if (idx == 0) {
                            continue;
                        }
                        const int dx = static_cast<int>(fi.x) + x;
                        const int dy = static_cast<int>(fi.y) + y;
                        if (dx < 0 || dy < 0 || dx >= cw || dy >= ch) {
                            continue;
                        }
                        const size_t p = (static_cast<size_t>(dy) * cw + dx) * 4;
                        const size_t c = static_cast<size_t>(idx) * 3;
                        rgba[p + 0] = pal[c + 0];
                        rgba[p + 1] = pal[c + 1];
                        rgba[p + 2] = pal[c + 2];
                        rgba[p + 3] = 255;
                    }
                }
                dst->sprite.push_back(
                    renderer_.Upload_Sprite_RGBA(rgba.data(), cw, ch));
            }
            return true;
        };
        if (load_pal("MOUSE.SHA", &ui_mouse_, mpal)) {
            ++ok;
            std::printf("  MOUSE.SHA %d 帧 %dx%d（表 0x82D028）\n", ui_mouse_.frames,
                        ui_mouse_.w, ui_mouse_.h);
        }
        // PIPS.SHP：血条 @0x006F64A0；空格帧 0，绿/黄/红 = 1/2/4。
        if (load_pal("PIPS.SHP", &ui_pips_, pal768)) {
            ++ok;
            std::printf("  PIPS.SHP %d 帧\n", ui_pips_.frames);
        }
    }

    // Title.PCX：主菜单 Background（GraphicMenu 键；ctor 推 0x8241C8）。
    {
        std::vector<uint8_t> raw;
        for (MixFileClass* m : roots_) {
            raw = m->Read_Deep("TITLE.PCX");
            if (raw.empty()) {
                raw = m->Read_Deep("Title.PCX");
            }
            if (!raw.empty()) {
                break;
            }
        }
        if (!raw.empty()) {
            PcxFile pcx;
            if (pcx.Load(raw.data(), raw.size()) && pcx.Width() > 0 &&
                pcx.Height() > 0) {
                std::vector<uint8_t> rgba = pcx.To_RGBA();
                ui_title_.w = pcx.Width();
                ui_title_.h = pcx.Height();
                ui_title_.frames = 1;
                ui_title_.sprite.assign(
                    1, renderer_.Upload_Sprite_RGBA(rgba.data(), pcx.Width(),
                                                    pcx.Height()));
                ++ok;
                std::printf("  Title.PCX %dx%d\n", ui_title_.w, ui_title_.h);
            }
        }
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

void GameShell::Draw_Fullfnt_Text(int x, int y, const std::string& utf8) {
    // FULLFNT3：帧下标 = 拉丁码点（实测 648 帧覆盖 ASCII 区）。非拉丁跳过。
    if (!ui_fullfnt_.ok()) {
        return;
    }
    int cx = x;
    for (unsigned char ch : utf8) {
        if (ch < 32 || ch >= 127) {
            // UTF-8 多字节：跳过后续续字节，避免错帧。
            if (ch >= 0xC0) {
                continue;
            }
            cx += ui_fullfnt_.w / 2;
            continue;
        }
        Draw_Ui(ui_fullfnt_, cx, y, static_cast<int>(ch));
        cx += ui_fullfnt_.w;
    }
}

bool GameShell::Enter_Title_Menu(const std::vector<std::string>& mix_paths,
                                 const char* skirmish_map, std::string* err) {
    pending_mixes_ = mix_paths;
    skirmish_map_ = skirmish_map ? skirmish_map : "";
    if (roots_.empty()) {
        for (const std::string& p : mix_paths) {
            auto m = std::make_unique<MixFileClass>();
            if (!m->Open(p.c_str())) {
                if (err) {
                    *err = "打不开 " + p;
                }
                return false;
            }
            roots_.push_back(m.get());
            mixes_.push_back(std::move(m));
        }
        {
            std::vector<uint8_t> csf;
            for (MixFileClass* m : roots_) {
                csf = m->Read_Deep("ra2md.csf");
                if (csf.empty()) {
                    csf = m->Read_Deep("ra2.csf");
                }
                if (!csf.empty()) {
                    break;
                }
            }
            if (!csf.empty()) {
                csf_.Load(csf.data(), csf.size());
            }
            for (MixFileClass* m : roots_) {
                std::vector<uint8_t> en = m->Read_Deep("ra2.csf");
                if (!en.empty()) {
                    csf_en_.Load(en.data(), en.size());
                    break;
                }
            }
        }
        Load_UI();
    }
    screen_ = GameScreen::Menu;
    title_page_ = 0;
    std::printf("  主菜单（RT_DIALOG 226）；Skirmish→%s\n",
                skirmish_map_.empty() ? "(无默认图)" : skirmish_map_.c_str());
    return true;
}

void GameShell::Draw_Title_Menu() {
    // RT_DIALOG 226 / 256：533×369；Background=Title.PCX。
    if (ui_title_.ok()) {
        const int id = ui_title_.Frame(0);
        if (id >= 0) {
            const float sx = static_cast<float>(win_w_) /
                             static_cast<float>((std::max)(1, ui_title_.w));
            const float sy = static_cast<float>(win_h_) /
                             static_cast<float>((std::max)(1, ui_title_.h));
            renderer_.Draw_Sprite(id, 0, 0, (std::max)(sx, sy));
        }
    } else {
        const float bg[4] = {0.02f, 0.02f, 0.05f, 1.0f};
        renderer_.Draw_Rect(0, 0, win_w_, win_h_, bg);
    }
    constexpr int kDlgW = 533;
    constexpr int kDlgH = 369;
    const float scale =
        (std::min)(static_cast<float>(win_w_ - 40) / kDlgW,
                   static_cast<float>(win_h_ - 40) / kDlgH);
    const int pw = static_cast<int>(kDlgW * scale);
    const int ph = static_cast<int>(kDlgH * scale);
    const int px = (win_w_ - pw) / 2;
    const int py = (win_h_ - ph) / 2;
    struct MenuBtn {
        int id;
        int dx, dy, dw, dh;
        const char* key;
        bool button;
    };
    static const MenuBtn kMain[] = {
        {0x694, 425, 1, 108, 16, "GUI:MainMenu", false},
        {0x683, 425, 125, 108, 23, "GUI:SinglePlayer", true},
        {0x684, 425, 152, 108, 23, "GUI:WWOnline", true},
        {0x578, 425, 179, 108, 23, "GUI:Network", true},
        {0x686, 425, 206, 108, 23, "GUI:MoviesAndCredits", true},
        {0x55C, 425, 233, 108, 23, "GUI:Options", true},
        {0x3EE, 425, 330, 108, 23, "GUI:ExitGame", true},
    };
    static const MenuBtn kSingle[] = {
        {0x694, 425, 1, 108, 16, "GUI:SinglePlayerMenu", false},
        {0x688, 425, 122, 108, 23, "GUI:NewCampaign", true},
        {0x689, 425, 149, 108, 23, "GUI:LoadSavedGame", true},
        {0x579, 425, 176, 108, 23, "GUI:Skirmish", true},
        {0x686, 425, 346, 108, 23, "GUI:MainMenu", true},
    };
    const MenuBtn* btns = title_page_ == 1 ? kSingle : kMain;
    const int nbtn = title_page_ == 1 ? 5 : 7;
    const float btn_bg[4] = {0.15f, 0.15f, 0.2f, 0.85f};
    const float btn_hi[4] = {0.4f, 0.4f, 0.5f, 1.0f};
    for (int i = 0; i < nbtn; ++i) {
        const MenuBtn& b = btns[i];
        const int bx = px + static_cast<int>(b.dx * scale);
        const int by = py + static_cast<int>(b.dy * scale);
        const int bw = static_cast<int>(b.dw * scale);
        const int bh = static_cast<int>(b.dh * scale);
        if (b.button) {
            renderer_.Draw_Rect(bx, by, bw, bh, btn_bg);
            renderer_.Draw_Rect_Outline(bx, by, bw, bh, btn_hi, 1);
        }
        if (!ui_fullfnt_.ok()) {
            continue;
        }
        std::string line = csf_en_.Get(b.key);
        if (line.empty()) {
            line = csf_.Get(b.key);
        }
        if (line.empty()) {
            line = b.key;
        }
        Draw_Fullfnt_Text(bx + 4, by + (std::max)(0, (bh - ui_fullfnt_.h) / 2),
                          line);
    }
}

bool GameShell::Hit_Title_Menu(int x, int y) {
    constexpr int kDlgW = 533;
    constexpr int kDlgH = 369;
    const float scale =
        (std::min)(static_cast<float>(win_w_ - 40) / kDlgW,
                   static_cast<float>(win_h_ - 40) / kDlgH);
    const int pw = static_cast<int>(kDlgW * scale);
    const int ph = static_cast<int>(kDlgH * scale);
    const int px = (win_w_ - pw) / 2;
    const int py = (win_h_ - ph) / 2;
    struct MenuBtn {
        int id;
        int dx, dy, dw, dh;
        const char* key;
    };
    static const MenuBtn kMain[] = {
        {0x683, 425, 125, 108, 23, "GUI:SinglePlayer"},
        {0x684, 425, 152, 108, 23, "GUI:WWOnline"},
        {0x578, 425, 179, 108, 23, "GUI:Network"},
        {0x686, 425, 206, 108, 23, "GUI:MoviesAndCredits"},
        {0x55C, 425, 233, 108, 23, "GUI:Options"},
        {0x3EE, 425, 330, 108, 23, "GUI:ExitGame"},
    };
    static const MenuBtn kSingle[] = {
        {0x688, 425, 122, 108, 23, "GUI:NewCampaign"},
        {0x689, 425, 149, 108, 23, "GUI:LoadSavedGame"},
        {0x579, 425, 176, 108, 23, "GUI:Skirmish"},
        {0x686, 425, 346, 108, 23, "GUI:MainMenu"},
    };
    const MenuBtn* btns = title_page_ == 1 ? kSingle : kMain;
    const int nbtn = title_page_ == 1 ? 4 : 6;
    for (int i = 0; i < nbtn; ++i) {
        const MenuBtn& b = btns[i];
        const int bx = px + static_cast<int>(b.dx * scale);
        const int by = py + static_cast<int>(b.dy * scale);
        const int bw = static_cast<int>(b.dw * scale);
        const int bh = static_cast<int>(b.dh * scale);
        if (x < bx || x >= bx + bw || y < by || y >= by + bh) {
            continue;
        }
        if (title_page_ == 0) {
            if (b.id == 0x3EE) {
                exit_requested_ = true;
                std::printf("  Title ExitGame\n");
                return true;
            }
            if (b.id == 0x683) {
                title_page_ = 1;
                std::printf("  Title → SinglePlayerMenu\n");
                return true;
            }
            std::printf("  Title stub id=0x%X %s\n", b.id, b.key);
            return true;
        }
        if (b.id == 0x686) {
            title_page_ = 0;
            std::printf("  SinglePlayer → MainMenu\n");
            return true;
        }
        if (b.id == 0x688) {
            // NewCampaign：战役选图 UI（BATTLEMD @0x52CB90）未全接前，
            // 直接载入 MAPS01.MIX 中 Brief:ALL01（盟军首关，MIX 实证）。
            std::string game_dir;
            if (!pending_mixes_.empty()) {
                game_dir = Dir_Of_Path(pending_mixes_.front());
            }
            std::string camp_path;
            std::string e;
            if (!Extract_Campaign_All01(game_dir, &camp_path, &e)) {
                std::printf("  NewCampaign stub: %s\n", e.c_str());
                return true;
            }
            std::vector<std::string> mixes = pending_mixes_;
            mixes_.clear();
            roots_.clear();
            title_page_ = 0;
            if (!Load_Map(mixes, camp_path.c_str(), &e)) {
                std::printf("  NewCampaign 载入失败: %s\n", e.c_str());
                Enter_Title_Menu(mixes, skirmish_map_.c_str(), nullptr);
                return true;
            }
            std::printf("  NewCampaign → ALL01 (%s)\n", camp_path.c_str());
            return true;
        }
        if (b.id == 0x579) {
            if (skirmish_map_.empty()) {
                std::printf("  Skirmish stub（无默认图）\n");
                return true;
            }
            std::string e;
            std::vector<std::string> mixes = pending_mixes_;
            mixes_.clear();
            roots_.clear();
            title_page_ = 0;
            if (!Load_Map(mixes, skirmish_map_.c_str(), &e)) {
                std::printf("  Skirmish 载入失败: %s\n", e.c_str());
                Enter_Title_Menu(mixes, skirmish_map_.c_str(), nullptr);
                return true;
            }
            std::printf("  Skirmish → %s\n", skirmish_map_.c_str());
            return true;
        }
        if (b.id == 0x688) {
            // NewCampaign：MAPS01 内 Brief:ALL01（战役第一关；BATTLEMD 选关 UI 未接）。
            std::string map_path, e;
            if (!Extract_Campaign_Map("Brief:ALL01", &map_path, &e)) {
                std::printf("  NewCampaign 失败: %s\n", e.c_str());
                return true;
            }
            std::vector<std::string> mixes = pending_mixes_;
            mixes_.clear();
            roots_.clear();
            title_page_ = 0;
            if (!Load_Map(mixes, map_path.c_str(), &e)) {
                std::printf("  NewCampaign 载入失败: %s\n", e.c_str());
                Enter_Title_Menu(mixes, skirmish_map_.c_str(), nullptr);
                return true;
            }
            std::printf("  NewCampaign → %s\n", map_path.c_str());
            return true;
        }
        std::printf("  SinglePlayer stub id=0x%X %s\n", b.id, b.key);
        return true;
    }
    return false;
}

void GameShell::Draw_Pause_Options() {
    // DLGTEMPLATE @0x489860：533×369，cdit=8（dialog base units）。
    // 按钮（右侧列，解析自 .rsrc）：
    //   0x686 Resume (425,346) 108×23
    //   0x521 GameControls (425,122)
    //   0x51e LoadGame (425,149)
    //   0x522 AbortMission (425,230)
    //   0x51f SaveGame (425,176)
    //   0x520 DeleteGame (425,203)
    //   0x694 GameOptions 标题 STATIC (425,1)
    const int vw = win_w_ - kSidebarW;
    const float dim[4] = {0.0f, 0.0f, 0.0f, 0.55f};
    renderer_.Draw_Rect(0, 0, vw, win_h_, dim);
    constexpr int kDlgW = 533;
    constexpr int kDlgH = 369;
    const float scale =
        (std::min)(static_cast<float>(vw - 40) / kDlgW,
                   static_cast<float>(win_h_ - 40) / kDlgH);
    const int pw = static_cast<int>(kDlgW * scale);
    const int ph = static_cast<int>(kDlgH * scale);
    const int px = (vw - pw) / 2;
    const int py = (win_h_ - ph) / 2;
    const float panel[4] = {0.08f, 0.08f, 0.1f, 0.92f};
    renderer_.Draw_Rect(px, py, pw, ph, panel);
    struct OptBtn {
        int id;
        int dx, dy, dw, dh;
        const char* key;
        bool button;
    };
    static const OptBtn kBtns[] = {
        {0x694, 425, 1, 108, 16, "GUI:GameOptions", false},
        {0x521, 425, 122, 108, 23, "GUI:GameControls", true},
        {0x51e, 425, 149, 108, 23, "GUI:LoadGame", true},
        {0x51f, 425, 176, 108, 23, "GUI:SaveGame", true},
        {0x520, 425, 203, 108, 23, "GUI:DeleteGame", true},
        {0x522, 425, 230, 108, 23, "GUI:AbortMission", true},
        {0x686, 425, 346, 108, 23, "GUI:ResumeMission", true},
    };
    const float btn_bg[4] = {0.2f, 0.2f, 0.25f, 1.0f};
    const float btn_hi[4] = {0.35f, 0.35f, 0.4f, 1.0f};
    for (const OptBtn& b : kBtns) {
        const int bx = px + static_cast<int>(b.dx * scale);
        const int by = py + static_cast<int>(b.dy * scale);
        const int bw = static_cast<int>(b.dw * scale);
        const int bh = static_cast<int>(b.dh * scale);
        if (b.button) {
            renderer_.Draw_Rect(bx, by, bw, bh, btn_bg);
            renderer_.Draw_Rect_Outline(bx, by, bw, bh, btn_hi, 1);
        }
        if (!ui_fullfnt_.ok()) {
            continue;
        }
        std::string line = csf_en_.Get(b.key);
        if (line.empty()) {
            line = csf_.Get(b.key);
        }
        if (line.empty()) {
            line = b.key;
        }
        // 钮内左对齐留 4px；过长则仍画（可能溢出，对齐原版截断前观感）。
        Draw_Fullfnt_Text(bx + 4, by + (std::max)(0, (bh - ui_fullfnt_.h) / 2),
                          line);
    }
}

bool GameShell::Hit_Pause_Options(int x, int y) {
    const int vw = win_w_ - kSidebarW;
    constexpr int kDlgW = 533;
    constexpr int kDlgH = 369;
    const float scale =
        (std::min)(static_cast<float>(vw - 40) / kDlgW,
                   static_cast<float>(win_h_ - 40) / kDlgH);
    const int pw = static_cast<int>(kDlgW * scale);
    const int ph = static_cast<int>(kDlgH * scale);
    const int px = (vw - pw) / 2;
    const int py = (win_h_ - ph) / 2;
    struct OptBtn {
        int id;
        int dx, dy, dw, dh;
        const char* key;
    };
    static const OptBtn kBtns[] = {
        {0x521, 425, 122, 108, 23, "GUI:GameControls"},
        {0x51e, 425, 149, 108, 23, "GUI:LoadGame"},
        {0x51f, 425, 176, 108, 23, "GUI:SaveGame"},
        {0x520, 425, 203, 108, 23, "GUI:DeleteGame"},
        {0x522, 425, 230, 108, 23, "GUI:AbortMission"},
        {0x686, 425, 346, 108, 23, "GUI:ResumeMission"},
    };
    for (const OptBtn& b : kBtns) {
        const int bx = px + static_cast<int>(b.dx * scale);
        const int by = py + static_cast<int>(b.dy * scale);
        const int bw = static_cast<int>(b.dw * scale);
        const int bh = static_cast<int>(b.dh * scale);
        if (x < bx || x >= bx + bw || y < by || y >= by + bh) {
            continue;
        }
        if (b.id == 0x686) {
            paused_ = false;
            ask_abort_ = false;
            return true;
        }
        if (b.id == 0x522) {
            // Abort → 对话框 182 AskAbortMission（不直接 Lose）。
            ask_abort_ = true;
            return true;
        }
        // Load/Save/Controls/Delete：Controls（0x521）留 UI；Save/Load/Delete 走真实 .sav。
        if (b.id == 0x51e) {
            // LoadGame：找最近的 SAVE0001.SAV 之类（暂时固定目录）
            std::string sav_path = "build/save0001.sav";
            std::string mp;
            if (Load_World(&world_, &mp, sav_path)) {
                std::printf("  GOptions Load 命中: %s\n", sav_path.c_str());
                return true;
            }
            std::printf("  GOptions Load 失败: %s\n", sav_path.c_str());
            return true;
        }
        if (b.id == 0x51f) {
            // SaveGame：把当前 World 写到 build/save0001.sav
            std::string sav_path = "build/save0001.sav";
            if (Save_World(world_, current_map_path_, sav_path)) {
                std::printf("  GOptions Save 成功: %s\n", sav_path.c_str());
            } else {
                std::printf("  GOptions Save 失败: %s\n", sav_path.c_str());
            }
            return true;
        }
        if (b.id == 0x520) {
            // DeleteGame：留 UI，最小实现 = 文件存在就删
            std::string sav_path = "build/save0001.sav";
            if (std::remove(sav_path.c_str()) == 0) {
                std::printf("  GOptions Delete 删除: %s\n", sav_path.c_str());
            } else {
                std::printf("  GOptions Delete 无文件: %s\n", sav_path.c_str());
            }
            return true;
        }
        std::printf("  GOptions stub id=0x%X %s\n", b.id, b.key);
        return true;
    }
    return false;
}

void GameShell::Draw_Ask_Abort() {
    // RT_DIALOG 182：AskAbortMission；0x6C9=GUI:Leave / 0x712=GUI:Restart / 0x686=Resume。
    const int vw = win_w_ - kSidebarW;
    const float dim[4] = {0.0f, 0.0f, 0.0f, 0.55f};
    renderer_.Draw_Rect(0, 0, vw, win_h_, dim);
    constexpr int kDlgW = 533;
    constexpr int kDlgH = 369;
    const float scale =
        (std::min)(static_cast<float>(vw - 40) / kDlgW,
                   static_cast<float>(win_h_ - 40) / kDlgH);
    const int pw = static_cast<int>(kDlgW * scale);
    const int ph = static_cast<int>(kDlgH * scale);
    const int px = (vw - pw) / 2;
    const int py = (win_h_ - ph) / 2;
    const float panel[4] = {0.08f, 0.08f, 0.1f, 0.92f};
    renderer_.Draw_Rect(px, py, pw, ph, panel);
    struct Btn {
        int id;
        int dx, dy, dw, dh;
        const char* key;
        bool button;
    };
    static const Btn kBtns[] = {
        {0xFFFF, 99, 165, 230, 19, "GUI:AskAbortMission", false},
        {0x6C9, 431, 122, 108, 23, "GUI:Leave", true},
        {0x712, 431, 149, 108, 23, "GUI:Restart", true},
        {0x686, 425, 346, 108, 23, "GUI:ResumeMission", true},
    };
    const float btn_bg[4] = {0.2f, 0.2f, 0.25f, 1.0f};
    const float btn_hi[4] = {0.35f, 0.35f, 0.4f, 1.0f};
    for (const Btn& b : kBtns) {
        const int bx = px + static_cast<int>(b.dx * scale);
        const int by = py + static_cast<int>(b.dy * scale);
        const int bw = static_cast<int>(b.dw * scale);
        const int bh = static_cast<int>(b.dh * scale);
        if (b.button) {
            renderer_.Draw_Rect(bx, by, bw, bh, btn_bg);
            renderer_.Draw_Rect_Outline(bx, by, bw, bh, btn_hi, 1);
        }
        if (!ui_fullfnt_.ok()) {
            continue;
        }
        std::string line = csf_en_.Get(b.key);
        if (line.empty()) {
            line = csf_.Get(b.key);
        }
        if (line.empty()) {
            line = b.key;
        }
        Draw_Fullfnt_Text(bx + 4, by + (std::max)(0, (bh - ui_fullfnt_.h) / 2),
                          line);
    }
}

bool GameShell::Hit_Ask_Abort(int x, int y) {
    const int vw = win_w_ - kSidebarW;
    constexpr int kDlgW = 533;
    constexpr int kDlgH = 369;
    const float scale =
        (std::min)(static_cast<float>(vw - 40) / kDlgW,
                   static_cast<float>(win_h_ - 40) / kDlgH);
    const int pw = static_cast<int>(kDlgW * scale);
    const int ph = static_cast<int>(kDlgH * scale);
    const int px = (vw - pw) / 2;
    const int py = (win_h_ - ph) / 2;
    struct Btn {
        int id;
        int dx, dy, dw, dh;
    };
    static const Btn kBtns[] = {
        {0x6C9, 431, 122, 108, 23},
        {0x712, 431, 149, 108, 23},
        {0x686, 425, 346, 108, 23},
    };
    for (const Btn& b : kBtns) {
        const int bx = px + static_cast<int>(b.dx * scale);
        const int by = py + static_cast<int>(b.dy * scale);
        const int bw = static_cast<int>(b.dw * scale);
        const int bh = static_cast<int>(b.dh * scale);
        if (x < bx || x >= bx + bw || y < by || y >= by + bh) {
            continue;
        }
        if (b.id == 0x686) {
            // Resume：关确认，留在 GOptions。
            ask_abort_ = false;
            return true;
        }
        if (b.id == 0x6C9) {
            // Leave @0x4F192C 结果码 2 → 放弃任务。
            ask_abort_ = false;
            paused_ = false;
            world_.Flag_To_Lose();
            return true;
        }
        if (b.id == 0x712) {
            // Restart：重载当前图（同次 MIX 列表）。
            ask_abort_ = false;
            paused_ = false;
            if (current_map_path_.empty() || pending_mixes_.empty()) {
                std::printf("  AskAbort Restart stub（无当前图）\n");
                return true;
            }
            const std::string path = current_map_path_;
            const std::vector<std::string> mixes = pending_mixes_;
            mixes_.clear();
            roots_.clear();
            std::string e;
            if (!Load_Map(mixes, path.c_str(), &e)) {
                std::printf("  AskAbort Restart 失败: %s\n", e.c_str());
            } else {
                std::printf("  AskAbort Restart → %s\n", path.c_str());
            }
            return true;
        }
        return true;
    }
    return false;
}

void GameShell::Draw_Money(int x, int y, int w, int amount) {
    if (!ui_digit_[0].ok()) {
        return;
    }
    std::string s = std::to_string(amount < 0 ? -amount : amount);
    if (amount < 0) {
        s.insert(s.begin(), '-');
    }
    const int gw = ui_digit_[0].w + 1;   // 字距 1 像素
    const int gh = ui_digit_[0].h;
    // 资金条高度以 CREDITS.SHP 为准（gamemd 0x72fc60：条高 = shape[+4]）。
    const int bar_h = ui_credits_.ok() ? ui_credits_.h : gh;
    const int cy0 = y + (bar_h - gh) / 2;
    int cx = x + (w - static_cast<int>(s.size()) * gw) / 2;
    for (char ch : s) {
        if (ch == '-') {
            renderer_.Draw_Rect(cx + 1, cy0 + gh / 2, gw - 2, 2, kUiGold);
            cx += gw;
            continue;
        }
        const int d = ch - '0';
        if (d < 0 || d > 9) {
            cx += gw;
            continue;
        }
        Draw_Ui(ui_digit_[d], cx, cy0);
        cx += gw;
    }
}

bool GameShell::Radar_Rect(int* x, int* y, int* w, int* h) const {
    if (!ui_radar_.ok()) {
        return false;
    }
    // gamemd 0x72fc60 侧栏矩形链（右对齐 X = 屏宽 - CREDITS.W）：
    //   CREDITS @ y=0
    //   TOP     @ y=CREDITS.H
    //   RADAR   @ y=CREDITS.H+TOP.H
    const int rx = win_w_ - kSidebarW;
    const int ry = (ui_credits_.ok() ? ui_credits_.h : 0) +
                   (ui_top_.ok() ? ui_top_.h : 0);
    if (x) *x = rx;
    if (y) *y = ry;
    if (w) *w = ui_radar_.w;
    if (h) *h = ui_radar_.h;
    return true;
}

int GameShell::Player_Side() const {
    // rulesmd.ini [Sides]：GDI 第一、Nod 第二、ThirdSide 第三。
    // RadarClass::Init_For_House 用 Side==0 走盟军内缩，非 0 走苏/尤内缩。
    const int ph = world_.Player_House();
    if (ph < 0 || ph >= static_cast<int>(world_.Houses().size())) {
        return 0;
    }
    const std::string& house = world_.Houses()[static_cast<size_t>(ph)];
    static const char* kGdi[] = {"Americans", "Alliance", "French", "Germans",
                                 "British", "Korea", "Japan", "GDI"};
    static const char* kNod[] = {"Russians", "Africans", "Confederation", "Arabs",
                                 "Cubans", "Libya", "Iraq", "Nod", "Soviet"};
    static const char* kYuri[] = {"YuriCountry", "Yuri", "ThirdSide"};
    for (const char* n : kYuri) {
        if (_stricmp(house.c_str(), n) == 0) {
            return 2;
        }
    }
    for (const char* n : kNod) {
        if (_stricmp(house.c_str(), n) == 0) {
            return 1;
        }
    }
    for (const char* n : kGdi) {
        if (_stricmp(house.c_str(), n) == 0) {
            return 0;
        }
    }
    // 未知阵营：按 Suffix/常识退盟军（与二进制默认 Side==0 分支一致）
    return 0;
}

bool GameShell::Radar_Inner(int* x, int* y, int* w, int* h) const {
    // RadarClass::One_Time @0x00652CF0：
    //   X=0, Y_TOP=0x10, Y_RADAR=0x30, 缓冲 0x8C×0x6C, [this+0x11f4]=0x31(=Y_RADAR+1)。
    // Init_For_House @0x00652E90：水平内缩 Side0=+0x0B / 非0=+0x0E。
    // 开雷达绘制：RADAR 外框落在 Y_RADAR（Draw_It @0x00653FD8）；
    // 0x8C×0x6C 内容区用 [this+0x11f4]（@0x0065342D），即 Y_RADAR+1，
    // 不是 Y_TOP+4——用 Y_TOP+4 会把预览黑底盖进 TOP 工具条。
    if (!ui_radar_.ok()) {
        return false;
    }
    const int sx = win_w_ - kSidebarW;
    const int y_radar = (ui_credits_.ok() ? ui_credits_.h : 0x10) +
                        (ui_top_.ok() ? ui_top_.h : 0x20);
    const int side = Player_Side();
    const int vx = sx + ((side == 0) ? 0x0B : 0x0E);
    const int vy = y_radar + 1;  // [Radar+0x11f4] = Y_RADAR+1
    if (x) *x = vx;
    if (y) *y = vy;
    if (w) *w = 0x8C;
    if (h) *h = 0x6C;
    return true;
}

void GameShell::Draw_Top_Bar() {
    // 原版没有顶部资源条：资金/电力都在侧栏里。这里留着函数是为了不改
    // 调用点（Render 里那一串），但什么都不画。
}

void GameShell::Draw_Sidebar() {
    const int sx = win_w_ - kSidebarW;
    renderer_.Draw_Rect(sx, 0, kSidebarW, win_h_, kUiBackdrop);
    renderer_.Draw_Rect(sx, 0, 1, win_h_, kUiEdge);

    // CREDITS / TOP / RADAR 外框改由 Draw_Radar_Chrome 在小地图之后画，
    // 避免预览黑底盖住资金条（用户可见的「盟军底 + 苏军黑条叠层」）。
    // 这里只按 0x72fc60 堆叠高度占位，继续画 SIDE1 以下。
    int y = 0;
    y += ui_credits_.ok() ? ui_credits_.h : 0;
    y += ui_top_.ok() ? ui_top_.h : 0;
    y += ui_radar_.ok() ? ui_radar_.h : 0;
    const int head_y = y;
    if (ui_side1_.ok()) {
        Draw_Ui(ui_side1_, sx, head_y);
        y = head_y + ui_side1_.h;
    }

    for (int i = 0; i < 4; ++i) {
        if (!ui_tab_[i].ok()) {
            continue;
        }
        Draw_Ui(ui_tab_[i], sx + 26 + i * 29, head_y + 42,
                (i == sidebar_tab_) ? 1 : 0);
    }

    const int row_h = ui_side2_.ok() ? ui_side2_.h : 50;
    const int grid_bottom = win_h_ - (ui_side3_.ok() ? ui_side3_.h : 0);
    if (row_h > 0 && grid_bottom > y) {
        int gy = y;
        while (gy + row_h <= grid_bottom) {
            if (ui_side2_.ok()) {
                Draw_Ui(ui_side2_, sx, gy);
            } else {
                renderer_.Draw_Rect(sx + 24, gy + 5, 60, 40, kUiSlot);
                renderer_.Draw_Rect(sx + 86, gy + 5, 60, 40, kUiSlot);
            }
            gy += row_h;
        }
        if (gy < grid_bottom) {
            const int tail = grid_bottom - row_h;
            if (tail >= y) {
                const UiPiece& row = ui_side2b_.ok() ? ui_side2b_ : ui_side2_;
                Draw_Ui(row, sx, tail);
            }
        }
    }
    Draw_Cameos(sx, y, grid_bottom);
    if (ui_side3_.ok()) {
        Draw_Ui(ui_side3_, sx, grid_bottom);
    }

    Draw_Power_Pips(sx, y, grid_bottom);

    if (cursor_mode_ != 0) {
        const float c[4] = {1.0f, 0.3f, 0.3f, 1.0f};
        renderer_.Draw_Rect(sx + 4, y + 2, kSidebarW - 8, 8, c);
    }
}

void GameShell::Draw_Power_Pips(int sx, int y0, int y1) {
    if (!ui_powerp_.ok() || y1 <= y0) {
        return;
    }
    // PowerClass::Draw_It：条带从 SIDE1 下沿铺到 SIDE3 上沿，步进 3。
    // 原版是**自下而上**点亮——产出/负载从底端往上堆，顶上留空。
    // 帧 0 空、1 输出、2/3 负载偏红。
    const int px = sx + kPowerPipX;
    const int span = y1 - y0;
    // 按产出比例点亮；欠电整段偏红；无电则全空（帧0）。
    int fill = 0;
    const int out = world_.Power_Output();
    const int drain = world_.Power_Drain();
    if (out > 0) {
        fill = span * out / (std::max)(out, drain + out);
    }
    if (power_warn_ && out > 0) {
        fill = span / 3;
    }
    int y = y1 - ui_powerp_.h;
    while (y >= y0) {
        const int from_bottom = y1 - y;
        const int frame = (from_bottom <= fill) ? (power_warn_ ? 3 : 1) : 0;
        Draw_Ui(ui_powerp_, px, y, frame);
        y -= kPowerPipStep;
    }
}

void GameShell::Refresh_Power() {
    int drain = 0, output = 0;
    const int ph = world_.Player_House();
    for (const Object& o : world_.Objects()) {
        if (o.house != ph || o.hp <= 0 || o.kind != MapObjectKind::Building) {
            continue;
        }
        const UnitModel* um = sprites_.Models().Resolve(o.type.c_str());
        if (um == nullptr || um->power == 0) {
            continue;
        }
        if (um->power > 0) {
            output += um->power;
        } else {
            drain += -um->power;
        }
    }
    world_.Set_Power_State(drain, output);
    power_warn_ = world_.Low_Power();
}

void GameShell::Refresh_Sidebar() {
    sidebar_items_.clear();
    UnitModelDB& db = sprites_.Models();
    for (const std::string& n : db.Tab_Units(sidebar_tab_)) {
        db.Resolve(n.c_str());
    }
    const char* owner = "";
    const int ph = world_.Player_House();
    if (ph >= 0 && ph < static_cast<int>(world_.Houses().size())) {
        owner = world_.Houses()[static_cast<size_t>(ph)].c_str();
    }
    db.Buildable(sidebar_tab_, owner, db.Start_Tech_Level(), &sidebar_items_);
    for (const std::string& name : sidebar_items_) {
        Ensure_Cameo(name);
    }
}

int GameShell::Cameo_At(int sx, int sy) const {
    // Draw_Cameos 布局：相对侧栏的两列 60×48，行高 50。
    const int sidebar_x = win_w_ - kSidebarW;
    if (sx < sidebar_x) {
        return -1;
    }
    // 估算 cameo 区域：SIDE1+TOP+RADAR 之下。用与 Draw_Sidebar 相同的锚点。
    int y0 = 0;
    y0 += ui_credits_.ok() ? ui_credits_.h : 0;
    y0 += ui_top_.ok() ? ui_top_.h : 0;
    y0 += ui_radar_.ok() ? ui_radar_.h : 0;
    y0 += ui_side1_.ok() ? ui_side1_.h : 0;
    const int y1 = win_h_ - (ui_side3_.ok() ? ui_side3_.h : 0);
    if (sy < y0 || sy >= y1) {
        return -1;
    }
    const int local_x = sx - sidebar_x;
    const int row = (sy - y0) / kCameoRowH;
    int col = -1;
    if (local_x >= kCameoX0 && local_x < kCameoX0 + kCameoW) {
        col = 0;
    } else if (local_x >= kCameoX1 && local_x < kCameoX1 + kCameoW) {
        col = 1;
    }
    if (col < 0 || row < 0) {
        return -1;
    }
    const int idx = cameo_scroll_ * 2 + row * 2 + col;
    if (idx < 0 || idx >= static_cast<int>(sidebar_items_.size())) {
        return -1;
    }
    return idx;
}

bool GameShell::Queue_Build(const std::string& type) {
    // 对齐 Event#14 → Begin_Production @0x4FA350（非 AI BaseNode 直调）。
    // 进度帧数仍用 Get_Build_Time @0x711EE0；不发明多队列。
    if (build_active_ || placing_ || type.empty()) {
        return false;
    }
    UnitModelDB& db = sprites_.Models();
    const UnitModel* um = db.Resolve(type.c_str());
    if (um == nullptr || um->cost <= 0) {
        return false;
    }
    if (credits_ < um->cost) {
        return false;
    }
    const int ph = world_.Player_House();
    // @0x4FA438 FindSuitableFactory：无匹配 Factory= 建筑 → "No-one can build."
    if (!world_.House_Has_Suitable_Factory(ph, type.c_str())) {
        std::printf("  无人可造 %s（无 Factory=%s）\n", type.c_str(),
                    UnitModelDB::Needed_Factory_For_Category(um->category));
        return false;
    }
    // Prerequisite=：逗号分隔 AND；单项内竖线 OR（GACNST|NACNST）。
    for (const std::string& pre : um->prereq) {
        if (pre.empty() || _stricmp(pre.c_str(), "none") == 0) {
            continue;
        }
        bool ok_pre = false;
        size_t start = 0;
        while (start <= pre.size()) {
            size_t bar = pre.find('|', start);
            if (bar == std::string::npos) {
                bar = pre.size();
            }
            std::string one = pre.substr(start, bar - start);
            while (!one.empty() && (one.front() == ' ' || one.front() == '\t')) {
                one.erase(one.begin());
            }
            while (!one.empty() && (one.back() == ' ' || one.back() == '\t')) {
                one.pop_back();
            }
            if (!one.empty() && world_.House_Meets_Prereq(ph, one.c_str())) {
                ok_pre = true;
                break;
            }
            if (bar >= pre.size()) {
                break;
            }
            start = bar + 1;
        }
        if (!ok_pre) {
            std::printf("  缺少前置 %s，无法造 %s\n", pre.c_str(), type.c_str());
            return false;
        }
    }
    // gamemd TechnoType::Get_Build_Time @0x00711EE0：
    //   frames = Cost * Rules.BuildSpeed * 0.9（0x007F4E80）
    const int needed = db.Build_Frames(um->cost);
    credits_ -= um->cost;
    world_.Set_Player_Credits(credits_);
    world_.Set_House_Credits(world_.Player_House(), credits_);
    build_job_.type = type;
    build_job_.tab = um->category >= 0 ? um->category : sidebar_tab_;
    build_job_.progress = 0;
    build_job_.progress_fp = 0.0;
    build_job_.needed = needed;
    build_job_.cost = um->cost;
    build_active_ = true;
    std::printf("  开始生产 %s（$%d，%d 帧）\n", type.c_str(), um->cost, needed);
    return true;
}

void GameShell::Finish_Build(const std::string& type, int tab) {
    UnitModelDB& db = sprites_.Models();
    const UnitModel* um = db.Resolve(type.c_str());
    if (um == nullptr) {
        return;
    }
    if (tab == 0 || tab == 1 || um->category == 0 || um->category == 1) {
        // 建筑：进入放置模式
        place_type_ = type;
        placing_ = true;
        std::printf("  %s 就绪，左键放置\n", type.c_str());
        return;
    }
    // 单位：在玩家第一个建筑旁生成
    float hx = 0.0f, hy = 0.0f;
    if (!world_.Home_Cell(&hx, &hy)) {
        world_.Spawn_Cell(0, &hx, &hy);
    }
    MapObjectKind kind = MapObjectKind::Unit;
    if (um->category == 2) {
        kind = MapObjectKind::Infantry;
    } else if (um->category == 3) {
        kind = MapObjectKind::Unit;
    }
    const int id = world_.Spawn(type.c_str(), kind, world_.Player_House(),
                                hx + 2.0f, hy + 1.0f);
    std::printf("  生产完成 %s id=%d @ (%.1f,%.1f)\n", type.c_str(), id, hx, hy);
    Refresh_Sidebar();
}

bool GameShell::Place_Pending_Building(float cx, float cy) {
    if (!placing_ || place_type_.empty()) {
        return false;
    }
    // BuildingType::CanPlaceHere @0x00464AC0（PlaceAnywhere / Land.Buildable / 占位）。
    if (!world_.Can_Place_Building(place_type_.c_str(), cx, cy)) {
        std::printf("  无法放置 %s @ (%.1f,%.1f)\n", place_type_.c_str(), cx, cy);
        return false;
    }
    const int id = world_.Spawn(place_type_.c_str(), MapObjectKind::Building,
                                world_.Player_House(), cx, cy);
    std::printf("  放置 %s id=%d @ (%.1f,%.1f)\n", place_type_.c_str(), id, cx, cy);
    place_type_.clear();
    placing_ = false;
    Refresh_Sidebar();
    Refresh_Power();
    return id >= 0;
}

bool GameShell::Try_Repair_Or_Sell(float cx, float cy) {
    if (cursor_mode_ == 0) {
        return false;
    }
    const int id = world_.Pick_At(cx, cy);
    Object* o = world_.Find(id);
    if (o == nullptr || o->house != world_.Player_House() || o->hp <= 0) {
        return false;
    }
    UnitModelDB& db = sprites_.Models();
    if (cursor_mode_ == 1) {
        // 修理：RepairRate 分钟/步（×900=帧间隔），每步 +RepairStep HP，
        // 费用见 0x007120D0（Cost/(Strength/Step)*RepairPercent）。
        if (o->hp >= o->hp_max) {
            return true;
        }
        if ((logic_frame_ % db.Repair_Interval_Frames()) != 0) {
            return true;  // 未到步进帧，光标仍算命中
        }
        const bool infantry = (o->kind == MapObjectKind::Infantry);
        const UnitModel* um = db.Resolve(o->type.c_str());
        const int cost = (um != nullptr) ? um->cost : 0;
        const int step_cost = db.Repair_Step_Cost(cost, o->hp_max, infantry);
        if (credits_ < step_cost) {
            return true;
        }
        const int heal = infantry ? db.IRepair_Step() : db.Repair_Step();
        credits_ -= step_cost;
        world_.Set_Player_Credits(credits_);
        world_.Set_House_Credits(world_.Player_House(), credits_);
        o->hp = (std::min)(o->hp_max, o->hp + heal);
        return true;
    }
    if (cursor_mode_ == 2) {
        // 变卖：0x00711F60 fld RefundPercent；退 Cost * RefundPercent。
        if (o->kind != MapObjectKind::Building &&
            o->kind != MapObjectKind::Unit &&
            o->kind != MapObjectKind::Infantry) {
            return false;
        }
        const UnitModel* um = db.Resolve(o->type.c_str());
        const int cost = (um != nullptr) ? um->cost : 0;
        const int refund =
            static_cast<int>(static_cast<double>(cost) * db.Refund_Percent() + 0.5);
        credits_ += refund;
        world_.Set_Player_Credits(credits_);
        world_.Set_House_Credits(world_.Player_House(), credits_);
        o->hp = 0;
        o->selectable = false;
        o->selected = false;
        std::printf("  变卖 %s 退 $%d（RefundPercent）\n", o->type.c_str(), refund);
        cursor_mode_ = 0;
        Refresh_Power();
        Refresh_Sidebar();
        return true;
    }
    return false;
}

const UiPiece* GameShell::Ensure_Cameo(const std::string& unit) {
    const UnitModel* um = sprites_.Models().Resolve(unit.c_str());
    if (um == nullptr || um->cameo.empty()) {
        return nullptr;
    }
    const auto it = ui_cameo_.find(um->cameo);
    if (it != ui_cameo_.end()) {
        return it->second.ok() ? &it->second : nullptr;
    }
    UiPiece piece;
    std::string fname = um->cameo + ".shp";
    std::vector<uint8_t> data;
    for (MixFileClass* m : roots_) {
        data = m->Read_Deep(fname.c_str());
        if (!data.empty()) {
            break;
        }
    }
    if (data.empty()) {
        ui_cameo_.emplace(um->cameo, piece);
        return nullptr;
    }
    ShpFile shp;
    if (!shp.Load(data.data(), data.size()) || shp.Frame_Count() <= 0) {
        ui_cameo_.emplace(um->cameo, piece);
        return nullptr;
    }
    uint8_t pal768[768];
    std::vector<uint8_t> pal6;
    for (MixFileClass* m : roots_) {
        pal6 = m->Read_Deep("cameo.pal");
        if (pal6.size() >= 768) {
            break;
        }
        pal6.clear();
    }
    if (pal6.size() >= 768) {
        SpriteCache::Expand_Pal768(pal6.data(), pal768);
    } else {
        for (int i = 0; i < 256; ++i) {
            pal768[i * 3 + 0] = pal768[i * 3 + 1] = pal768[i * 3 + 2] =
                static_cast<uint8_t>(i);
        }
    }
    const int cw = shp.Width();
    const int ch = shp.Height();
    const std::vector<uint8_t>& px = shp.Frame_Pixels(0);
    const ShpFrameInfo& fi = shp.Frame_Info(0);
    if (cw <= 0 || ch <= 0 || fi.w <= 0 || fi.h <= 0 ||
        px.size() < static_cast<size_t>(fi.w) * fi.h) {
        ui_cameo_.emplace(um->cameo, piece);
        return nullptr;
    }
    std::vector<uint8_t> rgba(static_cast<size_t>(cw) * ch * 4, 0);
    for (int y = 0; y < fi.h; ++y) {
        for (int x = 0; x < fi.w; ++x) {
            const uint8_t idx = px[static_cast<size_t>(y) * fi.w + x];
            if (idx == 0) {
                continue;
            }
            const int dx = static_cast<int>(fi.x) + x;
            const int dy = static_cast<int>(fi.y) + y;
            if (dx < 0 || dy < 0 || dx >= cw || dy >= ch) {
                continue;
            }
            const size_t p = (static_cast<size_t>(dy) * cw + dx) * 4;
            const size_t c = static_cast<size_t>(idx) * 3;
            rgba[p + 0] = pal768[c + 0];
            rgba[p + 1] = pal768[c + 1];
            rgba[p + 2] = pal768[c + 2];
            rgba[p + 3] = 255;
        }
    }
    piece.w = cw;
    piece.h = ch;
    piece.frames = 1;
    piece.sprite.assign(1, renderer_.Upload_Sprite_RGBA(rgba.data(), cw, ch));
    auto ins = ui_cameo_.emplace(um->cameo, std::move(piece));
    return ins.first->second.ok() ? &ins.first->second : nullptr;
}

const UiPiece* GameShell::Ensure_Projectile(const std::string& image) {
    if (image.empty()) {
        return nullptr;
    }
    const auto it = ui_proj_.find(image);
    if (it != ui_proj_.end()) {
        return it->second.ok() ? &it->second : nullptr;
    }
    UiPiece piece;
    std::string fname = image + ".shp";
    std::vector<uint8_t> data;
    for (MixFileClass* m : roots_) {
        data = m->Read_Deep(fname.c_str());
        if (!data.empty()) {
            break;
        }
    }
    if (data.empty()) {
        ui_proj_.emplace(image, piece);
        return nullptr;
    }
    ShpFile shp;
    if (!shp.Load(data.data(), data.size()) || shp.Frame_Count() <= 0) {
        ui_proj_.emplace(image, piece);
        return nullptr;
    }
    // 弹道多用 unit/anim 调色；无 SIDEBAR.PAL 作缺省展开。
    uint8_t pal768[768];
    std::vector<uint8_t> pal6;
    for (MixFileClass* m : roots_) {
        pal6 = m->Read_Deep("unit.pal");
        if (pal6.size() >= 768) {
            break;
        }
        pal6.clear();
    }
    if (pal6.size() < 768) {
        for (MixFileClass* m : roots_) {
            pal6 = m->Read_Deep("anim.pal");
            if (pal6.size() >= 768) {
                break;
            }
            pal6.clear();
        }
    }
    if (pal6.size() >= 768) {
        SpriteCache::Expand_Pal768(pal6.data(), pal768);
    } else {
        for (int i = 0; i < 256; ++i) {
            pal768[i * 3 + 0] = pal768[i * 3 + 1] = pal768[i * 3 + 2] =
                static_cast<uint8_t>(i);
        }
    }
    const int cw = shp.Width();
    const int ch = shp.Height();
    const std::vector<uint8_t>& px = shp.Frame_Pixels(0);
    const ShpFrameInfo& fi = shp.Frame_Info(0);
    if (cw <= 0 || ch <= 0 || fi.w <= 0 || fi.h <= 0 ||
        px.size() < static_cast<size_t>(fi.w) * fi.h) {
        ui_proj_.emplace(image, piece);
        return nullptr;
    }
    std::vector<uint8_t> rgba(static_cast<size_t>(cw) * ch * 4, 0);
    for (int y = 0; y < fi.h; ++y) {
        for (int x = 0; x < fi.w; ++x) {
            const uint8_t idx = px[static_cast<size_t>(y) * fi.w + x];
            if (idx == 0) {
                continue;
            }
            const int dx = fi.x + x;
            const int dy = fi.y + y;
            if (dx < 0 || dy < 0 || dx >= cw || dy >= ch) {
                continue;
            }
            const size_t p = (static_cast<size_t>(dy) * cw + dx) * 4;
            const size_t c = static_cast<size_t>(idx) * 3;
            rgba[p + 0] = pal768[c + 0];
            rgba[p + 1] = pal768[c + 1];
            rgba[p + 2] = pal768[c + 2];
            rgba[p + 3] = 255;
        }
    }
    piece.w = cw;
    piece.h = ch;
    piece.frames = 1;
    piece.sprite.assign(1, renderer_.Upload_Sprite_RGBA(rgba.data(), cw, ch));
    auto ins = ui_proj_.emplace(image, std::move(piece));
    return ins.first->second.ok() ? &ins.first->second : nullptr;
}

void GameShell::Draw_Cameos(int sx, int y0, int y1) {
    if (y1 <= y0 || sidebar_items_.empty()) {
        return;
    }
    const int rows = (y1 - y0) / kCameoRowH;
    const int slots = rows * 2;
    const int max_scroll =
        (std::max)(0, (static_cast<int>(sidebar_items_.size()) + 1) / 2 - rows);
    if (cameo_scroll_ > max_scroll) {
        cameo_scroll_ = max_scroll;
    }
    if (cameo_scroll_ < 0) {
        cameo_scroll_ = 0;
    }
    // R-UP / R-DN（Sidebar.CPP 0x006A59CA）：可翻时画在格区上下。
    if (ui_rup_.ok() && cameo_scroll_ > 0) {
        Draw_Ui(ui_rup_, sx + (kSidebarW - ui_rup_.w) / 2, y0 - ui_rup_.h, 0);
    }
    if (ui_rdn_.ok() && cameo_scroll_ < max_scroll) {
        Draw_Ui(ui_rdn_, sx + (kSidebarW - ui_rdn_.w) / 2, y1, 0);
    }
    static const float kProg[4] = {0.2f, 0.85f, 0.3f, 0.85f};
    static const float kPlace[4] = {0.95f, 0.85f, 0.2f, 0.7f};
    const int base = cameo_scroll_ * 2;
    for (int i = 0; i < slots; ++i) {
        const int item = base + i;
        if (item >= static_cast<int>(sidebar_items_.size())) {
            break;
        }
        const int col = i & 1;
        const int row = i / 2;
        const int x = sx + (col ? kCameoX1 : kCameoX0);
        const int y = y0 + row * kCameoRowH + 1;
        const std::string& name = sidebar_items_[static_cast<size_t>(item)];
        const UiPiece* p = Ensure_Cameo(name);
        if (p != nullptr) {
            Draw_Ui(*p, x, y);
        }
        // 生产进度条（Factory 进度简化画在 cameo 底边）
        if (build_active_ && build_job_.type == name && build_job_.needed > 0) {
            const int bw = kCameoW * build_job_.progress / build_job_.needed;
            renderer_.Draw_Rect(x, y + kCameoRowH - 6, (std::max)(1, bw), 4, kProg);
        }
        if (placing_ && place_type_ == name) {
            renderer_.Draw_Rect_Outline(x, y, kCameoW, 48, kPlace, 2);
        }
    }
}

void GameShell::Draw_Radar_Chrome() {
    // CREDITS 最后压顶。RADAR 外框已在 Draw_Radar 里、缩略图之前画过；
    // 这里再画会把预览盖黑。
    const int sx = win_w_ - kSidebarW;
    if (ui_credits_.ok()) {
        Draw_Ui(ui_credits_, sx, 0);
    }
    Draw_Money(sx, 0, kSidebarW, credits_);
}

void GameShell::Draw_Radar() {
    const int sx = win_w_ - kSidebarW;
    // TOP 在预览之前：工具条不被 0x8C×0x6C 黑底盖住。
    // 预览锚在 Y_RADAR+1（见 Radar_Inner），落在 RADAR 外框内。
    if (ui_top_.ok()) {
        const int y_top = ui_credits_.ok() ? ui_credits_.h : 0;
        Draw_Ui(ui_top_, sx, y_top);
        // SIDEBTTN / ButtonXX：有选中单位时画在 TOP 条上（指令钮条）。
        if (world_.Selected_Count() > 0) {
            const int bx0 = sx + 4;
            const int by0 = y_top + 2;
            for (int i = 0; i < 4; ++i) {
                if (ui_btn_[i].ok()) {
                    Draw_Ui(ui_btn_[i], bx0 + i * (ui_btn_[i].w + 2), by0, 0);
                } else if (ui_sidebttn_.ok()) {
                    Draw_Ui(ui_sidebttn_, bx0 + i * (ui_sidebttn_.w + 2), by0,
                            i % (std::max)(1, ui_sidebttn_.frames));
                    break;
                }
            }
        }
    }
    // 开雷达外框先铺（f32 中央是黑底），再往视口里画缩略图。
    if (ui_radar_.ok()) {
        const int y_radar = (ui_credits_.ok() ? ui_credits_.h : 0) +
                            (ui_top_.ok() ? ui_top_.h : 0);
        Draw_Ui(ui_radar_, sx, y_radar, 32);
    }

    int x = 0, y = 0, size = 0, size_h = 0;
    if (!Radar_Inner(&x, &y, &size, &size_h)) {
        x = win_w_ - kSidebarW + 8;
        y = 8;
        size = kRadarSize - 16;
        size_h = size;
        renderer_.Draw_Rect(x, y, size, size_h, kUiRadarFog);
        renderer_.Draw_Rect_Outline(x, y, size, size_h, kUiEdge, 1);
    }
    if (minimap_sprite_ >= 0) {
        renderer_.Draw_Sprite(minimap_sprite_, x, y, 1.0f);
    }

    // 整张地图等比压进显示区（对象坐标是 iso dx / row）
    if (map_.Iso_Width() <= 0 || map_.Height() <= 0) {
        return;
    }
    const float scalex = static_cast<float>(size) / map_.Iso_Width();
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
        if (!world_.Cell_Revealed(static_cast<int>(o.x), static_cast<int>(o.y))) {
            continue;
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

void GameShell::Draw_Health_Pips(const Object& o, int sx, int sy, float scale) {
    // TechnoClass::DrawHealthBar @0x006F64A0（非建筑支路 ebx=8）：
    //   最多 8 格；空=帧0；满格帧由 ConditionYellow/Red 决定（1/2/4）。
    //   X 步进 4（0x6F66C7 add ebp,4）。
    if (!ui_pips_.ok() || o.hp_max <= 0) {
        return;
    }
    const double ratio =
        static_cast<double>(o.hp) / static_cast<double>(o.hp_max);
    const UnitModelDB& db = sprites_.Models();
    int fill_frame = 1;
    if (ratio <= db.Condition_Red()) {
        fill_frame = 4;  // 0x005F5CD0
    } else if (ratio <= db.Condition_Yellow()) {
        fill_frame = 2;  // 0x005F5D20
    }
    constexpr int kMaxPips = 8;   // 0x6F68DB mov ebx, 8
    constexpr int kPipStep = 4;   // 0x6F66C7
    int filled = static_cast<int>(ratio * kMaxPips + 0.5);
    if (filled < 1 && o.hp > 0) {
        filled = 1;
    }
    if (filled > kMaxPips) {
        filled = kMaxPips;
    }
    const int house_color = (o.house >= 0 && o.is_mine) ? player_color_ : 11;
    const ObjectSprite* sp = sprites_.Get(o.type.c_str(), o.facing, house_color);
    int top = sy - static_cast<int>(20.0f * scale);
    if (sp != nullptr && sp->ok) {
        top = sy + static_cast<int>(sp->off_y * scale) - 2;
    }
    const int total_w = (kMaxPips - 1) * kPipStep;
    const int x0 = sx - total_w / 2;
    for (int i = 0; i < kMaxPips; ++i) {
        const int frame = (i < filled) ? fill_frame : 0;
        Draw_Ui(ui_pips_, x0 + i * kPipStep, top, frame);
    }
}

void GameShell::Draw_Mouse() {
    if (!ui_mouse_.ok()) {
        return;
    }
    // cursor_mode_ → MouseCursorType（表下标 = 枚举值）。
    int ctype = Mouse_Default;
    if (cursor_mode_ == 1) {
        ctype = Mouse_Repair;
    } else if (cursor_mode_ == 2) {
        ctype = Mouse_Sell;
    }
    mouse_cursor_ = ctype;
    if (ctype < 0 || ctype >= kMouseShapeCount) {
        ctype = Mouse_Default;
    }
    const MouseShapeEntry& sh = kMouseShapes[ctype];
    int frame = sh.frame;
    if (sh.count > 1) {
        const int period = (sh.interval > 0) ? sh.interval : 1;
        frame = sh.frame + ((logic_frame_ / period) % sh.count);
    }
    // 热区：Set_Cursor @0x005BDB0F 对 0x3039/0xD431 的分支。
    int hot_x = 0, hot_y = 0;
    if (sh.hot_x == kMouseHotCenter) {
        hot_x = ui_mouse_.w / 2;
    } else if (sh.hot_x == kMouseHotMax) {
        hot_x = ui_mouse_.w;
    } else {
        hot_x = sh.hot_x;
    }
    if (sh.hot_y == kMouseHotCenter) {
        hot_y = ui_mouse_.h / 2;
    } else if (sh.hot_y == kMouseHotMax) {
        hot_y = ui_mouse_.h;
    } else {
        hot_y = sh.hot_y;
    }
    Draw_Ui(ui_mouse_, mouse_x_ - hot_x, mouse_y_ - hot_y, frame);
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
    if (*cx < 0 || *cy < 0 || *cx >= map_.Iso_Width() || *cy >= map_.Height()) {
        return false;
    }
    return true;
}

/// 屏幕 -> 连续格坐标。先把屏幕换算成画布像素，再反解 CNCMaps dx/dy。
bool GameShell::Pick_Cell_F(int sx, int sy, float* fx, float* fy) const {
    if (terrain_w_ <= 0) {
        return false;
    }
    if (sx < 0 || sx >= win_w_ - kSidebarW || sy < kTopBarH || sy >= win_h_) {
        return false;
    }
    float wx = 0.0f, wy = 0.0f;
    camera_.Screen_To_World(static_cast<float>(sx), static_cast<float>(sy), &wx, &wy);
    const float dx = (wx - kCellHalfW -
                      static_cast<float>(map_renderer_.Origin_X())) /
                     static_cast<float>(kCellHalfW);
    const float dy0 = (wy - kCellHalfH -
                       static_cast<float>(map_renderer_.Origin_Y())) /
                      static_cast<float>(kCellHalfH);
    // row = dy/2；高度校正一次
    float row = dy0 * 0.5f;
    const int lvl = Cell_Level(static_cast<int>(dx), static_cast<int>(row));
    const float dy1 = (wy - kCellHalfH +
                       static_cast<float>(lvl) * kLevelHeightPx -
                       static_cast<float>(map_renderer_.Origin_Y())) /
                      static_cast<float>(kCellHalfH);
    row = dy1 * 0.5f;
    *fx = dx;
    *fy = row;
    if (*fx < 0.0f || *fy < 0.0f || *fx >= static_cast<float>(map_.Iso_Width()) ||
        *fy >= static_cast<float>(map_.Height())) {
        return false;
    }
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
        case 'Q': sidebar_tab_ = 0; cameo_scroll_ = 0; Refresh_Sidebar(); break;   // 建筑页
        case 'W': sidebar_tab_ = 1; cameo_scroll_ = 0; Refresh_Sidebar(); break;   // 防御页
        case 'E': sidebar_tab_ = 2; cameo_scroll_ = 0; Refresh_Sidebar(); break;   // 步兵页
        case 'R': sidebar_tab_ = 3; cameo_scroll_ = 0; Refresh_Sidebar(); break;   // 车辆页
        case VK_PRIOR: // PageUp — 侧栏上翻
            if (cameo_scroll_ > 0) {
                --cameo_scroll_;
            }
            break;
        case VK_NEXT: // PageDown — 侧栏下翻
            ++cameo_scroll_;
            break;

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
            // 先清选择/修理变卖光标（原版亦然）。
            // 已空闲时：对齐 Options 命令入口 @0x647040 的"进选项"意图——
            // GOptions 是 Win32 对话框（GUI:ResumeMission/AbortMission…），
            // DX12 壳在模板坐标未逆完前只做暂停 + CSF 文案条，不发明按钮布局。
            if (cursor_mode_ != 0 || world_.Selected_Count() > 0 || placing_) {
                world_.Select_None();
                cursor_mode_ = 0;
                placing_ = false;
                place_type_.clear();
            } else {
                paused_ = !paused_;
                if (!paused_) {
                    ask_abort_ = false;
                }
            }
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
        if (screen_ == GameScreen::Menu) {
            Hit_Title_Menu(x, y);
            return;
        }
        if (paused_ && world_.Outcome() == MatchOutcome::Playing) {
            if (ask_abort_) {
                if (Hit_Ask_Abort(x, y)) {
                    return;
                }
            } else if (Hit_Pause_Options(x, y)) {
                return;
            }
        }
        // 雷达点击：跳转镜头（RadarClass 视口点击 → 战术图中心）
        {
            int rx = 0, ry = 0, rw = 0, rh = 0;
            if (Radar_Inner(&rx, &ry, &rw, &rh) && rw > 0 && rh > 0 &&
                x >= rx && x < rx + rw && y >= ry && y < ry + rh &&
                map_.Iso_Width() > 0 && map_.Height() > 0) {
                const float cx = static_cast<float>(x - rx) / static_cast<float>(rw) *
                                 static_cast<float>(map_.Iso_Width());
                const float cy = static_cast<float>(y - ry) / static_cast<float>(rh) *
                                 static_cast<float>(map_.Height());
                Center_On_Cell(cx, cy);
                return;
            }
        }
        // 侧栏：页签 TAB00..03（与 Draw_Sidebar 同坐标）；R-UP/R-DN；cameo
        if (x >= win_w_ - kSidebarW) {
            const int sx = win_w_ - kSidebarW;
            int y0 = 0;
            const int y_credits = ui_credits_.ok() ? ui_credits_.h : 0;
            const int y_top = y_credits;
            // TOP 指令钮：与 Draw_Radar 同锚点；0x8065/0x8066 → Display +0x11b0/+0x11b1
            // （互斥修理/变卖旗，对齐 K/L → cursor_mode_ 1/2）。
            if (ui_top_.ok() && world_.Selected_Count() > 0 &&
                y >= y_top && y < y_top + ui_top_.h) {
                const int bx0 = sx + 4;
                const int by0 = y_top + 2;
                for (int i = 0; i < 2; ++i) {
                    int bw = 24, bh = 24;
                    if (ui_btn_[i].ok()) {
                        bw = ui_btn_[i].w;
                        bh = ui_btn_[i].h;
                    } else if (ui_sidebttn_.ok()) {
                        bw = ui_sidebttn_.w;
                        bh = ui_sidebttn_.h;
                    }
                    const int bx = bx0 + i * (bw + 2);
                    if (x >= bx && x < bx + bw && y >= by0 && y < by0 + bh) {
                        if (i == 0) {
                            cursor_mode_ = (cursor_mode_ == 1) ? 0 : 1;
                        } else {
                            cursor_mode_ = (cursor_mode_ == 2) ? 0 : 2;
                        }
                        return;
                    }
                }
            }
            y0 += y_credits;
            y0 += ui_top_.ok() ? ui_top_.h : 0;
            y0 += ui_radar_.ok() ? ui_radar_.h : 0;
            const int head_y = y0;
            // SIDE1 页签：sx+26+i*29, head_y+42（同 Draw_Sidebar）
            if (ui_side1_.ok() && y >= head_y && y < head_y + ui_side1_.h) {
                for (int i = 0; i < 4; ++i) {
                    if (!ui_tab_[i].ok()) {
                        continue;
                    }
                    const int tx = sx + 26 + i * 29;
                    const int ty = head_y + 42;
                    if (x >= tx && x < tx + ui_tab_[i].w &&
                        y >= ty && y < ty + ui_tab_[i].h) {
                        sidebar_tab_ = i;
                        cameo_scroll_ = 0;
                        Refresh_Sidebar();
                        return;
                    }
                }
            }
            y0 += ui_side1_.ok() ? ui_side1_.h : 0;
            const int y1 = win_h_ - (ui_side3_.ok() ? ui_side3_.h : 0);
            if (ui_rup_.ok() && y >= y0 - ui_rup_.h && y < y0) {
                if (cameo_scroll_ > 0) {
                    --cameo_scroll_;
                }
                return;
            }
            if (ui_rdn_.ok() && y >= y1 && y < y1 + ui_rdn_.h) {
                ++cameo_scroll_;
                return;
            }
            const int idx = Cameo_At(x, y);
            if (idx >= 0) {
                Queue_Build(sidebar_items_[static_cast<size_t>(idx)]);
                return;
            }
        }
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
        const int dx = x - drag_x0_;
        const int dy = y - drag_y0_;
        if (dx * dx + dy * dy < 16) {
            float fx = 0.0f, fy = 0.0f;
            if (Pick_Cell_F(x, y, &fx, &fy)) {
                if (placing_) {
                    Place_Pending_Building(fx, fy);
                    return;
                }
                if (cursor_mode_ != 0) {
                    Try_Repair_Or_Sell(fx, fy);
                    return;
                }
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
        if (placing_) {
            // 右键取消放置，退款
            credits_ += build_job_.cost;
            place_type_.clear();
            placing_ = false;
            std::printf("  取消放置，退回 $%d\n", build_job_.cost);
            return;
        }
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
    // 框选只命中己方（is_mine）；敌方单位不进选择集。
    int mine_techno = 0;
    for (const Object& o : world_.Objects()) {
        if (o.Is_Techno() && o.is_mine) {
            ++mine_techno;
        }
    }
    const int n_box = world_.Select_In_Rect(-1.0f, -1.0f,
                                            static_cast<float>(map_.Iso_Width()),
                                            static_cast<float>(map_.Height()), false);
    check(n_box == mine_techno, "框选全图 == 己方可交互对象");
    std::printf("     框选 %d / 己方可交互 %d（全图可交互 %d）\n", n_box,
                mine_techno, techno);

    world_.Select_None();
    check(world_.Selected_Count() == 0, "取消选择后没有选中");

    // 点选：拿第一个己方可交互对象的位置去点
    const Object* first = nullptr;
    for (const Object& o : world_.Objects()) {
        if (o.Is_Techno() && o.is_mine) {
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
    // 挑一个能动的单位；Arena 预置兵可能全是 Sticky，没有则 Spawn 一辆测移动。
    Object* mover = nullptr;
    for (Object& o : world_.Objects()) {
        if (o.Is_Techno() && o.speed > 0.0f && o.mission != Mission::Sleep &&
            o.mission != Mission::Sticky && o.kind != MapObjectKind::Building) {
            mover = &o;
            break;
        }
    }
    if (mover == nullptr) {
        const int mid = world_.Spawn("MTNK", MapObjectKind::Unit, world_.Player_House(),
                                     30.5f, 30.5f);
        mover = world_.Find(mid);
        check(mover != nullptr && mover->speed > 0.0f, "Spawn MTNK 可移动");
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
        for (int i = 0; i < kLogicFps * 3; ++i) {
            world_.Update(kLogicDt);
        }
        const float after = std::sqrt((mover->x - tx) * (mover->x - tx) +
                                      (mover->y - ty) * (mover->y - ty));
        check(after < before - 0.5f, "推进 3 秒后离目标更近了");
        std::printf("     距目标 %.2f -> %.2f 格\n", before, after);
        check(mover->mission == Mission::Move || after < 0.05f,
              "任务状态是 Move（或已到达）");
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

    // TOP 钮：画位点击切换修理/变卖（同 K/L；gadget 0x8065/0x8066）
    if (world_.Select_All_Combat() > 0) {
        const int sx = win_w_ - kSidebarW;
        const int y_top = ui_credits_.ok() ? ui_credits_.h : 0;
        int bw = ui_btn_[0].ok() ? ui_btn_[0].w
                                 : (ui_sidebttn_.ok() ? ui_sidebttn_.w : 24);
        int bh = ui_btn_[0].ok() ? ui_btn_[0].h
                                 : (ui_sidebttn_.ok() ? ui_sidebttn_.h : 24);
        On_Mouse_Down(0, sx + 4 + bw / 2, y_top + 2 + bh / 2, false);
        check(cursor_mode_ == 1, "TOP 钮0 → 修理");
        const int bw1 = ui_btn_[1].ok() ? ui_btn_[1].w : bw;
        const int bh1 = ui_btn_[1].ok() ? ui_btn_[1].h : bh;
        On_Mouse_Down(0, sx + 4 + (bw + 2) + bw1 / 2, y_top + 2 + bh1 / 2, false);
        check(cursor_mode_ == 2, "TOP 钮1 → 变卖");
        On_Key_Down(VK_ESCAPE, false, false);
        check(cursor_mode_ == 0, "Esc 清 TOP 模式");
        world_.Select_None();
    }

    // Esc 空闲 → 暂停（GOptions 意图；再 Esc 恢复）
    {
        world_.Select_None();
        cursor_mode_ = 0;
        paused_ = false;
        On_Key_Down(VK_ESCAPE, false, false);
        check(paused_, "Esc 空闲进入暂停");
        // Resume 钮（对话框单位 425,346 缩放后）
        {
            const int vw = win_w_ - kSidebarW;
            constexpr int kDlgW = 533;
            constexpr int kDlgH = 369;
            const float scale =
                (std::min)(static_cast<float>(vw - 40) / kDlgW,
                           static_cast<float>(win_h_ - 40) / kDlgH);
            const int px = (vw - static_cast<int>(kDlgW * scale)) / 2;
            const int py = (win_h_ - static_cast<int>(kDlgH * scale)) / 2;
            const int bx = px + static_cast<int>(425 * scale) + 4;
            const int by = py + static_cast<int>(346 * scale) + 4;
            On_Mouse_Down(0, bx, by, false);
            check(!paused_, "点击 ResumeMission 恢复");
        }
        On_Key_Down(VK_ESCAPE, false, false);
        check(paused_, "Esc 再进暂停");
        On_Key_Down(VK_ESCAPE, false, false);
        check(!paused_, "Esc 再按恢复");
        // AskAbortMission（对话框 182）：Abort → 确认条 → Resume 取消
        {
            On_Key_Down(VK_ESCAPE, false, false);
            check(paused_, "AskAbort 前暂停");
            const int vw = win_w_ - kSidebarW;
            constexpr int kDlgW = 533;
            constexpr int kDlgH = 369;
            const float scale =
                (std::min)(static_cast<float>(vw - 40) / kDlgW,
                           static_cast<float>(win_h_ - 40) / kDlgH);
            const int px = (vw - static_cast<int>(kDlgW * scale)) / 2;
            const int py = (win_h_ - static_cast<int>(kDlgH * scale)) / 2;
            On_Mouse_Down(0, px + static_cast<int>(425 * scale) + 4,
                          py + static_cast<int>(230 * scale) + 4, false);
            check(ask_abort_, "AbortMission → AskAbort");
            On_Mouse_Down(0, px + static_cast<int>(425 * scale) + 4,
                          py + static_cast<int>(346 * scale) + 4, false);
            check(!ask_abort_, "AskAbort Resume 取消");
            check(paused_, "取消后仍在 GOptions");
            // Restart：对话框 182 重载当前图
            {
                const size_t before_n = world_.Objects().size();
                On_Mouse_Down(0, px + static_cast<int>(425 * scale) + 4,
                              py + static_cast<int>(230 * scale) + 4, false);
                check(ask_abort_, "AbortMission → AskAbort(再)");
                On_Mouse_Down(0, px + static_cast<int>(431 * scale) + 4,
                              py + static_cast<int>(149 * scale) + 4, false);
                check(!ask_abort_, "AskAbort Restart 关确认");
                check(!paused_, "Restart 后取消暂停");
                check(screen_ == GameScreen::Battle, "Restart 后仍在战场");
                check(world_.Objects().size() > 0, "Restart 后地图有对象");
                std::printf("     Restart 对象 %zu -> %zu\n", before_n,
                            world_.Objects().size());
            }
            if (paused_) {
                On_Key_Down(VK_ESCAPE, false, false);
            }
            check(!paused_, "Esc 退出暂停");
        }
        // 主菜单对话框 226 命中（坐标缩放与 GOptions 同式）
        {
            const int vw = win_w_;
            constexpr int kDlgW = 533;
            constexpr int kDlgH = 369;
            const float scale =
                (std::min)(static_cast<float>(vw - 40) / kDlgW,
                           static_cast<float>(win_h_ - 40) / kDlgH);
            const int pw = static_cast<int>(kDlgW * scale);
            const int ph = static_cast<int>(kDlgH * scale);
            const int px = (vw - pw) / 2;
            const int py = (win_h_ - ph) / 2;
            // SinglePlayer 钮中心
            const int tx = px + static_cast<int>((425 + 54) * scale);
            const int ty = py + static_cast<int>((125 + 11) * scale);
            const GameScreen was = screen_;
            screen_ = GameScreen::Menu;
            title_page_ = 0;
            skirmish_map_.clear();  // 避免真载入
            check(Hit_Title_Menu(tx, ty), "Title SinglePlayer 命中");
            check(title_page_ == 1, "进入 SinglePlayerMenu");
            screen_ = was;
            title_page_ = 0;
            std::printf("     Title.PCX %s\n", ui_title_.ok() ? "已载" : "未命中");
        }
    }

    // 编队热键：Ctrl+2 存、2 取
    if (world_.Select_All_Combat() > 0) {
        world_.Team_Set(1);
        world_.Select_None();
        On_Key_Down('2', false, false);
        check(world_.Selected_Count() > 0, "数字键 2 取回编队");
    }

    std::printf("== 战斗 / 电力 / 生产 ==\n");
    // 武器字段：至少一个有 Primary 的单位读到 Damage>0
    int armed = 0;
    for (const Object& o : world_.Objects()) {
        if (o.Is_Techno() && o.damage > 0 && o.range > 0.0f) {
            ++armed;
        }
    }
    check(armed > 0, "rules 武器灌入：至少 1 个单位有 Damage/Range");
    std::printf("     有武器单位 %d\n", armed);

    // 受控对打：Spawn 一对敌对步兵（Arena 预置多为 Sticky/同阵营）。
    {
        int enemy_h = -1;
        for (int i = 0; i < static_cast<int>(world_.Houses().size()); ++i) {
            if (i == world_.Player_House()) {
                continue;
            }
            const std::string& hn = world_.Houses()[static_cast<size_t>(i)];
            if (hn == "Neutral" || hn == "Special") {
                continue;
            }
            enemy_h = i;
            break;
        }
        if (enemy_h < 0) {
            enemy_h = world_.Player_House() >= 0 ? world_.Player_House() + 1 : 1;
        }
        // ShortGame：无建筑且无 BaseUnit 会立刻败北。先给对手一辆 AMCV。
        world_.Spawn("AMCV", MapObjectKind::Unit, enemy_h, 48.5f, 40.5f);
        const int aid = world_.Spawn("E1", MapObjectKind::Infantry, world_.Player_House(),
                                     40.5f, 40.5f);
        const int vid = world_.Spawn("E2", MapObjectKind::Infantry, enemy_h, 41.0f, 40.5f);
        Object* atk = world_.Find(aid);
        Object* vic = world_.Find(vid);
        check(atk != nullptr && vic != nullptr, "Spawn 攻防测试单位");
        if (atk != nullptr && vic != nullptr) {
            if (atk->damage <= 0) {
                atk->damage = 25;
                atk->range = 5.0f;
                atk->rof = 15;
            }
            // 即时弹：Speed=0 对齐 Inviso/无飞行弹结算，先验伤害链。
            atk->weapon_speed = 0;
            atk->proj_inviso = true;
            const int hp0 = vic->hp;
            world_.Select_None();
            atk->selected = true;
            check(world_.Order_Attack(vic->id) == 1, "Attack 命令下达");
            for (int i = 0; i < kLogicFps * 4; ++i) {
                world_.Update(kLogicDt);
                if (vic->hp < hp0) {
                    break;
                }
            }
            check(vic->hp < hp0, "攻击后目标血量下降");
            std::printf("     %s 打 %s：HP %d -> %d\n", atk->type.c_str(),
                        vic->type.c_str(), hp0, vic->hp);

            // 弹道：Speed=40 leptons/帧（规则 120mm），隔开距离后应先有子弹再掉血。
            const int aid2 = world_.Spawn("MTNK", MapObjectKind::Unit,
                                          world_.Player_House(), 42.5f, 40.5f);
            const int vid2 =
                world_.Spawn("E2", MapObjectKind::Infantry, enemy_h, 47.5f, 40.5f);
            Object* atk2 = world_.Find(aid2);
            Object* vic2 = world_.Find(vid2);
            check(atk2 != nullptr && vic2 != nullptr, "Spawn 弹道测试单位");
            if (atk2 != nullptr && vic2 != nullptr) {
                if (atk2->damage <= 0) {
                    atk2->damage = 90;
                    atk2->range = 6.0f;
                    atk2->rof = 15;
                }
                atk2->weapon_speed = 40;
                atk2->proj_inviso = false;
                atk2->proj_arcing = true;
                const int hp1 = vic2->hp;
                world_.Select_None();
                atk2->selected = true;
                check(world_.Order_Attack(vic2->id) == 1, "弹道 Attack 下达");
                bool saw_bullet = false;
                bool hit = false;
                for (int i = 0; i < kLogicFps * 6; ++i) {
                    world_.Update(kLogicDt);
                    if (!world_.Bullets().empty()) {
                        saw_bullet = true;
                    }
                    if (vic2->hp < hp1) {
                        hit = true;
                        break;
                    }
                }
                check(saw_bullet, "开火后弹道列表非空");
                check(hit, "弹道飞行后目标掉血");
                std::printf("     弹道命中：HP %d -> %d（曾见弹 %s）\n", hp1,
                            vic2->hp, saw_bullet ? "yes" : "no");
            }
        }
    }

    // Deploy：找 DeploysInto；没有则 Spawn AMCV
    Object* mcv = nullptr;
    for (Object& o : world_.Objects()) {
        if (o.Is_Techno() && !o.deploys_into.empty() && o.hp > 0) {
            mcv = &o;
            break;
        }
    }
    if (mcv == nullptr) {
        const int mid = world_.Spawn("AMCV", MapObjectKind::Unit, world_.Player_House(),
                                     50.5f, 50.5f);
        mcv = world_.Find(mid);
    }
    if (mcv != nullptr && !mcv->deploys_into.empty()) {
        const std::string into = mcv->deploys_into;
        world_.Select_None();
        mcv->selected = true;
        check(world_.Order_Deploy() == 1, "Deploy 命令下达");
        world_.Update(kLogicDt);
        check(mcv->type == into && mcv->kind == MapObjectKind::Building,
              "Deploy 后变成 DeploysInto 建筑");
        std::printf("     展开 -> %s\n", mcv->type.c_str());
    } else {
        std::printf("     （无 DeploysInto，跳过 Deploy）\n");
    }

    // 电脑 Unload：IQ>=Production 门控（@0x4F85A3 → +0x1F3；Unload @0x7409F8）。
    {
        int enemy_h = -1;
        for (size_t i = 0; i < world_.Houses().size(); ++i) {
            if (static_cast<int>(i) != world_.Player_House() &&
                world_.Houses()[i] != "Neutral" &&
                world_.Houses()[i] != "Special") {
                enemy_h = static_cast<int>(i);
                break;
            }
        }
        if (enemy_h >= 0) {
            const int mid =
                world_.Spawn("AMCV", MapObjectKind::Unit, enemy_h, 70.5f, 20.5f);
            Object* em = world_.Find(mid);
            const int need_iq = sprites_.Models().IQ_Production();
            world_.Force_House_IQ(enemy_h, need_iq);
            if (em != nullptr && !em->deploys_into.empty()) {
                const std::string into = em->deploys_into;
                for (int i = 0; i < 3; ++i) {
                    world_.Update(kLogicDt);
                }
                check(em->type == into && em->kind == MapObjectKind::Building,
                      "电脑 IQ>=Production 后 BaseUnit Unload→建筑");
                std::printf("     电脑展开 -> %s（IQ>=%d）\n", em->type.c_str(),
                            need_iq);
            }
        }
    }

    // 补发电/兵营，让电力与生产前置成立
    world_.Spawn("GAPOWR", MapObjectKind::Building, world_.Player_House(), 52.5f, 50.5f);
    world_.Spawn("GAPILE", MapObjectKind::Building, world_.Player_House(), 54.5f, 50.5f);
    Refresh_Power();
    std::printf("     电力 产出 %d / 负荷 %d\n", world_.Power_Output(),
                world_.Power_Drain());
    check(world_.Power_Output() > 0, "电厂后产出 > 0");

    // Begin_Production 门控：无 Factory=InfantryType 时拒造（"No-one can build."）
    {
        // 临时清掉兵营：只留电厂，Queue_Build(E1) 应失败。
        for (Object& o : world_.Objects()) {
            if (o.type == "GAPILE") {
                o.hp = 0;
            }
        }
        check(!Queue_Build("E1"), "无兵营时 Queue_Build 拒绝");
        world_.Spawn("GAPILE", MapObjectKind::Building, world_.Player_House(),
                     54.5f, 50.5f);
        check(world_.House_Has_Suitable_Factory(world_.Player_House(), "E1"),
              "兵营后 Has_Suitable_Factory(E1)");
    }

    // 生产：造 E1（已有 GAPILE）
    {
        const int cred0 = credits_;
        On_Key_Down('E', false, false);
        Refresh_Sidebar();
        const std::string pick = "E1";
        const int before_n = world_.Count();
        check(Queue_Build(pick), "Queue_Build 扣款并开工");
        check(credits_ < cred0, "生产扣了资金");
        build_job_.needed = 2;
        build_job_.progress = 0;
        for (int i = 0; i < 4; ++i) {
            Update(kLogicDt);
        }
        check(!build_active_, "短周期后生产完成");
        check(world_.Count() > before_n || placing_, "单位生成或建筑进入放置");
        if (placing_) {
            Place_Pending_Building(56.5f, 50.5f);
            check(!placing_, "放置后退出放置模式");
        }
        std::printf("     生产 %s 完成\n", pick.c_str());
        On_Key_Down('Q', false, false);
    }

    // 采矿：LoadRate*3 / DumpRate*900 / Riparius Value（gamemd 0x73D515 / 0x73E361）
    {
        world_.Spawn("GAREFN", MapObjectKind::Building, world_.Player_House(),
                     60.5f, 60.5f);
        const int hid = world_.Spawn("CMIN", MapObjectKind::Unit, world_.Player_House(),
                                     62.5f, 60.5f);
        Object* hv = world_.Find(hid);
        int ore_idx = -1;
        for (int i = 0; i < sprites_.Models().Overlay_Count(); ++i) {
            if (sprites_.Models().Overlay_Is_Ore(i)) {
                ore_idx = i;
                break;
            }
        }
        if (ore_idx >= 0 && hv != nullptr && hv->harvester && hv->storage > 0) {
            map_.Set_Overlay_At(62, 60, static_cast<uint8_t>(ore_idx));
            map_.Set_Overlay_Data_At(62, 60, 5);
            hv->x = 62.5f;
            hv->y = 60.5f;
            hv->cargo = 0;
            hv->harvest_timer = 0;
            hv->mission = Mission::Harvest;
            const int load_iv = sprites_.Models().Harvester_Load_Interval_Frames();
            for (int i = 0; i < load_iv + 2; ++i) {
                world_.Update(kLogicDt);
            }
            check(hv->cargo >= 1, "LoadRate 间隔后挖到矿");
            // 装满后卸货
            hv->cargo = hv->storage;
            hv->harvest_timer = 0;
            hv->x = 60.5f;
            hv->y = 60.5f;
            const int c0 = credits_;
            const int dump_iv = sprites_.Models().Harvester_Dump_Interval_Frames();
            for (int i = 0; i < dump_iv + 2; ++i) {
                world_.Update(kLogicDt);
                credits_ += world_.Take_Credit_Delta();
            }
            check(credits_ >= c0 + sprites_.Models().Ore_Bail_Value(),
                  "DumpRate 间隔后入账 Riparius Value");
            std::printf("     采矿 Load=%d Dump=%d Value=%d\n", load_iv, dump_iv,
                        sprites_.Models().Ore_Bail_Value());
        } else {
            std::printf("     （无 Tiberium overlay / CMIN，跳过采矿）\n");
        }
    }

    // 变卖：RefundPercent（0x00711F60）
    {
        const int bid = world_.Spawn("GAPOWR", MapObjectKind::Building,
                                     world_.Player_House(), 70.5f, 70.5f);
        Object* b = world_.Find(bid);
        const int c0 = credits_;
        cursor_mode_ = 2;
        check(Try_Repair_Or_Sell(70.5f, 70.5f), "变卖光标命中建筑");
        check(credits_ > c0, "变卖按 RefundPercent 退款");
        check(b == nullptr || b->hp <= 0, "变卖后建筑血量归零");
        const UnitModel* um = sprites_.Models().Resolve("GAPOWR");
        if (um != nullptr) {
            const int expect = static_cast<int>(
                static_cast<double>(um->cost) * sprites_.Models().Refund_Percent() + 0.5);
            check(credits_ - c0 == expect, "退款金额 = Cost * RefundPercent");
        }
        std::printf("     变卖后退款 $%d\n", credits_ - c0);
        cursor_mode_ = 0;
    }

    std::printf("== 败北 / 结局 ==\n");
    {
        // ShortGame：清空所有非玩家房屋建筑+BaseUnit → 只剩玩家 → Flag_To_Win
        int wiped = 0;
        for (Object& o : world_.Objects()) {
            if (o.house != world_.Player_House() && o.Is_Techno() && o.hp > 0) {
                const std::string& hn =
                    (o.house >= 0 &&
                     static_cast<size_t>(o.house) < world_.Houses().size())
                        ? world_.Houses()[static_cast<size_t>(o.house)]
                        : "";
                if (hn == "Neutral" || hn == "Special") {
                    continue;
                }
                o.hp = 0;
                ++wiped;
            }
        }
        check(wiped > 0, "清空敌方科技单位");
        for (int i = 0; i < kLogicFps * 2; ++i) {
            world_.Update(kLogicDt);
            if (world_.Outcome() != MatchOutcome::Playing) {
                break;
            }
        }
        check(world_.Outcome() == MatchOutcome::Won, "敌方清空后 Flag_To_Win");
        std::printf("     清空 %d 单位，Outcome=%d（1=Won）\n", wiped,
                    static_cast<int>(world_.Outcome()));
    }

    std::printf("== 触发器 Global ==\n");
    {
        // TAction 28/29 + TEvent 27/28：Scenario 全局变量 @0x689670 / 0x689760
        MapTrigger trig;
        trig.id = "test";
        trig.house = world_.Houses().empty() ? "" : world_.Houses()[0];
        MapTriggerEvent ev;
        ev.type = 27;
        ev.kind = 2;
        ev.param = 3;
        check(!world_.Event_Has_Occurred(trig, ev), "Global[3] 初值未置");
        world_.Dispatch_TAction(28, world_.Player_House(), 3);
        check(world_.Event_Has_Occurred(trig, ev), "TAction28 后 Global[3] 为真");
        ev.type = 28;
        check(!world_.Event_Has_Occurred(trig, ev), "type28 取反为假");
        world_.Dispatch_TAction(29, world_.Player_House(), 3);
        ev.type = 27;
        check(!world_.Event_Has_Occurred(trig, ev), "TAction29 清 Global[3]");
        // TAction 56/57 + TEvent 36/37：Local @0x689910 / 0x689a00
        ev.type = 36;
        ev.param = 5;
        check(!world_.Event_Has_Occurred(trig, ev), "Local[5] 初值未置");
        world_.Dispatch_TAction(56, world_.Player_House(), 5);
        check(world_.Event_Has_Occurred(trig, ev), "TAction56 后 Local[5] 为真");
        ev.type = 37;
        check(!world_.Event_Has_Occurred(trig, ev), "type37 取反为假");
        world_.Dispatch_TAction(57, world_.Player_House(), 5);
        ev.type = 36;
        check(!world_.Event_Has_Occurred(trig, ev), "TAction57 清 Local[5]");
        // TAction 4/7：Create Team / Reinforce（0x6F09C0 / 0x65D8E0）
        {
            MapTeamType tt;
            tt.id = "SELFTEST_TEAM";
            tt.name = "Selftest Squad";
            tt.house = world_.Houses().empty()
                           ? ""
                           : world_.Houses()[static_cast<size_t>(
                                 world_.Player_House() >= 0 ? world_.Player_House()
                                                           : 0)];
            tt.waypoint = 0;
            MapTaskForceEntry e1;
            e1.count = 2;
            e1.type = "E1";
            MapTaskForceEntry e2;
            e2.count = 1;
            e2.type = "MTNK";
            tt.members.push_back(e1);
            tt.members.push_back(e2);
            world_.Add_Team_Type(tt);
            const size_t before = world_.Objects().size();
            check(world_.Dispatch_TAction(4, world_.Player_House(), 0, nullptr,
                                          "SELFTEST_TEAM"),
                  "TAction4 CreateTeam 认领");
            check(world_.Objects().size() > before, "TAction4 投放 TaskForce");
            const size_t mid = world_.Objects().size();
            check(world_.Dispatch_TAction(7, world_.Player_House(), 0, nullptr,
                                          "SELFTEST_TEAM"),
                  "TAction7 Reinforce 认领");
            check(world_.Objects().size() > mid, "TAction7 再投放 TaskForce");
            std::printf("     TAction4/7 TeamType 投放 %zu -> %zu\n", before,
                        world_.Objects().size());
        }
        // TAction 37/38 同盟位 @0x4F9B70 / 0x4F9F90（House+0x5788）
        {
            int enemy_h = -1;
            for (size_t i = 0; i < world_.Houses().size(); ++i) {
                if (static_cast<int>(i) != world_.Player_House() &&
                    world_.Houses()[i] != "Neutral" &&
                    world_.Houses()[i] != "Special") {
                    enemy_h = static_cast<int>(i);
                    break;
                }
            }
            if (enemy_h >= 0) {
                const int ph = world_.Player_House();
                world_.Dispatch_TAction(37, ph, enemy_h);
                check(world_.Is_Ally(ph, enemy_h), "TAction37 结盟");
                check(world_.Is_Ally(enemy_h, ph), "TAction37 互盟");
                world_.Dispatch_TAction(38, ph, enemy_h);
                check(!world_.Is_Ally(ph, enemy_h), "TAction38 解盟");
            }
        }
        // TEvent 30/58 电力；TAction 27 任务计时 + TEvent 14
        {
            MapTrigger ptrig;
            MapTriggerEvent pev;
            pev.type = 30;
            check(world_.Event_Has_Occurred(ptrig, pev), "电力充足 TEvent30");
            pev.type = 58;
            check(!world_.Event_Has_Occurred(ptrig, pev), "非低电 TEvent58");
            world_.Dispatch_TAction(27, world_.Player_House(), 1);
            check(world_.Mission_Timer_Start() >= 0, "TAction27 启动任务计时");
            pev.type = 14;
            check(!world_.Event_Has_Occurred(ptrig, pev), "计时未满 TEvent14");
            for (int i = 0; i < 20; ++i) {
                world_.Update(kLogicDt);
            }
            check(world_.Event_Has_Occurred(ptrig, pev), "1 秒后 TEvent14");
            world_.Dispatch_TAction(24, world_.Player_House(), 0);
            check(world_.Mission_Timer_Start() < 0, "TAction24 停表");
        }
        // TAction 53/54 Enable/Disable（Trigger+0x44 @0x7268F0 / 0x726900）
        {
            const char* tid = nullptr;
            for (const MapTrigger& t : world_.Triggers()) {
                if (!t.id.empty()) {
                    tid = t.id.c_str();
                    break;
                }
            }
            check(tid != nullptr, "地图含触发器");
            if (tid != nullptr) {
                check(world_.Dispatch_TAction(54, 0, 0, nullptr, tid),
                      "TAction54 Disable 认领");
                bool off = false;
                for (const MapTrigger& t : world_.Triggers()) {
                    if (t.id == tid) {
                        off = !t.enabled;
                        break;
                    }
                }
                check(off, "TAction54 后 +0x44=0");
                check(world_.Dispatch_TAction(53, 0, 0, nullptr, tid),
                      "TAction53 Enable 认领");
                bool on = false;
                bool unlatched = false;
                for (const MapTrigger& t : world_.Triggers()) {
                    if (t.id == tid) {
                        on = t.enabled;
                        unlatched = !t.fired;
                        break;
                    }
                }
                check(on, "TAction53 后 +0x44=1");
                check(unlatched, "TAction53 清 +0x30 闩");
            }
        }
    }

    std::printf("== 渲染 ==\n");
    world_.Select_None();
    // 雷达点击 → Center_On_Cell（Radar_Inner 线性映射）
    {
        int rx = 0, ry = 0, rw = 0, rh = 0;
        if (Radar_Inner(&rx, &ry, &rw, &rh) && rw > 0 && rh > 0) {
            const float cam0x = camera_.X();
            const float cam0y = camera_.Y();
            On_Mouse_Down(0, rx + rw / 2, ry + rh / 2, false);
            check(camera_.X() != cam0x || camera_.Y() != cam0y ||
                      map_.Iso_Width() <= 1,
                  "雷达中心点击会移动镜头");
        }
    }
    // 胜负条：强制 MessageList 文案 + FULLFNT3（若已载入）
    if (world_.Outcome() == MatchOutcome::Won) {
        if (outcome_msg_.empty()) {
            outcome_msg_ = csf_en_.Get("TXT_SCENARIO_WON");
            if (outcome_msg_.empty()) {
                outcome_msg_ = csf_.Get("TXT_SCENARIO_WON");
            }
        }
        check(!outcome_msg_.empty(), "胜负 MessageList 文案非空");
        if (ui_fullfnt_.ok()) {
            check(ui_fullfnt_.frames >= 127, "FULLFNT3 覆盖 ASCII 帧");
        }
    }
    Render();
    check(objects_drawn_ > 0, "这一帧画出了对象");
    std::printf("     画出 %d 个对象\n", objects_drawn_);
    check(!csf_.Get("TXT_SCENARIO_WON").empty(), "CSF TXT_SCENARIO_WON 可读");
    check(!csf_.Get("TXT_SCENARIO_LOST").empty(), "CSF TXT_SCENARIO_LOST 可读");

    // TAction 113（Crowd Cheer）扫描验证：选 player 家，TAction 113
    // 必须命中 ≥0 个 Techno（无就 0，否则 ≥1），且 sound_play_count_ 增加。
    {
        const int house = world_.Player_House();
        const int cheer_before = world_.Crowd_Cheer(house);
        const int snd_before = world_.Sound_Play_Count();
        world_.Dispatch_TAction(113, house, 0, "_Voc_Cheer", nullptr);
        const int snd_after = world_.Sound_Play_Count();
        check(snd_after >= snd_before + 1, "TAction 113 触发一次 sound_play");
        std::printf("     TAction 113 cheer 扫描到 %d 个 Techno\n", cheer_before);
    }

    // Real audio path：拿一个确知存在的 AUD（NSWEEP.AUD 在 ra2.mix 里），
    // 走 Read_Deep_By_ID → Aud_To_Wav → PlaySound 全链路。
    {
        // 多试几个名字：raw AUD / Format80 chunked / .wav 后缀 / 不同名
        std::vector<uint8_t> raw;
        const char* names[] = {"NSWEEP.AUD", "NSWEEP.WAV", "BESTBOX.AUD",
                               "GSWEEP.AUD", "INTRO.AUD"};
        const char* got_name = nullptr;
        for (const char* n : names) {
            for (MixFileClass* m : roots_) {
                raw = m->Read_Deep_By_ID(MixFileClass::CRC_Of(n));
                if (!raw.empty()) { got_name = n; break; }
            }
            if (!raw.empty()) break;
        }
        check(!raw.empty(), "AUD 链路：MIX 拿得到 AUD（任一名）");
        if (!raw.empty()) {
            std::printf("     %s raw size=%zu, first 16 bytes: ", got_name, raw.size());
            for (size_t i = 0; i < 16 && i < raw.size(); ++i) {
                std::printf("%02X ", raw[i]);
            }
            std::printf("\n");
            // Aud_To_Wav 内部做 Format80 解 + raw AUD 头解析 + 裸 PCM 兜底。
            std::vector<uint8_t> wav = Aud_To_Wav(raw.data(), raw.size());
            check(!wav.empty(), "AUD 链路：Format80/裸 PCM 解出非空 WAV");
            check(wav.size() >= 44 + 1000, "AUD 链路：WAV 至少有 1KB PCM");
            check(std::memcmp(wav.data(), "RIFF", 4) == 0 &&
                  std::memcmp(wav.data() + 8, "WAVE", 4) == 0 &&
                  std::memcmp(wav.data() + 12, "fmt ", 4) == 0 &&
                  std::memcmp(wav.data() + 36, "data", 4) == 0,
                  "AUD 链路：WAV RIFF/WAVE/fmt/data 标识正确");
            // 走一遍真播放（异步、不阻塞）。
            Play_Wav_Memory(wav, got_name);
            std::printf("     AUD 链路：%s → WAV %zu 字节\n", got_name, wav.size());
        }
    }

    // Save/Load 链路：把当前 World 写到 build/_selftest.sav 再读回，
    // 验魔数 + 版本 + map_path 复原 + player_credits 复原。
    {
        const std::string sav_path = "build/_selftest.sav";
        const int cr_before = world_.Player_Credits();
        check(Save_World(world_, current_map_path_, sav_path),
              "Save/Load 链路：Save_World 写文件成功");
        std::ifstream is(sav_path, std::ios::binary);
        check(is.good(), "Save/Load 链路：写出来的文件能再打开");
        if (is) {
            uint32_t mlo = 0, mhi = 0, ver = 0;
            is.read(reinterpret_cast<char*>(&mlo), 4);
            is.read(reinterpret_cast<char*>(&mhi), 4);
            is.read(reinterpret_cast<char*>(&ver), 4);
            const uint64_t magic = (static_cast<uint64_t>(mhi) << 32) | mlo;
            // 'R','A','2','S','A','V','E',0 LE → 0x0045564153324152ULL
            check(magic == 0x0045564153324152ULL,
                  "Save/Load 链路：magic = RA2SAVE");
            check(ver == 1, "Save/Load 链路：version = 1");
        }
        // 改 Player_Credits 验"读回"覆盖。
        world_.Set_Player_Credits(cr_before + 999);
        std::string mp;
        check(Load_World(&world_, &mp, sav_path),
              "Save/Load 链路：Load_World 读文件成功");
        check(!mp.empty(), "Save/Load 链路：map_path 复原非空");
        check(world_.Player_Credits() == cr_before,
              "Save/Load 链路：player_credits 复原到 save 时的值");
        std::printf("     Save/Load：mp=\"%s\" cr=%d\n",
                    mp.c_str(), world_.Player_Credits());
    }

    // [Base] 段解析：电脑 AI 蓝图节点。Arena 这种剧情图无 [Base]，
    // 这里只验证"解析器就位"——空集合法，找到非空条也合法。
    {
        const auto& nodes = world_.Base_Nodes();
        // 不强制要求非空：剧情图/教程图没 [Base] 是正常现象。
        std::printf("     [Base] 段：%zu 条\n", nodes.size());
        if (!nodes.empty()) {
            int with_b = 0;
            for (const auto& n : nodes) {
                if (!n.building.empty()) ++with_b;
            }
            check(with_b > 0, "[Base] 段：非空时至少一条 Building 名非空");
        }
    }

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
