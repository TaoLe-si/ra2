// 自动生成文件，请勿手改。生成工具：tools/sizeofscan.py
// 数据来源：gamemd.exe 的 `push <size>; call operator new` 与构造函数写虚表的配对。
// 只保留票数 >= 4 的条目（同 size 被多个独立调用点证实）；
// 另外无条件保留被『字段末端』校准过的条目（证据比投票硬）。
// 完整数据见 db/sizes.json，说明见 docs/sizes.md。
#pragma once
#include <cstdint>

namespace ra2 {
namespace re {

struct SizeInfo {
    const char* name;
    uint32_t    size;
    uint8_t     from_new;  // 1 = 来自 new 的分配大小；0 = 来自数组步长（弱）
    uint8_t     votes;
};

inline constexpr int kSizeCount = 67;

inline constexpr SizeInfo kSizeTable[kSizeCount] = {
    {"WinModemClass", 8328, 1, 4},
    {"AircraftTypeClass", 3600, 1, 6},
    {"UnitClass", 2280, 1, 23},
    {"InfantryClass", 1776, 1, 21},
    {"AircraftClass", 1752, 1, 4},
    {"CampaignClass", 928, 1, 4},
    {"VRGBClass::?$TypeList", 792, 1, 11},
    {"TerrainTypeClass", 700, 1, 6},
    {"SmudgeTypeClass", 676, 1, 6},
    {"ScriptTypeClass", 564, 1, 12},
    {"PBVAnimTypeClass::?$TypeList", 464, 1, 8},
    {"TubeClass", 452, 1, 8},
    {"VWstring::?$DynamicVectorClass", 444, 1, 6},
    {"W4DiskID::?$TypeList", 444, 1, 7},
    {"MapSeedClass", 376, 1, 4},
    {"PAVParticleClass::?$DynamicVectorClass", 256, 1, 19},
    {"TerrainClass", 224, 1, 14},
    {"TaskForceClass", 212, 1, 10},
    {"IPXConnClass", 184, 1, 4},
    {"H::?$TypeList", 180, 1, 8},
    {"TriggerTypeClass", 180, 1, 11},
    {"IsometricTileClass", 176, 1, 4},
    {"OverlayClass", 176, 1, 8},
    {"SmudgeClass", 176, 1, 5},
    {"PAVPlanningNodeClass::?$DynamicVectorClass", 156, 1, 9},
    {"Mouse", 152, 1, 6},
    {"PBVTechnoTypeClass::?$DynamicVectorClass", 116, 1, 26},
    {"RadSiteClass", 116, 1, 6},
    {"CCFileClass", 108, 1, 2},
    {"AirstrikeClass", 96, 1, 4},
    {"BombClass", 92, 1, 4},
    {"CCINIClass", 88, 1, 6},
    {"INIClass::PAUINISection::?$List", 88, 1, 4},
    {"ParasiteClass", 88, 1, 4},
    {"TemporalClass", 80, 1, 4},
    {"LightSourceClass", 76, 1, 4},
    {"TextLabelClass", 76, 1, 4},
    {"TriggerClass", 72, 1, 6},
    {"DiskLaserClass", 64, 1, 4},
    {"VWaypointClass::?$DynamicVectorClass", 64, 1, 24},
    {"PixelFXClass", 60, 1, 4},
    {"TagClass", 56, 1, 8},
    {"MSAnim", 52, 1, 12},
    {"ScriptClass", 48, 1, 9},
    {"I::?$DynamicVectorClass", 40, 1, 4},
    {"MixFileClass", 40, 1, 106},
    {"DSurface", 36, 1, 41},
    {"RawFileClass", 36, 1, 4},
    {"MSFont", 32, 1, 4},
    {"Surface", 32, 1, 4},
    {"XSurface", 32, 1, 27},
    {"H::?$VectorClass", 24, 0, 4},
    {"PAVFoggedObjectClass::?$DynamicVectorClass", 24, 1, 4},
    {"PAVTechnoClass::?$DynamicVectorClass", 24, 1, 10},
    {"PAVTechnoClass::?$VectorClass", 24, 1, 9},
    {"USubzoneConnectionStruct::?$VectorClass", 24, 0, 4},
    {"VCell::?$DynamicVectorClass", 24, 1, 14},
    {"VCell::?$VectorClass", 24, 1, 6},
    {"VPoint2D::?$VectorClass", 24, 0, 6},
    {"PAD::?$DynamicVectorClass", 16, 1, 11},
    {"ReferenceCounted", 16, 1, 4},
    {"rc_ptr_base", 16, 1, 4},
    {"AddTeamCommandClass", 8, 1, 20},
    {"CenterTeamCommandClass", 8, 1, 20},
    {"CreateTeamCommandClass", 8, 1, 20},
    {"SelectTeamCommandClass", 8, 1, 20},
    {"TauntCommandClass", 8, 1, 16},
};

// 按类名查 sizeof；查不到返回 0
inline uint32_t SizeOf(const char* name) {
    for (int i = 0; i < kSizeCount; ++i) {
        const char* a = kSizeTable[i].name;
        const char* b = name;
        while (*a && *a == *b) { ++a; ++b; }
        if (*a == *b) return kSizeTable[i].size;
    }
    return 0;
}

}  // namespace re
}  // namespace ra2
