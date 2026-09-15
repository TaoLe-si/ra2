// ObjectSprite.cpp

#include "gfx/ObjectSprite.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "gfx/ShpFile.h"
#include "gfx/VoxelLight.h"

namespace ra2 {
namespace {

/// 绕模型空间 Z 轴（竖直轴）转 yaw 的 3×4 矩阵。
/// 模型空间：X 向前、Y 横向、Z 向上（实测四足机甲四脚分居 ±X/±Y）。
void Yaw_Matrix(float yaw, float out[12]) {
    const float c = std::cos(yaw);
    const float s = std::sin(yaw);
    // 行主序 3×4：前 3 列旋转，第 4 列平移（这里为 0）
    out[0] =  c; out[1] = -s; out[2] = 0.0f; out[3] = 0.0f;
    out[4] =  s; out[5] =  c; out[6] = 0.0f; out[7] = 0.0f;
    out[8] = 0.0f; out[9] = 0.0f; out[10] = 1.0f; out[11] = 0.0f;
}

/// 剧场的单位调色板。RA2 每个剧场一套，SHP 单位必须用它上色。
/// 实测文件名：unittem / uniturb / unitsno / unitdes / unitlun（+.pal）。
const char* Theater_Palette(const std::string& theater) {
    if (theater == "URBAN")     return "uniturb.pal";
    if (theater == "SNOW")      return "unitsno.pal";
    if (theater == "DESERT")    return "unitdes.pal";
    if (theater == "LUNAR")     return "unitlun.pal";
    if (theater == "NEWURBAN")  return "unitubn.pal";
    return "unittem.pal";       // TEMPERATE 及兜底
}

}  // namespace

// ---------------------------------------------------------------------------
// Bind
// ---------------------------------------------------------------------------

bool SpriteCache::Bind(const std::vector<MixFileClass*>& roots) {
    roots_ = roots;
    if (roots_.empty()) {
        return false;
    }
    return models_.Load(roots_.data(), static_cast<int>(roots_.size()));
}

// ---------------------------------------------------------------------------
// 索引图 -> RGBA
// ---------------------------------------------------------------------------

void SpriteCache::Index_To_RGBA(const uint8_t* indexed, int n,
                                const uint8_t* pal768, std::vector<uint8_t>* out) {
    out->assign(static_cast<size_t>(n) * 4, 0);
    for (int i = 0; i < n; ++i) {
        const uint8_t idx = indexed[i];
        if (idx == 0) {
            continue;                       // 0 = 透明
        }
        const size_t p = static_cast<size_t>(idx) * 3;
        (*out)[static_cast<size_t>(i) * 4 + 0] = pal768[p + 0];
        (*out)[static_cast<size_t>(i) * 4 + 1] = pal768[p + 1];
        (*out)[static_cast<size_t>(i) * 4 + 2] = pal768[p + 2];
        (*out)[static_cast<size_t>(i) * 4 + 3] = 255;
    }
}

// ---------------------------------------------------------------------------
// 体素单位
// ---------------------------------------------------------------------------

bool SpriteCache::Build_Voxel(const UnitModel& um, float yaw, int house_color,
                              ObjectSprite* out) {
    // 车体 + 炮塔 + 炮管，三者共享模型空间原点（UnitModel.h 里有实测证据）
    const VxlFile* body = nullptr;
    if (um.body.id != 0) {
        auto it = vxl_cache_.find(um.body.id);
        if (it == vxl_cache_.end()) {
            std::vector<uint8_t> data;
            for (MixFileClass* m : roots_) {
                data = m->Read_Deep_By_ID(um.body.id);
                if (!data.empty()) {
                    break;
                }
            }
            auto f = std::make_unique<VxlFile>();
            if (data.empty() || !f->Load(data.data(), data.size())) {
                vxl_cache_[um.body.id] = nullptr;
            } else {
                vxl_cache_[um.body.id] = std::move(f);
            }
            it = vxl_cache_.find(um.body.id);
        }
        body = it->second.get();
    }
    if (body == nullptr) {
        return false;
    }

    // HVA 姿态。多肢模型（四足机甲）才有意义，单肢模型传 nullptr 走静态尾。
    std::vector<float> pose;
    const float* pose_ptr = nullptr;
    if (um.body_hva.present && body->Limb_Count() > 1) {
        auto it = hva_cache_.find(um.body_hva.id);
        if (it == hva_cache_.end()) {
            std::vector<uint8_t> data;
            for (MixFileClass* m : roots_) {
                data = m->Read_Deep_By_ID(um.body_hva.id);
                if (!data.empty()) {
                    break;
                }
            }
            auto h = std::make_unique<HvaFile>();
            if (data.empty() || !h->Load(data.data(), data.size())) {
                hva_cache_[um.body_hva.id] = nullptr;
            } else {
                hva_cache_[um.body_hva.id] = std::move(h);
            }
            it = hva_cache_.find(um.body_hva.id);
        }
        const HvaFile* hva = it->second.get();
        if (hva != nullptr && hva->Limb_Count() == body->Limb_Count()) {
            pose.resize(static_cast<size_t>(body->Limb_Count()) * 12);
            for (int l = 0; l < body->Limb_Count(); ++l) {
                const HvaMatrix mm = hva->Matrix(0, l);
                std::memcpy(&pose[static_cast<size_t>(l) * 12], mm.m,
                            sizeof(float) * 12);
            }
            pose_ptr = pose.data();
        }
    }

    // 附加层：炮塔 / 炮管
    VxlAttach attach[2];
    int attach_count = 0;
    std::unique_ptr<VxlFile> turret_vxl, barrel_vxl;
    if (um.turret_vxl.present) {
        std::vector<uint8_t> data;
        for (MixFileClass* m : roots_) {
            data = m->Read_Deep_By_ID(um.turret_vxl.id);
            if (!data.empty()) {
                break;
            }
        }
        if (!data.empty()) {
            turret_vxl = std::make_unique<VxlFile>();
            if (turret_vxl->Load(data.data(), data.size())) {
                attach[attach_count].file = turret_vxl.get();
                float ident[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
                std::memcpy(attach[attach_count].transform, ident, sizeof(ident));
                ++attach_count;
            } else {
                turret_vxl.reset();
            }
        }
    }
    if (um.barrel_vxl.present) {
        std::vector<uint8_t> data;
        for (MixFileClass* m : roots_) {
            data = m->Read_Deep_By_ID(um.barrel_vxl.id);
            if (!data.empty()) {
                break;
            }
        }
        if (!data.empty()) {
            barrel_vxl = std::make_unique<VxlFile>();
            if (barrel_vxl->Load(data.data(), data.size())) {
                attach[attach_count].file = barrel_vxl.get();
                float ident[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
                std::memcpy(attach[attach_count].transform, ident, sizeof(ident));
                ++attach_count;
            } else {
                barrel_vxl.reset();
            }
        }
    }

    // 朝向：整个模型绕 Z 转 yaw
    float model_xform[12];
    Yaw_Matrix(yaw, model_xform);

    // 光影：和原版同一套（VoxelLight 的算法是从 gamemd.exe 抄的）
    // 光向量：默认 normalize(1,1,2) 就是原版那个太阳方位，别乱改
    VoxelLight light;

    std::vector<uint8_t> indexed, shade;
    int w = 0, h = 0;
    // scale：一格 60px 宽，体素单位 det=1/12，经验上 8 倍刚好让一辆坦克
    // 占满一格多一点。等 rules 的 Size= 接进来再按真值调。
    if (!body->Render_Isometric(&indexed, &w, &h, 8.0f, pose_ptr, attach,
                                attach_count, &light, &shade, nullptr, 0.0f,
                                nullptr, model_xform)) {
        return false;
    }
    if (w <= 0 || h <= 0) {
        return false;
    }

    // 调色板：VXL 自带 768，再按阵营色把 16..31 整段换掉
    uint8_t pal768[768];
    std::memcpy(pal768, body->Palette(), 768);
    remap_.Make_Palette768(pal768, true, body->Remap_Start(), body->Remap_End(),
                           house_color, pal768);

    // 明暗：索引图 + 明暗级 + 调色板 -> RGBA（VoxelLight 的算法是从 exe 抄的）
    std::vector<uint8_t> rgba;
    Shade_To_RGBA(indexed.data(), shade.data(), w * h, pal768, light, &rgba);
    out->rgba = std::move(rgba);
    out->w = w;
    out->h = h;
    // 体素模型的"脚下"在画布中下方，把落点对齐到格心
    out->off_x = -w / 2;
    out->off_y = -h + 12;
    out->ok = true;
    return true;
}

// ---------------------------------------------------------------------------
// SHP 单位（步兵 / 建筑 / 装饰）
// ---------------------------------------------------------------------------

bool SpriteCache::Build_Shp(const char* image, int house_color, ObjectSprite* out) {
    std::string file = std::string(image) + ".SHP";
    std::vector<uint8_t> data;
    for (MixFileClass* m : roots_) {
        data = m->Read_Deep(file.c_str());
        if (!data.empty()) {
            break;
        }
    }
    if (data.empty()) {
        return false;
    }
    ShpFile shp;
    if (!shp.Load(data.data(), data.size()) || shp.Frame_Count() <= 0) {
        return false;
    }
    const int frame = 0;
    const std::vector<uint8_t>& px = shp.Frame_Pixels(frame);
    const ShpFrameInfo& fi = shp.Frame_Info(frame);
    if (fi.w <= 0 || fi.h <= 0 || px.size() < static_cast<size_t>(fi.w) * fi.h) {
        return false;
    }

    // 调色板：剧场 .pal
    std::vector<uint8_t> pal_data;
    for (MixFileClass* m : roots_) {
        pal_data = m->Read_Deep(Theater_Palette(theater_));
        if (pal_data.size() >= 768) {
            break;
        }
        pal_data.clear();
    }
    uint8_t pal768[768];
    if (pal_data.size() >= 768) {
        std::memcpy(pal768, pal_data.data(), 768);
    } else {
        // 拿不到剧场调色板就退成灰度，至少能看出形状
        for (int i = 0; i < 256; ++i) {
            pal768[i * 3 + 0] = pal768[i * 3 + 1] = pal768[i * 3 + 2] =
                static_cast<uint8_t>(i);
        }
    }
    // .PAL 文件是 6 位分量，要展开成 8 位（expanded=false）
    remap_.Make_Palette768(pal768, false, 16, 31, house_color, pal768);

    Index_To_RGBA(px.data(), fi.w * fi.h, pal768, &out->rgba);
    out->w = fi.w;
    out->h = fi.h;
    out->off_x = -fi.w / 2;
    out->off_y = -fi.h / 2;
    out->ok = true;
    return true;
}

// ---------------------------------------------------------------------------
// Get
// ---------------------------------------------------------------------------

const ObjectSprite* SpriteCache::Get(const char* type, int facing,
                                     int house_color) {
    Key key;
    key.type = type;
    key.step = (facing * kFacingSteps) / 256;
    if (key.step < 0) key.step = 0;
    if (key.step >= kFacingSteps) key.step = kFacingSteps - 1;
    key.color = house_color;

    auto it = cache_.find(key);
    if (it != cache_.end()) {
        return it->second->ok ? it->second.get() : nullptr;
    }
    // 本帧预算用完了：先不建，退化成色块，下一帧再来。
    if (budget_left_ <= 0) {
        return nullptr;
    }
    --budget_left_;

    auto sp = std::make_unique<ObjectSprite>();
    bool ok = false;
    const UnitModel* um = models_.Resolve(type);
    if (um != nullptr && um->voxel) {
        const float yaw = static_cast<float>(key.step) * 6.28318530718f /
                          static_cast<float>(kFacingSteps);
        ok = Build_Voxel(*um, yaw, house_color, sp.get());
    }
    if (!ok) {
        // 不是体素（步兵/建筑/装饰），或者体素没渲出来，退到 SHP
        const char* image = (um != nullptr && !um->image.empty()) ? um->image.c_str()
                                                                  : type;
        ok = Build_Shp(image, house_color, sp.get());
    }
    if (!ok) {
        // 同名 SHP 再试一次（有些单位的 Image= 和原名都不带 .SHP 后缀命中）
        ok = Build_Shp(type, house_color, sp.get());
    }

    ++built_;
    if (!ok) {
        ++failed_;
        last_failure_ = type;
        // 只报前若干个，免得几百个装饰物刷屏
        if (failed_ <= 8) {
            std::printf("    精灵缺失: %s（退化成色块）\n", type);
        }
    }
    sp->ok = ok;
    const ObjectSprite* p = sp.get();
    cache_[key] = std::move(sp);
    return ok ? p : nullptr;
}

}  // namespace ra2
