// ObjectSprite.cpp

#include "gfx/ObjectSprite.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "gfx/ShpFile.h"
#include "gfx/VoxelLight.h"
#include "gfx/dx12/Dx12Renderer.h"

namespace ra2 {
namespace {

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

void SpriteCache::Expand_Pal768(const uint8_t* pal6, uint8_t* out768) {
    // .PAL 存的是 6 位分量，要展开成 8 位。用 (v<<2)|(v>>4) 而不是 v*4 ——
    // 后者最大只到 252，整幅会偏暗一档。
    for (int i = 0; i < 256; ++i) {
        for (int c = 0; c < 3; ++c) {
            const uint8_t v = static_cast<uint8_t>(pal6[i * 3 + c] & 0x3F);
            out768[i * 3 + c] = static_cast<uint8_t>((v << 2) | (v >> 4));
        }
    }
}

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
//
// CPU 到这里就收工：LZO 解压 + 列解码，产出"只解码、不投影"的几何，
// 上传一次。之后每个朝向只是换一组根常量再烘一张，投影 / 明暗查表 /
// 调色板查表 / 画家序全在着色器里（见 Dx12Renderer::Bake_Voxels）。
// ---------------------------------------------------------------------------

bool SpriteCache::Load_Voxel_Model(const UnitModel& um, VoxelModel* out) {
    // 车体
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
    out->body = body;

    // HVA 姿态。多肢模型（四足机甲）才有意义，单肢模型传 nullptr 走静态尾。
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
            out->pose.resize(static_cast<size_t>(body->Limb_Count()) * 12);
            for (int l = 0; l < body->Limb_Count(); ++l) {
                const HvaMatrix mm = hva->Matrix(0, l);
                std::memcpy(&out->pose[static_cast<size_t>(l) * 12], mm.m,
                            sizeof(float) * 12);
            }
            out->pose_ptr = out->pose.data();
        }
    }

    // 附加层：炮塔 / 炮管。三者共享模型空间原点（UnitModel.h 里有实测证据），
    // 所以 attach 变换是单位阵就行，不需要坐标换算。
    const float ident[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    struct Add {
        bool present;
        uint32_t id;
    } adds[2] = {{um.turret_vxl.present, um.turret_vxl.id},
                 {um.barrel_vxl.present, um.barrel_vxl.id}};
    for (int a = 0; a < 2; ++a) {
        if (!adds[a].present) {
            continue;
        }
        std::vector<uint8_t> data;
        for (MixFileClass* m : roots_) {
            data = m->Read_Deep_By_ID(adds[a].id);
            if (!data.empty()) {
                break;
            }
        }
        if (data.empty()) {
            continue;
        }
        auto f = std::make_unique<VxlFile>();
        if (!f->Load(data.data(), data.size())) {
            continue;
        }
        const int k = out->attach_count;
        out->owned[k] = std::move(f);
        out->attach[k].file = out->owned[k].get();
        std::memcpy(out->attach[k].transform, ident, sizeof(ident));
        out->attach_count = k + 1;
    }
    return true;
}

const SpriteCache::GpuGeom* SpriteCache::Ensure_Geom(const char* type,
                                                     const UnitModel& um) {
    auto it = gpu_geoms_.find(type);
    if (it != gpu_geoms_.end()) {
        return it->second.get();   // nullptr = 上次就失败了，别再试
    }
    auto gg = std::make_unique<GpuGeom>();
    bool ok = false;
    // 没有渲染器就别白解码了 —— 反正也烘不出来
    if (renderer_ != nullptr) {
        VoxelModel vm;
        if (Load_Voxel_Model(um, &vm) &&
            vm.body->Build_GPU_Geom(vm.pose_ptr, vm.attach, vm.attach_count, 0.0f,
                                    &gg->geom)) {
            gg->id = renderer_->Upload_Voxel_Geom(
                gg->geom.voxels.data(),
                static_cast<int>(gg->geom.voxels.size() / 2),
                gg->geom.limbs.data(), static_cast<int>(gg->geom.limbs.size()));
            if (gg->id >= 0) {
                std::memcpy(gg->base_pal, vm.body->Palette(), 768);
                gg->remap_start = vm.body->Remap_Start();
                gg->remap_end = vm.body->Remap_End();
                ok = true;
            }
        }
    }
    if (ok) {
        // 体素已经进显存了，CPU 这份可以扔掉：一辆坦克 8 万体素 = 640KB，
        // 几十个类型就是几十 MB。肢体矩阵要留着（算画布要用）。
        gg->geom.voxels.clear();
        gg->geom.voxels.shrink_to_fit();
    } else {
        gg.reset();
    }
    const GpuGeom* p = gg.get();
    gpu_geoms_[type] = std::move(gg);
    return p;
}

void SpriteCache::Geom_BBox(const VxlGpuGeom& g, float yaw, float bbox[4],
                            float depth[2]) {
    const float k = 0.70710678f;
    const float yc = std::cos(yaw);
    const float ys = std::sin(yaw);
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
    float dmin = 1e30f, dmax = -1e30f;
    for (const VxlGpuLimb& L : g.limbs) {
        // 肢体 AABB 的 8 个角就够了：这是保守上界，多出来的只是画布边上的
        // 一圈透明像素，落点由模型原点决定、不受影响。
        for (int ci = 0; ci < 8; ++ci) {
            const float lx = (ci & 1) ? L.max_b[0] : L.min_b[0];
            const float ly = (ci & 2) ? L.max_b[1] : L.min_b[1];
            const float lz = (ci & 4) ? L.max_b[2] : L.min_b[2];
            const float px = L.m[0] * lx + L.m[1] * ly + L.m[2] * lz + L.m[3];
            const float py = L.m[4] * lx + L.m[5] * ly + L.m[6] * lz + L.m[7];
            const float pz = L.m[8] * lx + L.m[9] * ly + L.m[10] * lz + L.m[11];
            const float wx = yc * px - ys * py;
            const float wy = ys * px + yc * py;
            const float sx = (wx - wy) * k;
            const float sy = (wx + wy) * k * 0.5f - pz;
            const float dp = wx + wy + pz;
            if (sx < x0) x0 = sx;
            if (sx > x1) x1 = sx;
            if (sy < y0) y0 = sy;
            if (sy > y1) y1 = sy;
            if (dp < dmin) dmin = dp;
            if (dp > dmax) dmax = dp;
        }
    }
    bbox[0] = x0;
    bbox[1] = y0;
    bbox[2] = x1;
    bbox[3] = y1;
    depth[0] = dmin;
    depth[1] = dmax;
}

bool SpriteCache::Build_Voxel_Gpu(const char* type, const UnitModel& um, float yaw,
                                  int house_color, ObjectSprite* out) {
    if (renderer_ == nullptr) {
        return false;
    }
    const GpuGeom* gg = Ensure_Geom(type, um);
    if (gg == nullptr) {
        return false;
    }
    float bbox[4] = {};
    float depth[2] = {};
    Geom_BBox(gg->geom, yaw, bbox, depth);

    // 调色板：VXL 自带 768（已经是 8 位），再按阵营色把 16..31 整段换掉
    uint8_t pal768[768];
    std::memcpy(pal768, gg->base_pal, 768);
    remap_.Make_Palette768(pal768, true, gg->remap_start, gg->remap_end,
                           house_color, pal768);

    // 光影：和原版同一套（VoxelLight 的算法是从 gamemd.exe 抄的）。
    // 光向量默认 normalize(1,1,2) 就是原版那个太阳方位，别乱改。
    VoxelLight light;
    VoxelBakeParams p;
    p.yaw = yaw;
    p.scale = kVoxelScale;
    p.bbox = bbox;
    p.depth_range = depth;
    p.pal768 = pal768;
    p.light = light.light;
    p.ambient = light.ambient;
    p.diffuse = light.diffuse;
    p.levels = static_cast<float>(light.levels);

    int w = 0, h = 0;
    const int id = renderer_->Bake_Voxels(gg->id, p, &w, &h);
    if (id < 0 || w <= 0 || h <= 0) {
        return false;
    }
    out->sprite_id = id;
    out->w = w;
    out->h = h;
    for (int i = 0; i < 4; ++i) {
        out->bbox[i] = bbox[i];
    }
    out->scale = kVoxelScale;
    // 落点：模型原点投影后恒为 (0,0)，所以精灵左上角要放在
    // 格心 + (bbox[0],bbox[1])×scale 的位置。
    out->off_x = static_cast<int>(bbox[0] * kVoxelScale);
    out->off_y = static_cast<int>(bbox[1] * kVoxelScale);
    out->ok = true;
    return true;
}

// ---------------------------------------------------------------------------
// CPU 参考图（只给 --vxlgpu 自检用）
// ---------------------------------------------------------------------------

bool SpriteCache::Is_Voxel(const char* type) {
    const UnitModel* um = models_.Resolve(type);
    return um != nullptr && um->voxel;
}

bool SpriteCache::Bake_CPU_Reference(const char* type, float yaw, int house_color,
                                     std::vector<uint8_t>* rgba, int* w, int* h,
                                     float* out_x0, float* out_y0) {
    const UnitModel* um = models_.Resolve(type);
    if (um == nullptr || !um->voxel) {
        return false;
    }
    VoxelModel vm;
    if (!Load_Voxel_Model(*um, &vm)) {
        return false;
    }
    float model_xform[12];
    const float c = std::cos(yaw);
    const float s = std::sin(yaw);
    model_xform[0] =  c; model_xform[1] = -s; model_xform[2] = 0.0f; model_xform[3] = 0.0f;
    model_xform[4] =  s; model_xform[5] =  c; model_xform[6] = 0.0f; model_xform[7] = 0.0f;
    model_xform[8] = 0.0f; model_xform[9] = 0.0f; model_xform[10] = 1.0f; model_xform[11] = 0.0f;

    // 画布原点用 GPU 那条路的（每根肢体 AABB 的 8 个角求出的保守上界）。
    // 不统一原点的话，两张图的像素格相位差一个非整数，逐像素比出来的差异
    // 全来自对位，真正的内容差异反而看不见 —— 这是这个自检最容易白忙一场的地方。
    VxlGpuGeom gpu_geom;
    float bbox[4] = {0, 0, 0, 0};
    const bool have_bbox =
        vm.body->Build_GPU_Geom(vm.pose_ptr, vm.attach, vm.attach_count, yaw,
                                &gpu_geom);
    if (have_bbox) {
        std::memcpy(bbox, gpu_geom.bbox, sizeof(bbox));
    }

    VoxelLight light;
    std::vector<uint8_t> indexed, shade;
    int ww = 0, hh = 0;
    if (!vm.body->Render_Isometric(&indexed, &ww, &hh, kVoxelScale, vm.pose_ptr,
                                   vm.attach, vm.attach_count, &light, &shade,
                                   nullptr, 0.0f, nullptr, model_xform, out_x0,
                                   out_y0, have_bbox ? bbox : nullptr)) {
        return false;
    }
    if (ww <= 0 || hh <= 0) {
        return false;
    }
    uint8_t pal768[768];
    std::memcpy(pal768, vm.body->Palette(), 768);
    remap_.Make_Palette768(pal768, true, vm.body->Remap_Start(),
                           vm.body->Remap_End(), house_color, pal768);
    Shade_To_RGBA(indexed.data(), shade.data(), ww * hh, pal768, light, rgba);
    *w = ww;
    *h = hh;
    return true;
}

// ---------------------------------------------------------------------------
// SHP 单位（步兵 / 建筑 / 装饰）
// ---------------------------------------------------------------------------

const uint8_t* SpriteCache::Theater_Palette_Data() {
    if (!theater_pal_tried_) {
        theater_pal_tried_ = true;
        std::vector<uint8_t> data;
        for (MixFileClass* m : roots_) {
            data = m->Read_Deep(Theater_Palette(theater_));
            if (data.size() >= 768) {
                break;
            }
            data.clear();
        }
        theater_pal_.assign(768, 0);
        if (data.size() >= 768) {
            std::memcpy(theater_pal_.data(), data.data(), 768);
        } else {
            // 拿不到剧场调色板就退成灰度，至少能看出形状
            for (int i = 0; i < 256; ++i) {
                theater_pal_[static_cast<size_t>(i) * 3 + 0] =
                    theater_pal_[static_cast<size_t>(i) * 3 + 1] =
                        theater_pal_[static_cast<size_t>(i) * 3 + 2] =
                            static_cast<uint8_t>(i);
            }
        }
    }
    return theater_pal_.data();
}

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

    // 调色板：剧场 .pal（整局只读一次，见 Theater_Palette_Data）
    uint8_t pal768[768];
    std::memcpy(pal768, Theater_Palette_Data(), 768);
    // .PAL 文件是 6 位分量，要展开成 8 位（expanded=false）
    remap_.Make_Palette768(pal768, false, 16, 31, house_color, pal768);

    // SHP 是"画好的位图"，没有投影 / 明暗这些可搬的活，所以这里的
    // 索引->RGBA 就是全部工作量，留在 CPU 上做（一次几十 KB，不值得上 GPU）。
    std::vector<uint8_t> rgba;
    Index_To_RGBA(px.data(), fi.w * fi.h, pal768, &rgba);
    if (renderer_ == nullptr) {
        return false;
    }
    const int id = renderer_->Upload_Sprite_RGBA(rgba.data(), fi.w, fi.h);
    if (id < 0) {
        return false;
    }
    out->sprite_id = id;
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
    if (um != nullptr && um->voxel && renderer_ != nullptr) {
        const float yaw = static_cast<float>(key.step) * 6.28318530718f /
                          static_cast<float>(kFacingSteps);
        ok = Build_Voxel_Gpu(type, *um, yaw, house_color, sp.get());
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
