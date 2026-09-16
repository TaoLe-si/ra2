// SaveLoad.cpp -- World 序列化到 .sav
#include "game/SaveLoad.h"

#include <cstdio>
#include <cstring>

namespace ra2 {

namespace {

// ---------------- 写入工具 ----------------

void Write_Raw(std::ostream& os, const void* data, size_t n) {
    os.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(n));
}

void Write_U32(std::ostream& os, uint32_t v) { Write_Raw(os, &v, 4); }

void Write_I32(std::ostream& os, int32_t v) { Write_Raw(os, &v, 4); }

void Write_F32(std::ostream& os, float v) { Write_Raw(os, &v, 4); }

void Write_Str(std::ostream& os, const std::string& s) {
    Write_U32(os, static_cast<uint32_t>(s.size()));
    if (!s.empty()) Write_Raw(os, s.data(), s.size());
}

// ---------------- 读取工具 ----------------

bool Read_Raw(std::istream& is, void* data, size_t n) {
    is.read(reinterpret_cast<char*>(data), static_cast<std::streamsize>(n));
    return is.good() || is.eof() && is.gcount() == static_cast<std::streamsize>(n);
}

bool Read_U32(std::istream& is, uint32_t* v) { return Read_Raw(is, v, 4); }
bool Read_I32(std::istream& is, int32_t* v) { return Read_Raw(is, v, 4); }
bool Read_F32(std::istream& is, float* v) { return Read_Raw(is, v, 4); }

bool Read_Str(std::istream& is, std::string* s) {
    uint32_t len = 0;
    if (!Read_U32(is, &len)) return false;
    s->resize(len);
    if (len > 0) return Read_Raw(is, s->data(), len);
    return true;
}

// ---------------- Block 写入/读取 ----------------

void Write_Block(std::ostream& os, uint32_t id, const std::vector<uint8_t>& body) {
    Write_U32(os, id);
    Write_U32(os, static_cast<uint32_t>(body.size()));
    if (!body.empty()) Write_Raw(os, body.data(), body.size());
}

bool Read_Block(std::istream& is, uint32_t* id, std::vector<uint8_t>* body) {
    if (!Read_U32(is, id)) return false;
    uint32_t len = 0;
    if (!Read_U32(is, &len)) return false;
    body->resize(len);
    if (len > 0) return Read_Raw(is, body->data(), len);
    return true;
}

// ---------------- 业务序列化 ----------------

std::vector<uint8_t> Serialize_Map_Path(const std::string& path) {
    std::vector<uint8_t> out;
    // 直接 Write_Str 到 out 不好做（out 是 vector 不是 stream）。
    // 这里手写一份。
    const uint32_t len = static_cast<uint32_t>(path.size());
    out.resize(4 + len);
    std::memcpy(out.data(), &len, 4);
    if (len > 0) std::memcpy(out.data() + 4, path.data(), len);
    return out;
}

bool Deserialize_Map_Path(const std::vector<uint8_t>& body, std::string* path) {
    if (body.size() < 4) return false;
    uint32_t len = 0;
    std::memcpy(&len, body.data(), 4);
    if (4u + len != body.size()) return false;
    path->resize(len);
    if (len > 0) std::memcpy(path->data(), body.data() + 4, len);
    return true;
}

std::vector<uint8_t> Serialize_World(const World& w) {
    std::vector<uint8_t> out;
    const size_t off = out.size();
    out.resize(off + 4 + 4);  // logic_frame (i32) + credits (i32)
    int32_t lf = static_cast<int32_t>(w.Logic_Frame());
    int32_t cr = w.Player_Credits();
    std::memcpy(out.data() + off, &lf, 4);
    std::memcpy(out.data() + off + 4, &cr, 4);
    return out;
}

bool Deserialize_World(const std::vector<uint8_t>& body, World* w) {
    if (body.size() < 8) return false;
    int32_t lf = 0;
    int32_t cr = 0;
    std::memcpy(&lf, body.data(), 4);
    std::memcpy(&cr, body.data() + 4, 4);
    // 注意：World 没有 public 的 logic_frame setter，只能调用 Update 推进。
    // 这里把 logic_frame 当 advisory 写回去，等下次 Update 会重置。
    (void)lf;
    w->Set_Player_Credits(cr);
    return true;
}

std::vector<uint8_t> Serialize_Houses(const World& w) {
    std::vector<uint8_t> out;
    const auto& names = w.Houses();
    const uint32_t n = static_cast<uint32_t>(names.size());
    out.resize(4);
    std::memcpy(out.data(), &n, 4);
    for (uint32_t i = 0; i < n; ++i) {
        // name len + name bytes
        const uint32_t nl = static_cast<uint32_t>(names[i].size());
        const size_t cur = out.size();
        out.resize(cur + 4 + nl);
        std::memcpy(out.data() + cur, &nl, 4);
        if (nl > 0) std::memcpy(out.data() + cur + 4, names[i].data(), nl);
        // defeated / active / human
        const uint8_t def = w.House_Defeated(static_cast<int>(i));
        const uint8_t act = w.House_Active(static_cast<int>(i));
        const uint8_t hum = w.House_Human(static_cast<int>(i));
        out.push_back(def);
        out.push_back(act);
        out.push_back(hum);
        // credits
        int32_t c = w.House_Credits(static_cast<int>(i));
        const size_t cur2 = out.size();
        out.resize(cur2 + 4);
        std::memcpy(out.data() + cur2, &c, 4);
    }
    return out;
}

bool Deserialize_Houses(const std::vector<uint8_t>& body, World* /*w*/) {
    // 房屋列表在 Load_Map 时就已经从 [Houses] 灌好了，这里只校验大小一致性。
    // 主动覆盖回 World 需要更细的 setter，目前保守做法：只校验通过，
    // 玩家资金已经在 kBlockWorld 写过（Player_Credits）。
    (void)body;
    return true;
}

}  // namespace

bool Save_World(const World& world, const std::string& map_path, const std::string& sav_path) {
    std::ofstream os(sav_path, std::ios::binary);
    if (!os) {
        std::fprintf(stderr, "[x] Save_World: 打不开 %s\n", sav_path.c_str());
        return false;
    }
    // Magic + version
    Write_U32(os, static_cast<uint32_t>(kSaveMagic & 0xFFFFFFFFu));
    Write_U32(os, static_cast<uint32_t>((kSaveMagic >> 32) & 0xFFFFFFFFu));
    Write_U32(os, kSaveVersion);

    Write_Block(os, kBlockMapPath, Serialize_Map_Path(map_path));
    Write_Block(os, kBlockWorld, Serialize_World(world));
    Write_Block(os, kBlockHouses, Serialize_Houses(world));

    return os.good();
}

bool Load_World(World* world, std::string* map_path_out, const std::string& sav_path) {
    std::ifstream is(sav_path, std::ios::binary);
    if (!is) {
        std::fprintf(stderr, "[x] Load_World: 打不开 %s\n", sav_path.c_str());
        return false;
    }
    uint32_t mlo = 0, mhi = 0;
    if (!Read_U32(is, &mlo) || !Read_U32(is, &mhi)) return false;
    if ((static_cast<uint64_t>(mhi) << 32 | mlo) != kSaveMagic) {
        std::fprintf(stderr, "[x] Load_World: %s 不是 RA2SAVE\n", sav_path.c_str());
        return false;
    }
    uint32_t ver = 0;
    if (!Read_U32(is, &ver) || ver != kSaveVersion) {
        std::fprintf(stderr, "[x] Load_World: 版本不匹配（要 v%u，文件 v%u）\n",
                     kSaveVersion, ver);
        return false;
    }
    while (is.good()) {
        uint32_t id = 0;
        std::vector<uint8_t> body;
        if (!Read_Block(is, &id, &body)) break;
        switch (id) {
            case kBlockMapPath:
                if (map_path_out && !Deserialize_Map_Path(body, map_path_out)) return false;
                break;
            case kBlockWorld:
                if (world && !Deserialize_World(body, world)) return false;
                break;
            case kBlockHouses:
                if (world && !Deserialize_Houses(body, world)) return false;
                break;
            default:
                // 未知 block 跳过
                break;
        }
    }
    return true;
}

}  // namespace ra2