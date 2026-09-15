// Subsystems.cpp -- Subsystems.h 的实现
//
// 目前这一层的实现是"语义正确但简化"的：
// 类关系、调用时机、数据流向都按逆向下来的结构搭好，
// 具体算法（真实伤害公式、locomotor 的加速曲线、脚本指令全集等）
// 还需要从对应函数里逐条还原，相关位置都标了原始 VA 以便对照。

#include "core/Subsystems.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ai/PathFinder.h"

namespace ra2 {

// ---------------------------------------------------------------------------
// Locomotor
// ---------------------------------------------------------------------------
// TODO(逆向)：11 个 Locomotor 各自的 Move_To 需要从各自的 vftable 入口还原。
//   它们的虚表都在 tools/analyze.py 定位到的 231 张候选里，
//   按类名匹配的做法见 docs/multicore-plan.md §4.1。
namespace {

bool Move_Toward(CoordStruct& cur, CoordStruct target, int32_t speed) {
    const int32_t dx = target.X - cur.X;
    const int32_t dy = target.Y - cur.Y;
    const int32_t adx = dx < 0 ? -dx : dx;
    const int32_t ady = dy < 0 ? -dy : dy;
    if (adx <= speed && ady <= speed) {
        cur = target;
        return true;
    }
    cur.X += (dx > 0 ? speed : -speed) * (adx > speed ? 1 : 0);
    cur.Y += (dy > 0 ? speed : -speed) * (ady > speed ? 1 : 0);
    return false;
}

}  // namespace

#define RA2_LOCOMOTOR_BODY(NS, SPEED)                            \
    bool NS::Move_To(CoordStruct target) {                       \
        speed_ = (SPEED);                                        \
        moving_ = true;                                          \
        const bool done = Move_Toward(pos_, target, speed_);     \
        if (done) { moving_ = false; }                           \
        return done;                                             \
    }                                                            \
    void NS::Stop_Moving() { moving_ = false; }

RA2_LOCOMOTOR_BODY(DriveLocomotionClass, 8)
RA2_LOCOMOTOR_BODY(WalkLocomotionClass, 3)
RA2_LOCOMOTOR_BODY(FlyLocomotionClass, 16)
RA2_LOCOMOTOR_BODY(JumpjetLocomotionClass, 12)
RA2_LOCOMOTOR_BODY(HoverLocomotionClass, 10)
RA2_LOCOMOTOR_BODY(ShipLocomotionClass, 6)
RA2_LOCOMOTOR_BODY(MechLocomotionClass, 5)
RA2_LOCOMOTOR_BODY(RocketLocomotionClass, 20)
RA2_LOCOMOTOR_BODY(TeleportLocomotionClass, 256)
RA2_LOCOMOTOR_BODY(TunnelLocomotionClass, 8)
RA2_LOCOMOTOR_BODY(DropPodLocomotionClass, 24)

#undef RA2_LOCOMOTOR_BODY

// ---------------------------------------------------------------------------
// 战斗
// ---------------------------------------------------------------------------
void BulletClass::Update() {
    // TODO(逆向)：真实弹道在 BulletClass 的 vftable 里，
    //   还包含抛物线/直线/追踪/激光等多种形态（DiskLaserClass / LaserDrawClass /
    //   EMPulseClass 等 RTTI 类各自对应一种）。
    //   多核注意：这里的"推进"只读世界，可按区间并行；
    //   "命中结算"要写目标血量，必须收集起来串行应用。
    const int32_t dx = 0;  // 目标差值由 Target 反查
    (void)dx;
}

// ---------------------------------------------------------------------------
// AI 小队
// ---------------------------------------------------------------------------
void TeamClass::Update() {
    if (Script.empty() || Members.empty()) {
        return;
    }
    // TODO(逆向)：ScriptType 的指令全集（移动/攻击/集结/跳转/解队……）
    //   需要从 ScriptClass 的实现还原；二进制里有 "ScriptType" 与 "Script" 两个 INI 键，
    //   指令名应在 rulesmd.ini / aimd.ini 的解析函数附近。
    if (ScriptStep < static_cast<int32_t>(Script.size())) {
        const ScriptAction& a = Script[static_cast<size_t>(ScriptStep)];
        (void)a;
        // 简化：每帧只推进一小步，真实实现要等到该动作完成才前进。
        ++ScriptStep;
    }
}

// ---------------------------------------------------------------------------
// INI
// ---------------------------------------------------------------------------
namespace {

std::string Trim(std::string s) {
    const char* ws = " \t\r\n";
    const size_t b = s.find_first_not_of(ws);
    if (b == std::string::npos) {
        return {};
    }
    const size_t e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

}  // namespace

bool INIClass::Load(const std::vector<uint8_t>& data) {
    sections_.clear();
    std::string cur;
    std::string text(reinterpret_cast<const char*>(data.data()), data.size());
    size_t pos = 0;
    while (pos <= text.size()) {
        const size_t nl = text.find('\n', pos);
        std::string line = Trim(text.substr(pos, (nl == std::string::npos ? text.size() : nl) - pos));
        if (nl == std::string::npos) {
            pos = text.size() + 1;
        } else {
            pos = nl + 1;
        }
        if (line.empty() || line[0] == ';' || line[0] == '#') {
            continue;  // 注释
        }
        if (line.front() == '[' && line.back() == ']') {
            cur = Trim(line.substr(1, line.size() - 2));
            sections_.emplace_back(cur, std::vector<IniEntry>{});
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos || sections_.empty()) {
            continue;
        }
        // 去掉行尾注释（Westwood 的 ini 用 ';' 作行内注释）
        std::string value = Trim(line.substr(eq + 1));
        const size_t sc = value.find(';');
        if (sc != std::string::npos) {
            value = Trim(value.substr(0, sc));
        }
        sections_.back().second.push_back(IniEntry{Trim(line.substr(0, eq)), value});
    }
    return !sections_.empty();
}

bool INIClass::Load_File(const char* path) {
    FILE* f = std::fopen(path, "rb");
    if (f == nullptr) {
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> buf(static_cast<size_t>(n > 0 ? n : 0));
    if (!buf.empty()) {
        std::fread(buf.data(), 1, buf.size(), f);
    }
    std::fclose(f);
    return Load(buf);
}

const char* INIClass::Get_String(const char* section, const char* key,
                                 const char* def) const {
    for (const auto& s : sections_) {
        if (s.first != section) {
            continue;
        }
        for (const auto& e : s.second) {
            if (e.key == key) {
                return e.value.c_str();
            }
        }
    }
    return def;
}

int32_t INIClass::Get_Int(const char* section, const char* key, int32_t def) const {
    const char* v = Get_String(section, key, nullptr);
    if (v == nullptr) {
        return def;
    }
    return static_cast<int32_t>(std::strtol(v, nullptr, 10));
}

bool INIClass::Get_Bool(const char* section, const char* key, bool def) const {
    const char* v = Get_String(section, key, nullptr);
    if (v == nullptr) {
        return def;
    }
    return (v[0] == 'y' || v[0] == 'Y' || v[0] == 't' || v[0] == 'T' || v[0] == '1');
}

std::vector<IniEntry> INIClass::Section(const char* name) const {
    for (const auto& s : sections_) {
        if (s.first == name) {
            return s.second;
        }
    }
    return {};
}

// ---------------------------------------------------------------------------
// 界面
// ---------------------------------------------------------------------------
CellStruct TacticalClass::Screen_To_Cell(int x, int y) const {
    // 等距变换：屏幕 (x,y) 反解到格子。
    // View_Origin 是视口左上角的 lepton 坐标。
    const int32_t lx = x + View_Origin.X;
    const int32_t ly = y + View_Origin.Y;
    return CellStruct{static_cast<int16_t>(lx >> kLeptonBits),
                      static_cast<int16_t>(ly >> kLeptonBits)};
}

void TacticalClass::Cell_To_Screen(CellStruct c, int& out_x, int& out_y) const {
    const int32_t lx = (static_cast<int32_t>(c.X) << kLeptonBits) - View_Origin.X;
    const int32_t ly = (static_cast<int32_t>(c.Y) << kLeptonBits) - View_Origin.Y;
    out_x = lx;
    out_y = ly;
}

void RadarClass::Init_Clear() { house_ = -1; }

void RadarClass::Init_For_House(int house_index) { house_ = house_index; }

void RadarClass::Refresh() {
    // 只读遍历地图，天然可并行 —— 见 multicore-plan.md 第 2 档。
    (void)house_;
}

void SidebarClass::Update() {}

// ---------------------------------------------------------------------------
// 网络
// ---------------------------------------------------------------------------
// TODO(逆向)：三个 Interface 的真实实现分别在
//   0x00541820（包泵）、0x00540A80（IPXManagerClass::Init）、
//   0x005F16E0（NullModemClass::Init）。
//   这里只把接口与调用时机落到代码上，socket 细节待还原。
bool UDPInterfaceClass::Init() { return true; }
void UDPInterfaceClass::Close() {}
bool UDPInterfaceClass::Send_Frame(uint32_t, const std::vector<uint8_t>&) { return true; }
int UDPInterfaceClass::Receive(std::vector<uint8_t>&) { return 0; }

bool IPXInterfaceClass::Init() { return false; }  // IPX 在现代系统上不可用
void IPXInterfaceClass::Close() {}
bool IPXInterfaceClass::Send_Frame(uint32_t, const std::vector<uint8_t>&) { return false; }
int IPXInterfaceClass::Receive(std::vector<uint8_t>&) { return 0; }

bool NullModemClass::Init() { return true; }
void NullModemClass::Close() {}
bool NullModemClass::Send_Frame(uint32_t, const std::vector<uint8_t>&) { return true; }
int NullModemClass::Receive(std::vector<uint8_t>&) { return 0; }
bool NullModemClass::Dial_Modem(const char*) { return false; }
bool NullModemClass::Answer_Modem() { return false; }

}  // namespace ra2
