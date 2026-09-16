// Types.h -- 引擎基础类型
//
// 还原依据：
//   * 类名来自 gamemd.exe 的 MSVC RTTI TypeDescriptor（tools/rtti.py 自动提取，
//     db/rtti.json 共 988 条，其中非模板类 328 个），命名不是猜测。
//   * 结构体的字段偏移**尚未**从二进制确认，凡未确认之处一律标注 TODO，
//     并给出验证方法，避免把臆测写成事实。
//
// 原始构建信息：
//   镜像   D:\westwood\RA2YR\gamemd.exe
//   ImageBase 0x00400000（重定位表已剥离，静态地址即运行时地址）
//   入口   0x007CD80F    链接时间戳 0x3BDF544E（2001-10-31）
//   内部名 Sun.exe       符号文件 tsun.dbg       FileVersion 1.11

#pragma once

#include <cstdint>

namespace ra2 {

// ---------------------------------------------------------------------------
// 坐标系
// ---------------------------------------------------------------------------
// RA2/YR 是等距（isometric）瓦片地图：
//   * 一个 Cell 是一个菱形格子，逻辑尺寸为 256x256 "lepton"。
//   * Lepton 是引擎内部最小的位置单位，1 Cell = 256 Lepton。
//   * CellStruct 是格子坐标（列 X / 行 Y）。
//   * CoordStruct 是连续坐标，按 lepton 计。
//
// TODO(逆向)：CellClass 的字段布局需要从 MapClass::Cell_At / Get_Cell 相关函数
//   反推。已定位的入口：寻路失败日志所在的 0x0042C900
//   （同时引用 "Regular findpath failure" 与 "Hierarchical findpath failure"）。
//   确认方法：在该函数下断点，观察传入的 cell 指针与数组步长。

struct CellStruct {
    int16_t X = 0;
    int16_t Y = 0;

    friend bool operator==(const CellStruct& a, const CellStruct& b) noexcept {
        return a.X == b.X && a.Y == b.Y;
    }
    friend bool operator!=(const CellStruct& a, const CellStruct& b) noexcept {
        return !(a == b);
    }
};

/// 等距地图下一个格子的 lepton 数（2 ^ kLeptonBits）。
inline constexpr int kLeptonPerCell = 256;
inline constexpr int kLeptonBits = 8;

struct CoordStruct {
    int32_t X = 0;  ///< lepton，X 沿屏幕右下方向
    int32_t Y = 0;  ///< lepton，Y 沿屏幕左下方向
    int32_t Z = 0;  ///< 高度（飞行单位用）

    CellStruct Cell() const noexcept {
        return CellStruct{static_cast<int16_t>(X >> kLeptonBits),
                          static_cast<int16_t>(Y >> kLeptonBits)};
    }
};

/// 由格子坐标构造格心坐标。
inline CoordStruct CellCenter(CellStruct c) noexcept {
    return CoordStruct{(static_cast<int32_t>(c.X) << kLeptonBits) + kLeptonPerCell / 2,
                       (static_cast<int32_t>(c.Y) << kLeptonBits) + kLeptonPerCell / 2,
                       0};
}

/// 切比雪夫距离足够用于等距格子的粗筛；精确距离走 PathFinder。
inline int CellDistance(CellStruct a, CellStruct b) noexcept {
    const int dx = a.X - b.X;
    const int dy = a.Y - b.Y;
    const int ax = dx < 0 ? -dx : dx;
    const int ay = dy < 0 ? -dy : dy;
    return ax > ay ? ax : ay;
}

// ---------------------------------------------------------------------------
// 全局枚举：均取自二进制内嵌字符串（db/strings.json），非臆测
// ---------------------------------------------------------------------------

/// 阵营侧（rulesmd.ini [Sides]：GDI 第一、Nod 第二、ThirdSide 第三）。
/// RadarClass::Init_For_House @0x00652E90 用 Side==0 走盟军雷达内缩。
enum class Side : int32_t {
    GDI = 0,      ///< 盟军（[Sides] 第一）
    Nod = 1,      ///< 苏联
    Yuri = 2,     ///< ThirdSide / 尤里
    Civilian = 3,
    Mutant = 4,
};

/// 逻辑帧率。RA2/YR 的逻辑是定步长推进的，这是锁步网络模型的基础。
/// 二进制证据：Queue.CPP 的 0x006475F0 引用 "Processing Ticks:%03d Frames:%03d"
/// 与 "Frame %d, my sent = %d"，说明帧（Frame）与网络发送是同一节拍。
inline constexpr int kLogicFPS = 15;
inline constexpr int kFrameMS = 1000 / kLogicFPS;

/// RTTI 已确认存在的一部分核心类（完整清单见 db/classes.md）。
/// 这里只列出继承骨架上已被 TypeDescriptor 证实存在的类型。
///
///   AbstractClass -> ObjectClass -> TechnoClass  -> FootClass -> UnitClass
///                                               ->           -> InfantryClass
///                                               ->           -> AircraftClass
///                                               -> BuildingClass
///                              -> TerrainClass / OverlayClass / SmudgeClass
///
/// TODO(逆向)：确认每个类的虚表槽位顺序。已自动定位 231 张候选虚表
///   （db/vtables.json），但本二进制没有 CompleteObjectLocator，
///   无法用 RTTI 把虚表和类名一一对应；需要按构造函数里写入虚表指针的顺序来匹配。

}  // namespace ra2
