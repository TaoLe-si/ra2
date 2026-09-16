// LatTiles.cpp -- 见 LatTiles.h。算法对齐 CNCMaps Operations.FixTiles。

#include "map/LatTiles.h"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace ra2 {
namespace {

int Set_Of_Tile(const TheaterFile& th, int tile) {
    const TheaterTileSet* s = th.Set_Of(tile);
    return (s != nullptr) ? s->index : -1;
}

int First_Tile_Of_Set(const TheaterFile& th, int set_num) {
    const TheaterTileSet* s = th.Set_By_Num(set_num);
    return (s != nullptr && s->count > 0) ? s->base : -1;
}

int Tile_From_Set(const TheaterFile& th, int set_num, int within) {
    const TheaterTileSet* s = th.Set_By_Num(set_num);
    if (s == nullptr || within < 0 || within >= s->count) {
        return -1;
    }
    return s->base + within;
}

struct LatIds {
    int clear = -1;
    int green = -1, sand = -1, rough = -1, pave = -1;
    int clear_to_green = -1, clear_to_sand = -1, clear_to_rough = -1,
        clear_to_pave = -1;
    int misc_pave = -1, medians = -1, paved_roads = -1;
    int shore = -1, water_bridge = -1;
};

LatIds Load_Ids(const TheaterFile& th) {
    LatIds id;
    id.clear = th.General_Int("ClearTile", -1);
    id.green = th.General_Int("GreenTile", -1);
    id.sand = th.General_Int("SandTile", -1);
    id.rough = th.General_Int("RoughTile", -1);
    id.pave = th.General_Int("PaveTile", -1);
    id.clear_to_green = th.General_Int("ClearToGreenLat", -1);
    id.clear_to_sand = th.General_Int("ClearToSandLat", -1);
    id.clear_to_rough = th.General_Int("ClearToRoughLat", -1);
    id.clear_to_pave = th.General_Int("ClearToPaveLat", -1);
    id.misc_pave = th.General_Int("MiscPaveTile", -1);
    id.medians = th.General_Int("Medians", -1);
    id.paved_roads = th.General_Int("PavedRoads", -1);
    id.shore = th.General_Int("ShorePieces", -1);
    id.water_bridge = th.General_Int("WaterBridge", -1);
    return id;
}

bool Is_Lat(const LatIds& id, int set_num) {
    return set_num == id.rough || set_num == id.sand || set_num == id.green ||
           set_num == id.pave;
}

bool Is_Clat(const LatIds& id, int set_num) {
    return set_num == id.clear_to_rough || set_num == id.clear_to_sand ||
           set_num == id.clear_to_green || set_num == id.clear_to_pave;
}

int Lat_From_Clat(const LatIds& id, int clat_set) {
    if (clat_set == id.clear_to_rough) {
        return id.rough;
    }
    if (clat_set == id.clear_to_sand) {
        return id.sand;
    }
    if (clat_set == id.clear_to_green) {
        return id.green;
    }
    if (clat_set == id.clear_to_pave) {
        return id.pave;
    }
    return -1;
}

int Clat_From_Lat(const LatIds& id, int lat_set) {
    if (lat_set == id.rough) {
        return id.clear_to_rough;
    }
    if (lat_set == id.sand) {
        return id.clear_to_sand;
    }
    if (lat_set == id.green) {
        return id.clear_to_green;
    }
    if (lat_set == id.pave) {
        return id.clear_to_pave;
    }
    return -1;
}

bool Connect_Tiles(const LatIds& id, int a, int b) {
    if (a == b) {
        return false;
    }
    // ModEnc / CNCMaps 豁免表：这些对之间不插 CLAT。
    auto pair = [&](int x, int y) {
        return (a == x && b == y) || (a == y && b == x);
    };
    if (pair(id.green, id.shore) || pair(id.green, id.water_bridge)) {
        return false;
    }
    if (pair(id.pave, id.paved_roads) || pair(id.pave, id.medians) ||
        pair(id.pave, id.misc_pave)) {
        return false;
    }
    return true;
}

IsoCell* Cell_At(std::vector<IsoCell>& cells, int iso_w, int H, int dx, int row) {
    if (dx < 0 || row < 0 || dx >= iso_w || row >= H) {
        return nullptr;
    }
    return &cells[static_cast<size_t>(row) * static_cast<size_t>(iso_w) +
                  static_cast<size_t>(dx)];
}

/// CNCMaps TileLayer 对角邻格（LAT 用 TopRight/BottomRight/BottomLeft/TopLeft）。
void Diag_Neighbor(int dx, int row, int bit, int* out_dx, int* out_row) {
    // bit: 1=TR 2=BR 4=BL 8=TL —— 与 Operations.FixTiles 一致。
    int y = row;
    y += dx & 1;  // 对角分支先把 y += x%2
    switch (bit) {
        case 1:  // TopRight
            *out_dx = dx + 1;
            *out_row = y - 1;
            break;
        case 2:  // BottomRight
            *out_dx = dx + 1;
            *out_row = y;
            break;
        case 4:  // BottomLeft
            *out_dx = dx - 1;
            *out_row = y;
            break;
        case 8:  // TopLeft
            *out_dx = dx - 1;
            *out_row = y - 1;
            break;
        default:
            *out_dx = dx;
            *out_row = row;
            break;
    }
}

}  // namespace

int Apply_Lat(MapFile* map, const TheaterFile& theater) {
    if (map == nullptr || theater.Set_Count() <= 0) {
        return 0;
    }
    std::vector<IsoCell>& cells = map->Mutable_Cells();
    const int W = map->Width();
    const int H = map->Height();
    const int iso_w = map->Iso_Width();
    if (W <= 0 || H <= 0 || iso_w <= 0 ||
        cells.size() != static_cast<size_t>(iso_w) * static_cast<size_t>(H)) {
        return 0;
    }

    const LatIds id = Load_Ids(theater);
    if (id.green < 0 && id.sand < 0 && id.rough < 0 && id.pave < 0) {
        return 0;
    }

    // 每格所属 TileSet 编号（段号）。
    std::vector<int> set_of(cells.size(), -1);
    int clat_cells = 0;
    for (size_t i = 0; i < cells.size(); ++i) {
        set_of[i] = Set_Of_Tile(theater, cells[i].tile);
        if (Is_Clat(id, set_of[i])) {
            ++clat_cells;
        }
    }

    // Arena 等图在 FinalAlert 里已经 AutoLat 过（大量 glat/plat）。
    // 若先把 CLAT 还原成 LAT 再按本工程邻接重算，邻接轴向稍有偏差就会
    // 把整片路面撕成 plat/glat 棋盘。已有 CLAT 时信任地图，只补"纯 LAT
    // 固体格"的过渡（CNCMaps 在无预写 CLAT 的图上仍走全量 FixTiles）。
    const bool trust_map_clat = (clat_cells > 0);

    if (!trust_map_clat) {
        // 1) 编辑器预写的 CLAT 先还原成对应 LAT 基瓦（CNCMaps 同序）。
        for (size_t i = 0; i < cells.size(); ++i) {
            const int sn = set_of[i];
            if (!Is_Clat(id, sn)) {
                continue;
            }
            const int lat = Lat_From_Clat(id, sn);
            const int t0 = First_Tile_Of_Set(theater, lat);
            if (t0 < 0) {
                continue;
            }
            cells[i].tile = t0;
            cells[i].sub = 0;
            set_of[i] = lat;
        }
    }

    // 2) 对每个 LAT 固体格算四对角邻接位，换成 CLAT 过渡瓦。
    int changed = 0;
    for (int cy = 0; cy < H; ++cy) {
        for (int cx = 0; cx < iso_w; ++cx) {
            const size_t i =
                static_cast<size_t>(cy) * static_cast<size_t>(iso_w) +
                static_cast<size_t>(cx);
            const int sn = set_of[i];
            if (!Is_Lat(id, sn)) {
                continue;
            }
            int bits = 0;
            auto check = [&](int bit) {
                int ndx = 0, nrow = 0;
                Diag_Neighbor(cx, cy, bit, &ndx, &nrow);
                const IsoCell* n = Cell_At(cells, iso_w, H, ndx, nrow);
                if (n == nullptr) {
                    return;
                }
                const size_t ni =
                    static_cast<size_t>(nrow) * static_cast<size_t>(iso_w) +
                    static_cast<size_t>(ndx);
                int nset = set_of[ni];
                if (Is_Clat(id, nset) && Lat_From_Clat(id, nset) == sn) {
                    return;
                }
                if (Connect_Tiles(id, sn, nset)) {
                    bits |= bit;
                }
            };
            // TopRight=1 BottomRight=2 BottomLeft=4 TopLeft=8
            check(1);
            check(2);
            check(4);
            check(8);

            if (bits == 0) {
                continue;
            }
            const int clat = Clat_From_Lat(id, sn);
            const int nt = Tile_From_Set(theater, clat, bits);
            if (nt < 0) {
                continue;
            }
            cells[i].tile = nt;
            cells[i].sub = 0;
            ++changed;
        }
    }

    std::printf("  LAT 过渡：改写 %d 格（地图已有 CLAT %d，Green=%d Sand=%d "
                "Rough=%d Pave=%d）\n",
                changed, clat_cells, id.green, id.sand, id.rough, id.pave);
    return changed;
}

}  // namespace ra2
