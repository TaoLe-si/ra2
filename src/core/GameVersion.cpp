// GameVersion.cpp -- 见 GameVersion.h。

#include "core/GameVersion.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <io.h>

namespace ra2 {
namespace {

// Windows.h 太重，这里只需要判断文件在不在，直接试开就行。
bool File_Exists(const std::string& p) {
    FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) {
        return false;
    }
    std::fclose(f);
    return true;
}

std::string Join(const std::string& dir, const char* name) {
    if (dir.empty()) {
        return name;
    }
    const char c = dir[dir.size() - 1];
    if (c == '\\' || c == '/') {
        return dir + name;
    }
    return dir + "\\" + name;
}

/// 列出 dir 里匹配 pattern 的**文件名**（不含路径），已排序。
///
/// 为什么需要通配：归档名表里同时写着 "MAPSMD%02d.MIX"（按号试）和
/// "MAPSMD*.MIX"（通配扫），后者就是 FindFirstFile 的那种用法 ——
/// 编号之外的命名（mod 自造的、跳号的）只能靠它兜住。
///
/// 大小写不敏感（Win32 通配本来就如此），这点必须依赖：
/// 名表里写的是全大写 "MAPSMD%02d.MIX"，磁盘上是小写 mapsmd03.mix。
///
/// 排序是为了确定性：挂载列表在两次运行之间不能抖动。
std::vector<std::string> List_Files(const std::string& dir, const char* pattern) {
    std::vector<std::string> out;
    if (dir.empty() || pattern == nullptr) {
        return out;
    }
    const std::string pat = Join(dir, pattern);
    _finddata_t fd;
    const intptr_t h = _findfirst(pat.c_str(), &fd);
    if (h == -1) {
        return out;
    }
    do {
        if ((fd.attrib & _A_SUBDIR) == 0) {
            out.push_back(fd.name);
        }
    } while (_findnext(h, &fd) == 0);
    _findclose(h);
    std::sort(out.begin(), out.end());
    return out;
}

/// 列出 dir 的子目录名（不含 "." / ".."），已排序。
std::vector<std::string> List_Dirs(const std::string& dir) {
    std::vector<std::string> out;
    if (dir.empty()) {
        return out;
    }
    const std::string pat = Join(dir, "*");
    _finddata_t fd;
    const intptr_t h = _findfirst(pat.c_str(), &fd);
    if (h == -1) {
        return out;
    }
    do {
        if ((fd.attrib & _A_SUBDIR) != 0 && fd.name[0] != '.') {
            out.push_back(fd.name);
        }
    } while (_findnext(h, &fd) == 0);
    _findclose(h);
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace

const char* Version_Name(GameVersion v) {
    return (v == GameVersion::kYuri) ? "Yuri's Revenge" : "RA2";
}

GameVersion GameInstall::Guess(const char* dir) {
    // 判断依据：YR 才有 ra2md.mix（它是覆盖层，不是另一份素材）。
    return File_Exists(Join(dir, "ra2md.mix")) ? GameVersion::kYuri
                                               : GameVersion::kRA2;
}

std::vector<GameVersion> GameInstall::Detect_Installed(const char* dir) {
    std::vector<GameVersion> out;
    if (File_Exists(Join(dir, "game.exe")) && File_Exists(Join(dir, "ra2.mix"))) {
        out.push_back(GameVersion::kRA2);
    }
    if (File_Exists(Join(dir, "gamemd.exe")) && File_Exists(Join(dir, "ra2md.mix"))) {
        out.push_back(GameVersion::kYuri);
    }
    return out;
}

bool GameInstall::Resolve(const char* dir, GameVersion v, GamePaths* out,
                          std::string* missing) {
    if (dir == nullptr || out == nullptr) {
        return false;
    }
    out->version = v;
    out->dir = dir;

    // ---- 各自的必备文件 ----
    const char* kExe = (v == GameVersion::kYuri) ? "gamemd.exe" : "game.exe";
    const char* kLauncher = (v == GameVersion::kYuri) ? "RA2MD.EXE" : "ra2.exe";
    const char* kIni = (v == GameVersion::kYuri) ? "RA2MD.INI" : "RA2.INI";
    out->exe = Join(dir, kExe);
    out->launcher = Join(dir, kLauncher);
    out->ini = Join(dir, kIni);
    out->map_ext = (v == GameVersion::kYuri) ? ".yro" : ".mmx";

    // ---- 素材包，按挂载顺序（后挂的覆盖先挂的）----
    // 这一条最容易搞错：ra2md.mix **不是**独立的一份素材，它压在 ra2.mix 上面。
    // 所以 YR 的列表里 ra2.mix 必须在前 —— 反过来挂的话，原版的老 rules 会
    // 把 YR 的新值盖掉，单位表就退化成原版的了。
    out->mixes.clear();
    out->mixes.push_back(Join(dir, "ra2.mix"));
    if (v == GameVersion::kYuri) {
        out->mixes.push_back(Join(dir, "ra2md.mix"));

        // 扩展包是 **编号循环**，不是固定的 expandmd01。
        // 二进制证据：gamemd.exe .data 的归档名表里是
        //   "RA2.MIX" / "RA2MD.MIX" / "EXPANDMD%02d.MIX" / "THEMEMD.MIX"
        // （同一处还紧邻 "MIXFILES\\MOVMD*.MIX"，即整张归档名表）。
        // 只挂 01 的后果很实在：Reunion 2023 把 RULESMD.INI / ARTMD.INI 放在
        // expandmd01.mix，而 RULES.INI / Art.ini 放在 expandmd97.mix；
        // 只挂 01 时 `--unitdb` 直接报「一份 RULES/ART 都没有」（实测）。
        // 按数字升序推入，与"后挂的覆盖先挂的"语义一致。
        for (int i = 1; i <= 99; ++i) {
            char name[32];
            std::snprintf(name, sizeof(name), "expandmd%02d.mix", i);
            if (File_Exists(Join(dir, name))) {
                out->mixes.push_back(Join(dir, name));
            }
        }
        // 主题音乐包也在同一张归档名表里（THEME.MIX / THEMEMD.MIX）。
        if (File_Exists(Join(dir, "thememd.mix"))) {
            out->mixes.push_back(Join(dir, "thememd.mix"));
        }
    }
    // cameo（*ICON.SHP）在 language.mix / langmd.mix，不在 ra2.mix。
    // 缺了不致命（侧栏格子空着），所以不算 critical。
    if (File_Exists(Join(dir, "language.mix"))) {
        out->mixes.push_back(Join(dir, "language.mix"));
    }
    if (v == GameVersion::kYuri && File_Exists(Join(dir, "langmd.mix"))) {
        out->mixes.push_back(Join(dir, "langmd.mix"));
    }

    // ---- 地图归档：和扩展包一样是**编号循环**，不是某个固定名 ----
    // 证据（gamemd.exe .data 归档名表，与 EXPANDMD%02d.MIX 同一处）：
    //   0x41C2C4 "MAPS%02d.MIX"    0x426790 "MAPS*.MIX"
    //   0x41C2EC "MAPSMD%02d.MIX"  0x42679C "MAPSMD*.MIX"
    // 原先写死 MAPSMD03.MIX / maps02.mix 是错的 —— 它把"03"当成常量，
    // 换一份带 mapsmd01.mix 的安装就整个找不到地图。
    out->map_mixes.clear();
    {
        const char* pat_num =
            (v == GameVersion::kYuri) ? "MAPSMD%02d.MIX" : "MAPS%02d.MIX";
        const char* pat_wild =
            (v == GameVersion::kYuri) ? "MAPSMD*.MIX" : "MAPS*.MIX";
        std::vector<std::string> names;
        for (int i = 1; i <= 99; ++i) {
            char name[32];
            std::snprintf(name, sizeof(name), pat_num, i);
            if (File_Exists(Join(dir, name))) {
                names.push_back(name);
            }
        }
        const size_t numbered = names.size();
        // 通配那一路补"编号扫不到的名字"，并去重（不区分大小写）。
        for (const std::string& n : List_Files(dir, pat_wild)) {
            bool dup = false;
            for (size_t k = 0; k < numbered && !dup; ++k) {
                dup = names[k].size() == n.size();
                for (size_t c = 0; dup && c < n.size(); ++c) {
                    dup = std::tolower(static_cast<unsigned char>(names[k][c])) ==
                          std::tolower(static_cast<unsigned char>(n[c]));
                }
            }
            if (!dup) {
                names.push_back(n);
            }
        }
        for (const std::string& n : names) {
            out->map_mixes.push_back(Join(dir, n.c_str()));
        }
    }

    // ---- 松散地图目录 ----
    // 只认实实在在存在的目录；Maps 本身在就记一条（本装地图直接放 Maps/ 下）。
    out->map_dirs.clear();
    {
        const std::string maps = Join(dir, "Maps");
        if (!List_Dirs(maps).empty()) {
            out->map_dirs.push_back(maps);
            for (const std::string& sub : List_Dirs(maps)) {
                out->map_dirs.push_back(Join(maps.c_str(), sub.c_str()));
            }
        }
    }

    // ---- 缺文件统计 ----
    int miss = 0;
    auto need = [&](const std::string& p, bool critical) {
        if (File_Exists(p)) {
            return;
        }
        if (missing) {
            if (!missing->empty()) {
                *missing += ", ";
            }
            *missing += p;
        }
        if (critical) {
            ++miss;
        }
    };
    need(out->exe, true);
    need(Join(dir, "ra2.mix"), true);                 // 两个版本都靠它打底
    if (v == GameVersion::kYuri) {
        need(Join(dir, "ra2md.mix"), true);           // YR 的覆盖层，缺了就不是 YR
    }
    need(out->ini, false);                            // 缺了用默认值也能跑
    need(out->launcher, false);
    return miss == 0;
}

std::string GameInstall::Find_First_Map(const GamePaths& p) {
    // 后缀按"裸地图优先"排：.map 不用剥 MIX 壳，最省事也最不容易踩坑。
    static const char* kPats[] = {"*.map", "*.mmx", "*.yro"};
    for (const std::string& d : p.map_dirs) {
        for (const char* pat : kPats) {
            const std::vector<std::string> fs = List_Files(d, pat);
            if (!fs.empty()) {
                return Join(d, fs.front().c_str());
            }
        }
    }
    return std::string();
}

void GameInstall::Dump(const GamePaths& p) {
    std::printf("版本    : %s\n", Version_Name(p.version));
    std::printf("目录    : %s\n", p.dir.c_str());
    std::printf("主程序  : %s\n", p.exe.c_str());
    std::printf("配置    : %s\n", p.ini.c_str());
    std::printf("地图后缀: %s\n", p.map_ext.c_str());
    std::printf("素材包  : %zu 个（按挂载顺序，后挂覆盖先挂）\n", p.mixes.size());
    for (size_t i = 0; i < p.mixes.size(); ++i) {
        std::printf("   [%zu] %s\n", i, p.mixes[i].c_str());
    }
    // 注意 %%02d：这里 `%` 是 printf 的格式符，归档名里的 %02d 必须转义，
    // 否则 printf 会去栈上找一个不存在的 int（warning C4473）。
    std::printf("地图归档: %zu 个（MAPS%%02d.MIX / MAPSMD%%02d.MIX 编号循环 + 通配扫描）\n",
                p.map_mixes.size());
    for (size_t i = 0; i < p.map_mixes.size(); ++i) {
        std::printf("   [%zu] %s\n", i, p.map_mixes[i].c_str());
    }
    std::printf("松散地图: %zu 个目录\n", p.map_dirs.size());
    for (size_t i = 0; i < p.map_dirs.size(); ++i) {
        std::printf("   [%zu] %s\n", i, p.map_dirs[i].c_str());
    }
}

}  // namespace ra2
