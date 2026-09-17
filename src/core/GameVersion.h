// GameVersion.h -- 原版 RA2 与《尤里的复仇》的版本区分。
//
// 为什么必须有这一层：这个安装目录里**两个版本并存**，而且它们是"打底 + 覆盖"
// 的关系，不是两套互不相干的文件：
//
//   原版 RA2   game.exe      ra2.mix(281MB)                 RA2.INI     地图 *.mmx
//   尤里的复仇 gamemd.exe    ra2.mix 打底 + ra2md.mix(204MB) RA2MD.INI   地图 *.yro
//
// 也就是说 **ra2md.mix 不是"另一份素材"，是覆盖在 ra2.mix 之上的一层**。
// 早先我图省事把两个 mix 一起挂上（--addmix），语义上是错的：
// 原版 RA2 根本不存在 ra2md.mix，那样挂出来的单位表是 YR 的。
//
// 挂载顺序（后挂的覆盖先挂的，跟游戏一致）：
//   RA2 : ra2.mix
//   YR  : ra2.mix -> ra2md.mix -> expandmd01..99.mix（按号升序，**编号循环**）
//         -> thememd.mix
//
// 地图归档**另有一套命名**，别和上面混：
//   RA2 : MAPS%02d.MIX      通配 MAPS*.MIX
//   YR  : MAPSMD%02d.MIX    通配 MAPSMD*.MIX
// 证据同源，都在 gamemd.exe .data 的归档名表里：
//   0x41C2C4 "MAPS%02d.MIX"   0x426790 "MAPS*.MIX"
//   0x41C2EC "MAPSMD%02d.MIX" 0x42679C "MAPSMD*.MIX"
// "按号试 + 通配扫"两条路并存 —— 只认一个固定名字必然漏。
//
// 扩展包是编号循环而不是固定的 expandmd01：gamemd.exe .data 里的归档名表就是
// "EXPANDMD%02d.MIX"（见 GameVersion.cpp 注释）。实测一份 Reunion 2023 安装把
// RULESMD.INI/ARTMD.INI 放在 expandmd01、RULES.INI/Art.ini 放在 expandmd97，
// 只挂 01 会丢一半规则。
//
// 二进制证据：
//   * 两个 exe 各带一份体素法线表（VoxLib 内置，VXL 文件里没有）：
//       game.exe   .data off=0x003F9FA8  361 项
//       gamemd.exe .data off=0x00446A08  361 项
//     **实测 361 项逐字节相同**（0 处不同）—— 两版本共用同一张法线表，
//     同时也说明扫描到的不是随机浮点巧合。见 db/voxel-normals.txt。
//   * 反过来，素材是有版本差异的：RULESMD.INI 只在 ra2md.mix 里，
//     RULES.INI 只在 ra2.mix 里（实测 extract_ini.txt）。

#pragma once

#include <string>
#include <vector>

namespace ra2 {

enum class GameVersion : int {
    kRA2 = 0,   ///< 原版《命令与征服：红色警戒 2》
    kYuri = 1,  ///< 资料片《尤里的复仇》
};

const char* Version_Name(GameVersion v);

/// 一个版本在游戏目录里的完整布局。
struct GamePaths {
    GameVersion version = GameVersion::kRA2;
    std::string dir;                    ///< 游戏根目录
    std::string exe;                    ///< 主程序（game.exe / gamemd.exe）
    std::string launcher;               ///< 启动器（ra2.exe / RA2MD.EXE）
    std::string ini;                    ///< RA2.INI / RA2MD.INI
    std::string map_ext;                ///< 多人地图后缀：.mmx / .yro
    std::vector<std::string> mixes;     ///< 按挂载顺序，后挂的覆盖先挂的
    /// 地图归档（MAPS%02d.MIX / MAPSMD%02d.MIX）。
    /// 和 mixes 一样是**编号循环**，外加一次通配扫描兜住编号外的命名。
    std::vector<std::string> map_mixes;
    /// 松散地图目录：直接躺在磁盘上的 *.map（INI 文本，非归档）。
    ///
    /// 注意这条**不是**从 exe 推出来的：归档名表里既没有 `*.MAP` 通配串，
    /// 也没有 "Maps" 目录名串，所以"引擎自己扫这几个目录"尚未证实。
    /// 这里记的是**本装的实际布局**（实测 540 张 .map 分在 5 个子目录里），
    /// 谁在扫（引擎 / CNCNet 启动器）留到 P3 读 MapSelect 的反汇编再定。
    std::vector<std::string> map_dirs;
};

/// 在一个游戏目录里识别并组装出指定版本的布局。
class GameInstall {
public:
    /// 扫描 dir，看装了哪些版本。返回能用的版本列表。
    static std::vector<GameVersion> Detect_Installed(const char* dir);

    /// 组装指定版本的路径。缺文件会把缺的列在 missing 里并返回 false，
    /// 但只要核心的 exe + ra2.mix 在就认为可用（lang/movies 缺了不影响玩）。
    static bool Resolve(const char* dir, GameVersion v, GamePaths* out,
                        std::string* missing = nullptr);

    /// 给定目录猜一个"最像"的版本：有 ra2md.mix 就是 YR，否则 RA2。
    static GameVersion Guess(const char* dir);

    /// 把这个版本的挂载顺序打印出来（--detect 用）。
    static void Dump(const GamePaths& p);

    /// 在 map_dirs 里找第一张松散地图（.map / .mmx / .yro），找不到返回空串。
    ///
    /// 为什么放在这里：本装的地图全在 Maps/ 下、root 里一个地图归档都没有，
    /// 于是"挑一张默认图"成了每个入口都要做的事。写两遍必然漂移，收成一处。
    static std::string Find_First_Map(const GamePaths& p);
};

}  // namespace ra2
