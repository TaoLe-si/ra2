// Subsystems.h -- 其余核心子系统的类骨架
//
// 本文件里的每一个类名都来自 gamemd.exe 的 RTTI TypeDescriptor
// （tools/rtti.py -> db/rtti.json / db/classes.md），不是凭印象起的。
// 与 core/Abstract.h 的分工：那里是"对象实例体系"，这里是"支撑系统"。
//
// 仍然遵守同一条纪律：字段偏移与虚表槽位没有从二进制确认的，一律标 TODO，
// 只用"接口语义 + 类之间的关系"来表达已经确认的部分。

#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "core/Abstract.h"
#include "core/Types.h"

namespace ra2 {

// ===========================================================================
// 1. Locomotor：移动策略（RTTI 确认的 11 个实现 + 3 个接口）
// ===========================================================================
//   LocomotionClass（基类）
//   DriveLocomotionClass   WalkLocomotionClass    FlyLocomotionClass
//   JumpjetLocomotionClass HoverLocomotionClass   ShipLocomotionClass
//   MechLocomotionClass    RocketLocomotionClass  TeleportLocomotionClass
//   TunnelLocomotionClass  DropPodLocomotionClass
//   接口：ILocomotion、IPiggyback、IFlyControl
//
// 这是标准的策略模式：FootClass 把移动委托给一个 Locomotor 对象。
// 对多核的意义：Locomotor 的状态是自包含的，
// 一个单位的移动计算不会直接写别人的状态，适合并行推进。
class LocomotionClass {
public:
    virtual ~LocomotionClass() = default;

    /// 每逻辑帧推进移动。返回是否已抵达终点。
    virtual bool Move_To(CoordStruct target) = 0;
    virtual void Stop_Moving() = 0;

    /// 当前是否在移动中。
    virtual bool Is_Moving() const noexcept { return moving_; }

    /// 当前位置（lepton）。真实实现里位置在 FootClass 上，
    /// 这里放在 Locomotor 上是为了让状态自包含、便于并行推进。
    CoordStruct Position() const noexcept { return pos_; }
    void Set_Position(CoordStruct p) noexcept { pos_ = p; }

protected:
    bool moving_ = false;
    CoordStruct pos_{};
    int32_t speed_ = 8;   ///< lepton / 帧
};

class DriveLocomotionClass : public LocomotionClass {
public:
    bool Move_To(CoordStruct target) override;
    void Stop_Moving() override;
};
class WalkLocomotionClass : public LocomotionClass {
public:
    bool Move_To(CoordStruct target) override;
    void Stop_Moving() override;
};
class FlyLocomotionClass : public LocomotionClass {
public:
    bool Move_To(CoordStruct target) override;
    void Stop_Moving() override;
};
class JumpjetLocomotionClass : public LocomotionClass {
public:
    bool Move_To(CoordStruct target) override;
    void Stop_Moving() override;
};
class HoverLocomotionClass : public LocomotionClass {
public:
    bool Move_To(CoordStruct target) override;
    void Stop_Moving() override;
};
class ShipLocomotionClass : public LocomotionClass {
public:
    bool Move_To(CoordStruct target) override;
    void Stop_Moving() override;
};
class MechLocomotionClass : public LocomotionClass {
public:
    bool Move_To(CoordStruct target) override;
    void Stop_Moving() override;
};
class RocketLocomotionClass : public LocomotionClass {
public:
    bool Move_To(CoordStruct target) override;
    void Stop_Moving() override;
};
class TeleportLocomotionClass : public LocomotionClass {
public:
    bool Move_To(CoordStruct target) override;
    void Stop_Moving() override;
};
class TunnelLocomotionClass : public LocomotionClass {
public:
    bool Move_To(CoordStruct target) override;
    void Stop_Moving() override;
};
class DropPodLocomotionClass : public LocomotionClass {
public:
    bool Move_To(CoordStruct target) override;
    void Stop_Moving() override;
};

// ===========================================================================
// 2. 战斗：武器 / 弹头 / 弹道
// ===========================================================================
// RTTI 确认：WeaponTypeClass、WarheadTypeClass、BulletClass、BulletTypeClass、
//            DiskLaserClass、EMPulseClass、LaserDrawClass、RadSiteClass、
//            IonBlastClass（0x0053A6C0 在 Ion.cpp 区间）、WaveClass、
//            AirstrikeClass、ParasiteClass、VeinholeMonsterClass
// 二进制证据：
//   "WarheadType"（INI 键）、"Warheads"（0x00668BF0）、
//   "EMPulseWarhead"、"*************** Warhead CRCs**************"
//   "SuperWeapon" / "SuperWeaponType"（INI 键）

/// 弹头：决定伤害如何作用。RTTI 确认 WarheadTypeClass。
class WarheadTypeClass : public AbstractTypeClass {
public:
    int32_t Damage = 0;
    /// 对各类护甲的伤害系数表。
    /// TODO(逆向)：护甲种类数量与索引顺序待从 rulesmd.ini 解析处确认。
    float Versus[8] = {1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f};
};

/// 武器：RTTI 确认 WeaponTypeClass。
class WeaponTypeClass : public AbstractTypeClass {
public:
    WarheadTypeClass* Warhead = nullptr;
    int32_t Damage = 0;
    int32_t Range = 0;        ///< lepton
    int32_t ROF = 0;          ///< 冷却帧数
    int32_t Speed = 0;        ///< 弹速
};

/// 飞行中的弹药。RTTI 确认 BulletClass / BulletTypeClass。
class BulletClass : public ObjectClass {
public:
    const AbstractTypeClass* Fetch_Type() const noexcept override { return nullptr; }

    WeaponTypeClass* Weapon = nullptr;
    /// 目标对象 ID。
    int Target = 0;

    /// 每帧推进。多核提示：弹药之间只在命中时写目标，
    /// 因此"推进"可并行，"结算伤害"必须串行（见 multicore-plan.md 第 3 档）。
    void Update() override;
};

/// 超级武器。RTTI 确认：SuperClass、SuperWeaponTypeClass。
class SuperClass {
public:
    /// 充能进度（0..100）。
    int32_t Charge = 0;
    bool Is_Ready() const noexcept { return Charge >= 100; }
    virtual void Fire(CellStruct target) { (void)target; }
};

// ===========================================================================
// 3. 阵营 / 经济
// ===========================================================================
// RTTI 确认：HouseClass、HouseTypeClass、SideClass、FactoryClass（出兵）、
//            PowerClass（电力）、SlaveManagerClass、SpawnManagerClass
// 二进制证据：0x005659F0 附近 "House->Fetch_ID()"；
//            House.CPP 区间 0x4F9B70-0x50AF07（最大函数 0x0050A5C0，710 指令）；
//            Power.CPP 的 0x00640450

/// 阵营。RTTI 确认：HouseClass、HouseTypeClass。
class HouseClass {
public:
    int32_t Index = 0;
    Side Side_ = Side::GDI;

    int32_t Credits = 0;      ///< 资金
    int32_t Power_Output = 0; ///< 发电量
    int32_t Power_Drain = 0;  ///< 耗电量

    bool Is_Powered() const noexcept { return Power_Output >= Power_Drain; }

    /// 单位/建筑工厂。RTTI 确认 FactoryClass。
    std::vector<class FactoryClass*> Factories;

    /// RTTI 确认 TechnoClass 由 DynamicVectorClass<TechnoClass*> 管理，
    /// 这里是对应的阵营视角持有方式。
    std::vector<TechnoClass*> Units;

    int Fetch_ID() const noexcept { return Index; }
};

/// 生产队列。RTTI 确认 FactoryClass。
class FactoryClass {
public:
    int32_t Owner = 0;         ///< HouseClass 索引
    int32_t Producing = -1;    ///< 正在生产的类型索引
    int32_t Progress = 0;
    bool Is_Busy() const noexcept { return Producing >= 0; }
};

// ===========================================================================
// 4. AI：小队 / 脚本 / 触发
// ===========================================================================
// RTTI 确认：TeamClass、TeamTypeClass、TaskForceClass、ScriptClass、
//            ScriptTypeClass、TriggerClass、TriggerTypeClass、TagClass、
//            TagTypeClass、TActionClass、TEventClass、AITriggerTypeClass
// 二进制证据（INI 键）："TeamType"、"TaskForce"、"Script"、"ScriptType"、
//            "Trigger"、"TriggerType"、"AITrigger"、"AITriggerType"
//            "AICapture: Capturer's Team overrides with (%s)."
//
// 这套结构是**数据驱动**的：aimd.ini 里定义 TeamType（小队类型）-> 引用
// TaskForce（兵力构成）与 Script（行动脚本），运行时实例化成 TeamClass。

/// 行动脚本的一条动作。RTTI 确认：ScriptClass、ScriptTypeClass。
struct ScriptAction {
    int32_t Action = 0;   ///< 动作类型（移动到、攻击、集结……）
    int32_t Arg = 0;      ///< 参数，常见是路点编号
};

/// AI 小队。RTTI 确认：TeamClass。
class TeamClass {
public:
    int32_t Owner = 0;                  ///< HouseClass 索引
    int32_t TypeIndex = -1;             ///< TeamTypeClass 索引
    std::vector<FootClass*> Members;    ///< 由 TaskForce 决定构成
    std::vector<ScriptAction> Script;   ///< 由 ScriptType 决定行为
    int32_t ScriptStep = 0;

    /// 推进脚本一步。
    void Update();

    bool Is_Full() const noexcept { return !Members.empty(); }
};

/// 触发器：事件 -> 动作。RTTI 确认：TriggerClass、TEventClass、TActionClass、TagClass。
class TriggerClass {
public:
    int32_t Event = 0;    ///< TEventClass：触发条件
    int32_t Action = 0;   ///< TActionClass：触发后动作
    int32_t Tag = -1;     ///< TagClass：关联的标签（挂到的对象/单元）
    bool Repeat = false;
    bool Fired = false;
};

// ===========================================================================
// 5. 数据驱动：INI
// ===========================================================================
// RTTI 确认：INIClass、CCINIClass（带注释支持的变体）
// 二进制证据：Ini.CPP 区间 0x529160-0x52934D（0x00529160，159 指令）
//
// 整个游戏的数据（单位属性、武器、AI、地图）都从 ini 读，
// 所以这是还原"游戏内容"的必经之路。

/// 一个 INI 段下的键值对。
struct IniEntry {
    std::string key;
    std::string value;
};

/// INI 文件。RTTI 确认：INIClass / CCINIClass。
class INIClass {
public:
    /// 从内存文本加载（真实来源通常是 MixFileSystem 解出的内容）。
    bool Load(const std::vector<uint8_t>& data);
    bool Load_File(const char* path);

    /// 读取 [section] 下的 key，找不到返回 def。
    const char* Get_String(const char* section, const char* key,
                           const char* def = "") const;
    int32_t Get_Int(const char* section, const char* key, int32_t def = 0) const;
    bool Get_Bool(const char* section, const char* key, bool def = false) const;

    /// 枚举某段下的所有键。
    std::vector<IniEntry> Section(const char* name) const;

private:
    std::vector<std::pair<std::string, std::vector<IniEntry>>> sections_;
};

// ===========================================================================
// 6. 界面与表现
// ===========================================================================
// RTTI 确认：SidebarClass、RadarClass、DisplayClass、Tactical、MouseClass、
//            WWMouseClass、PowerClass、TabClass、GadgetClass、ControlClass、
//            GaugeClass、TriColorGaugeClass、ShapeButtonClass、TextLabelClass、
//            DropListClass、SliderClass、ToggleClass、EditClass、ListClass、
//            CheckListClass、ToolTipManager、CCToolTip、ProgressScreenClass
// 二进制证据：
//   0x00652DE0 RadarClass::Init_Clear / 0x00652E90 RadarClass::Init_For_House
//   0x006A9540 Sidebar.CPP（1041 指令）
//   0x006D3D10 Tactical.CPP（1004 指令）
//   Display.CPP 区间 0x4AE4F0-0x4AE6A5

/// 战术地图视图（主战场）。RTTI 确认：Tactical。
class TacticalClass {
public:
    /// 屏幕坐标 -> 格子。
    CellStruct Screen_To_Cell(int x, int y) const;
    /// 格子 -> 屏幕坐标。
    void Cell_To_Screen(CellStruct c, int& out_x, int& out_y) const;

    /// 当前可视区域左上角（lepton）。
    CoordStruct View_Origin;
};

/// 小地图。RTTI 确认：RadarClass。
class RadarClass {
public:
    void Init_Clear();
    void Init_For_House(int house_index);
    /// 重算整张小地图。只读世界状态，可安全并行。
    void Refresh();

private:
    int house_ = -1;
};

/// 侧边栏（建造菜单）。RTTI 确认：SidebarClass。
class SidebarClass {
public:
    void Update();
};

// ===========================================================================
// 7. 网络
// ===========================================================================
// RTTI 确认：UDPInterfaceClass、IPXInterfaceClass、IPXConnClass、
//            IPXGlobalConnClass、IPXManagerClass、NullModemClass、
//            NullModemConnClass、WinModemClass、Dial8Class、
//            ConnectionClass、CommBufferClass、ConnManClass
// 二进制证据：
//   0x00540A80 IPXManagerClass::Init（引用 "**** IPXManagerClass::Init ***"）
//   0x005F16E0 NullModemClass::Init
//   0x005F2950 NullModemClass::Dial_Modem
//   0x005F2CE0 NullModemClass::Answer_Modem
//   0x0048C040 Connect.CPP（"ConnectClass::Receive_Packet" 线索）
//   0x00541820 网络包泵：Multicast / DATA_ACK / RouterChannel
//   "Setting addresses for UDP broadcast"、"Primary UDP Socket init complete"

/// 网络传输抽象。对应上面一整组 *InterfaceClass。
class NetworkInterface {
public:
    virtual ~NetworkInterface() = default;
    virtual bool Init() = 0;
    virtual void Close() = 0;
    /// 发送一帧的输入；返回是否成功。
    virtual bool Send_Frame(uint32_t frame, const std::vector<uint8_t>& payload) = 0;
    /// 尽量收包，追加到 out。
    virtual int Receive(std::vector<uint8_t>& out) = 0;
};

class UDPInterfaceClass : public NetworkInterface {
public:
    bool Init() override;
    void Close() override;
    bool Send_Frame(uint32_t frame, const std::vector<uint8_t>& payload) override;
    int Receive(std::vector<uint8_t>& out) override;
};

class IPXInterfaceClass : public NetworkInterface {
public:
    bool Init() override;
    void Close() override;
    bool Send_Frame(uint32_t frame, const std::vector<uint8_t>& payload) override;
    int Receive(std::vector<uint8_t>& out) override;
};

/// 串口/调制解调器直连（老式对战方式）。RTTI 确认 NullModemClass。
class NullModemClass : public NetworkInterface {
public:
    bool Init() override;      ///< 0x005F16E0
    void Close() override;
    bool Send_Frame(uint32_t frame, const std::vector<uint8_t>& payload) override;
    int Receive(std::vector<uint8_t>& out) override;

    bool Dial_Modem(const char* number);    ///< 0x005F2950
    bool Answer_Modem();                    ///< 0x005F2CE0
};

}  // namespace ra2
