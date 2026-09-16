// SaveLoad.h -- 序列化 World 状态到磁盘
//
// 文件格式（自己定义，简单可移植）：
//   header:
//     "RA2SAVE1\0"      8 字节 magic
//     u32 version        1
//   block[]:
//     u32 id             // 0=map_path, 1=world, 2=houses, 3=objects, ...
//     u32 len
//     u8[] data
//
// 各 block 的 data 由对应 Serialize_*/Deserialize_* 函数产出。
//
// 我们不用原版 .yro (MIX 包 .pkt) 的格式 —— 那是封包 + 联网一致性校验，
// 这里单机调试够用。后续接联机再换成原版格式也不晚。

#pragma once
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "game/World.h"

namespace ra2 {

constexpr uint64_t kSaveMagic = 0x0045564153324152ULL;  // 'R','A','2','S','A','V','E','\0' LE
constexpr uint32_t kSaveVersion = 1;

// Block IDs
enum SaveBlock : uint32_t {
    kBlockMapPath    = 0x1001,
    kBlockWorld      = 0x1002,
    kBlockHouses     = 0x1003,
    kBlockObjects    = 0x1004,
    kBlockBullets    = 0x1005,
    kBlockTeams      = 0x1006,
    kBlockTriggers   = 0x1007,
    kBlockTeamTypes  = 0x1008,
    kBlockShroud     = 0x1009,
};

/// 把 World 当前状态写到 .sav 文件。返回 true 表示成功。
bool Save_World(const World& world, const std::string& map_path, const std::string& sav_path);

/// 从 .sav 文件恢复 World。返回 true 表示成功。
/// 注意：调用方需要先 Load_Map 把空 World 建出来（地图只重新加载，
/// 但 house 列表、出生点、对象初始摆放等从 map 来），
/// 然后再 Load_World 覆盖动态状态（已建好的对象、玩家动作、触发起始态）。
bool Load_World(World* world, std::string* map_path_out, const std::string& sav_path);

}  // namespace ra2