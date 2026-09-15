// TheaterFile.cpp -- 剧场控制 INI 解析。

#include "map/TheaterFile.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string.h>
#include <utility>

#include "data/Ini.h"

namespace ra2 {
namespace {

/// 段名 "TileSet0002" -> 2；不是 TileSet 段返回 -1。
int Tileset_Index(const std::string& name) {
    if (name.size() < 8) {
        return -1;
    }
    if (strncmp(name.c_str(), "TileSet", 7) != 0 && strncmp(name.c_str(), "tileset", 7) != 0) {
        return -1;
    }
    return atoi(name.c_str() + 7);
}

int Atoi_Safe(const std::string& s, int def = 0) {
    if (s.empty()) {
        return def;
    }
    return atoi(s.c_str());
}

}  // namespace

bool TheaterFile::Load(const uint8_t* data, size_t size) {
    sets_.clear();
    tile_count_ = 0;
    general_.clear();

    IniFile ini;
    if (!ini.Load(data, size)) {
        return false;
    }

    // [General] 里的数值项先收下来（ClearTile / HeightBase / CliffSet ...）。
    if (const IniSection* g = ini.Find_Section("General")) {
        for (const IniEntry& e : g->entries) {
            general_.emplace_back(e.key, Atoi_Safe(e.value));
        }
    }

    // [TileSetnnnn] 按数字序号升序收集。
    std::vector<std::pair<int, const IniSection*>> ordered;
    for (int i = 0; i < ini.Section_Count(); ++i) {
        const IniSection* s = ini.Section(i);
        if (s == nullptr) {
            continue;
        }
        const int idx = Tileset_Index(s->name);
        if (idx >= 0) {
            ordered.emplace_back(idx, s);
        }
    }
    if (ordered.empty()) {
        return false;
    }
    // 文件里本来就是升序，排一遍是为了防止有补丁段乱序。
    std::sort(ordered.begin(), ordered.end(),
              [](const std::pair<int, const IniSection*>& a,
                 const std::pair<int, const IniSection*>& b) { return a.first < b.first; });

    for (const auto& kv : ordered) {
        const IniSection* s = kv.second;
        std::string file_name = ini.Get_String(s->name.c_str(), "FileName", "");
        if (file_name.empty()) {
            continue;                       // 没有 FileName 的段不产生瓦片
        }
        TheaterTileSet ts;
        ts.section = s->name;
        ts.set_name = ini.Get_String(s->name.c_str(), "SetName", "");
        ts.file_name = file_name;
        ts.count = ini.Get_Int(s->name.c_str(), "TilesInSet", 0);
        if (ts.count < 0) {
            ts.count = 0;
        }
        ts.base = tile_count_;
        tile_count_ += ts.count;
        sets_.push_back(ts);
    }
    return tile_count_ > 0;
}

std::string TheaterFile::Tile_Name(int index) const {
    if (index < 0 || index >= tile_count_) {
        return std::string();
    }
    for (const TheaterTileSet& s : sets_) {
        if (index >= s.base && index < s.base + s.count) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%s%02d", s.file_name.c_str(),
                          index - s.base + 1);
            return std::string(buf);
        }
    }
    return std::string();
}

const TheaterTileSet* TheaterFile::Set_Of(int index) const {
    if (index < 0 || index >= tile_count_) {
        return nullptr;
    }
    for (const TheaterTileSet& s : sets_) {
        if (index >= s.base && index < s.base + s.count) {
            return &s;
        }
    }
    return nullptr;
}

int TheaterFile::General_Int(const char* key, int def) const {
    for (const auto& kv : general_) {
        if (_stricmp(kv.first.c_str(), key) == 0) {
            return kv.second;
        }
    }
    return def;
}

}  // namespace ra2
