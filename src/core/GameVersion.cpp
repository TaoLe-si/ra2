// GameVersion.cpp -- 见 GameVersion.h。

#include "core/GameVersion.h"

#include <cctype>
#include <cstdio>
#include <cstring>

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
        out->mixes.push_back(Join(dir, "expandmd01.mix"));
    }
    // cameo（*ICON.SHP）在 language.mix / langmd.mix，不在 ra2.mix。
    // 缺了不致命（侧栏格子空着），所以不算 critical。
    if (File_Exists(Join(dir, "language.mix"))) {
        out->mixes.push_back(Join(dir, "language.mix"));
    }
    if (v == GameVersion::kYuri && File_Exists(Join(dir, "langmd.mix"))) {
        out->mixes.push_back(Join(dir, "langmd.mix"));
    }

    out->map_mixes.clear();
    if (v == GameVersion::kYuri) {
        out->map_mixes.push_back(Join(dir, "MAPSMD03.MIX"));
    } else {
        out->map_mixes.push_back(Join(dir, "maps02.mix"));
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
    std::printf("地图包  : ");
    for (size_t i = 0; i < p.map_mixes.size(); ++i) {
        std::printf("%s%s", i ? ", " : "", p.map_mixes[i].c_str());
    }
    std::printf("\n");
}

}  // namespace ra2
