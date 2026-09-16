// ObjectSprite.cpp

#include "gfx/ObjectSprite.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>

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

/// 地形 TMP / 装饰 SHP 用的 iso 调色板。和 MapRenderer 那张表一致。
const char* Iso_Palette(const std::string& theater) {
    if (theater == "URBAN")     return "isourb.pal";
    if (theater == "SNOW")      return "isosno.pal";
    if (theater == "DESERT")    return "isodes.pal";
    if (theater == "LUNAR")     return "isolun.pal";
    if (theater == "NEWURBAN")  return "isoubn.pal";
    return "isotem.pal";
}

/// Theater=yes 的 SHP 不叫 .SHP，后缀是剧场扩展名（TREE01.urb）。
const char* Theater_Ext(const std::string& theater) {
    if (theater == "URBAN")     return "urb";
    if (theater == "SNOW")      return "sno";
    if (theater == "DESERT")    return "des";
    if (theater == "LUNAR")     return "lun";
    if (theater == "NEWURBAN")  return "ubn";
    return "tem";
}

/// NewTheater=yes：文件名第 2 个字母换成剧场字母（CAAIRP→CUAIRP）。
/// 与 MapRenderer / Overlay 同一套（URBAN=U, SNOW=A, …）。
char Theater_New_Letter(const std::string& theater) {
    if (theater == "URBAN") {
        return 'U';
    }
    if (theater == "SNOW") {
        return 'A';
    }
    if (theater == "DESERT") {
        return 'D';
    }
    if (theater == "LUNAR") {
        return 'L';
    }
    if (theater == "NEWURBAN") {
        return 'N';
    }
    if (theater == "TEMPERATE") {
        return 'T';
    }
    return 'T';
}

std::string New_Theater_Name(const std::string& image, const std::string& theater) {
    if (image.size() < 2) {
        return image;
    }
    std::string out = image;
    out[1] = Theater_New_Letter(theater);
    return out;
}

std::vector<uint8_t> Read_Named(const std::vector<MixFileClass*>& roots,
                                const char* filename) {
    for (MixFileClass* m : roots) {
        std::vector<uint8_t> data = m->Read_Deep(filename);
        if (!data.empty()) {
            return data;
        }
    }
    return {};
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
    // 光向量与 Bake / VxlFile 阴影同一套（VoxelLight 默认）。
    VoxelLight light;
    const float lx = light.light[0], ly = light.light[1], lz = light.light[2];
    constexpr float kGroundZ = 0.0f;
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
    float dmin = 1e30f, dmax = -1e30f;
    for (const VxlGpuLimb& L : g.limbs) {
        // 肢体 AABB 的 8 个角就够了：这是保守上界，多出来的只是画布边上的
        // 一圈透明像素，落点由模型原点决定、不受影响。
        for (int ci = 0; ci < 8; ++ci) {
            const float lx0 = (ci & 1) ? L.max_b[0] : L.min_b[0];
            const float ly0 = (ci & 2) ? L.max_b[1] : L.min_b[1];
            const float lz0 = (ci & 4) ? L.max_b[2] : L.min_b[2];
            const float px = L.m[0] * lx0 + L.m[1] * ly0 + L.m[2] * lz0 + L.m[3];
            const float py = L.m[4] * lx0 + L.m[5] * ly0 + L.m[6] * lz0 + L.m[7];
            const float pz = L.m[8] * lx0 + L.m[9] * ly0 + L.m[10] * lz0 + L.m[11];
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
            // 阴影落点也要进 bbox，否则投到模型外的影子被裁掉（VxlFile 同款）。
            float qx = wx, qy = wy;
            if (lz > 1e-4f) {
                const float t = (pz - kGroundZ) / lz;
                qx = wx - lx * t;
                qy = wy - ly * t;
            }
            const float gx = (qx - qy) * k;
            const float gy = (qx + qy) * k * 0.5f - kGroundZ;
            if (gx < x0) x0 = gx;
            if (gx > x1) x1 = gx;
            if (gy < y0) y0 = gy;
            if (gy > y1) y1 = gy;
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
    // 校车等大体素按新基准不再需要钳制 —— 48vox/格 的定标本身就把
    // 所有模型放回"原版占用格数"的比例。BUS 也就 ~2 格宽，与原版一致。
    // （旧的 160px 钳制是 8.0 比例时代的补丁，定标修正后删掉。）
    float scale = kVoxelScale;
    VoxelBakeParams p;
    p.yaw = yaw;
    p.scale = scale;
    p.bbox = bbox;
    p.depth_range = depth;
    p.pal768 = pal768;
    p.light = light.light;
    p.ambient = light.ambient;
    p.diffuse = light.diffuse;
    p.levels = static_cast<float>(light.levels);
    // 地面投影阴影：算法对齐 VxlFile（沿 -L 投到 z=0），GPU 两趟烘焙。
    p.shadow = true;
    VoxelShadow sh;
    p.shadow_alpha = sh.alpha;
    p.ground_z = sh.ground_z;

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
    out->scale = scale;
    // 落点：模型原点投影后恒为 (0,0)，所以精灵左上角要放在
    // 格心 + (bbox[0],bbox[1])×scale 的位置。
    out->off_x = static_cast<int>(bbox[0] * scale);
    out->off_y = static_cast<int>(bbox[1] * scale);
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

const uint8_t* SpriteCache::Iso_Palette_Data() {
    if (!iso_pal_tried_) {
        iso_pal_tried_ = true;
        std::vector<uint8_t> data;
        for (MixFileClass* m : roots_) {
            data = m->Read_Deep(Iso_Palette(theater_));
            if (data.size() >= 768) {
                break;
            }
            data.clear();
        }
        iso_pal_.assign(768, 0);
        if (data.size() >= 768) {
            std::memcpy(iso_pal_.data(), data.data(), 768);
        } else {
            for (int i = 0; i < 256; ++i) {
                iso_pal_[static_cast<size_t>(i) * 3 + 0] =
                    iso_pal_[static_cast<size_t>(i) * 3 + 1] =
                        iso_pal_[static_cast<size_t>(i) * 3 + 2] =
                            static_cast<uint8_t>((i >> 2) & 0x3F);
            }
        }
    }
    return iso_pal_.data();
}

bool SpriteCache::Build_Shp(const char* image, int house_color, bool facing_frames,
                            int facing, int foot_w, int foot_h, bool house_remap,
                            bool use_unit_pal, ObjectSprite* out, int phase) {
    std::string file = std::string(image) + ".SHP";
    std::vector<uint8_t> data = Read_Named(roots_, file.c_str());
    if (data.empty()) {
        file = std::string(image) + "." + Theater_Ext(theater_);
        data = Read_Named(roots_, file.c_str());
    }
    if (data.empty()) {
        return false;
    }
    ShpFile shp;
    if (!shp.Load(data.data(), data.size()) || shp.Frame_Count() <= 0) {
        return false;
    }
    // 【帧不是随便挑的】SHP 有多类多帧布局（实测 GI.SHP 744 帧）：
    //   * 步兵（帧多）：[0..7] 站立 8 朝向；[8..55] 行走 8 朝向 × 6 相位；
    //     [56..] 趴下/开火/死亡等（**绝不能按旧式 facing*总数/256 摊平** ——
    //     那会随机抽到趴下和死亡帧）。
    //   * 简单朝向件（帧少）：帧 = 朝向。
    //   * 建筑/装饰：帧 = 正常态/损毁态，恒 0。
    // 步兵的行走相位由调用方传：phase=0 站立，1..5 行走。
    int frame = 0;
    if (facing_frames && shp.Frame_Count() > 1) {
        const int dir = ((facing + 16) & 255) >> 5;   // 8 朝向，0 = 北
        if (shp.Frame_Count() >= 56) {
            // 标准步兵布局（站 8 + 走 48）。
            frame = (phase > 0) ? (8 + dir * 6 + (phase - 1) % 6) : dir;
            if (frame >= shp.Frame_Count()) {
                frame = dir;
            }
        } else {
            // 小 SHP（雷达指针之类）：帧 = 朝向。
            frame = (facing * shp.Frame_Count()) / 256;
            if (frame >= shp.Frame_Count()) {
                frame = shp.Frame_Count() - 1;
            }
        }
    }
    const std::vector<uint8_t>& px = shp.Frame_Pixels(frame);
    const ShpFrameInfo& fi = shp.Frame_Info(frame);
    if (fi.w <= 0 || fi.h <= 0 || px.size() < static_cast<size_t>(fi.w) * fi.h) {
        return false;
    }

    uint8_t pal768[768];
    if (use_unit_pal) {
        // 建筑/单位：unit*.pal。Remapable=no 时不解阵营色（CATHOSP 等）。
        std::memcpy(pal768, Theater_Palette_Data(), 768);
        if (house_remap) {
            remap_.Make_Palette768(pal768, false, 16, 31, house_color, pal768);
        } else {
            Expand_Pal768(pal768, pal768);
        }
    } else {
        // 地形装饰：iso*.pal
        std::memcpy(pal768, Iso_Palette_Data(), 768);
        Expand_Pal768(pal768, pal768);
    }

    // 与 GameShell UI / CC_Draw_Shape 同一套画布语义：帧像素落到 SHP 整幅
    // (Width×Height) 的 (fi.x, fi.y)，不能只上传紧裁矩形，否则锚点漂、
    // 多格建筑拼缝错位（Arena 体育场那种"撕碎"）。
    const int cw = shp.Width();
    const int ch = shp.Height();
    if (cw <= 0 || ch <= 0) {
        return false;
    }
    std::vector<uint8_t> rgba(static_cast<size_t>(cw) * static_cast<size_t>(ch) * 4, 0);
    for (int y = 0; y < fi.h; ++y) {
        for (int x = 0; x < fi.w; ++x) {
            const uint8_t idx = px[static_cast<size_t>(y) * fi.w + x];
            if (idx == 0) {
                continue;
            }
            const int dx = static_cast<int>(fi.x) + x;
            const int dy = static_cast<int>(fi.y) + y;
            if (dx < 0 || dy < 0 || dx >= cw || dy >= ch) {
                continue;
            }
            const size_t p = (static_cast<size_t>(dy) * cw + dx) * 4;
            const size_t c = static_cast<size_t>(idx) * 3;
            rgba[p + 0] = pal768[c + 0];
            rgba[p + 1] = pal768[c + 1];
            rgba[p + 2] = pal768[c + 2];
            rgba[p + 3] = 255;
        }
    }
    if (renderer_ == nullptr) {
        return false;
    }
    const int id = renderer_->Upload_Sprite_RGBA(rgba.data(), cw, ch);
    if (id < 0) {
        return false;
    }
    out->sprite_id = id;
    out->w = cw;
    out->h = ch;
    // 锚点：画布中心（与原版 Draw_Shape 以 shape 宽高为参照一致）。
    out->off_x = -cw / 2;
    out->off_y = -ch / 2;
    // 建筑的 (x,y) 是**左上格**：把锚点挪到足迹中心。
    // +1 格 x 在屏幕上是 (+30,+15)，+1 格 y 是 (-30,+15)。
    if (foot_w > 1 || foot_h > 1) {
        const float cx = static_cast<float>(foot_w - 1) * 0.5f;
        const float cy = static_cast<float>(foot_h - 1) * 0.5f;
        out->off_x += static_cast<int>((cx - cy) * 30.0f);
        out->off_y += static_cast<int>((cx + cy) * 15.0f);
    }
    out->ok = true;
    return true;
}

// ---------------------------------------------------------------------------
// Get
// ---------------------------------------------------------------------------

const ObjectSprite* SpriteCache::Get(const char* type, int facing, int house_color,
                                     int phase) {
    Key key;
    key.type = type;
    key.step = (facing * kFacingSteps) / 256;
    if (key.step < 0) key.step = 0;
    if (key.step >= kFacingSteps) key.step = kFacingSteps - 1;
    key.color = house_color;
    key.phase = phase;

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
    // Techno（category>=0）用 unit*.pal；地形装饰用 iso*.pal。
    const bool use_unit_pal = (um != nullptr && um->category >= 0);
    const bool house_remap =
        use_unit_pal && (um == nullptr || um->remapable);
    if (um != nullptr && um->voxel && renderer_ != nullptr) {
        const float yaw = static_cast<float>(key.step) * 6.28318530718f /
                          static_cast<float>(kFacingSteps);
        ok = Build_Voxel_Gpu(type, *um, yaw, house_color, sp.get());
    }
    if (!ok) {
        // 不是体素（步兵/建筑/装饰），或者体素没渲出来，退到 SHP
        std::string image =
            (um != nullptr && !um->image.empty()) ? um->image : std::string(type);
        // NewTheater=yes：先试剧场字母文件名（CUAIRP），再退回原名（CAAIRP）。
        if (um != nullptr && um->new_theater) {
            image = New_Theater_Name(image, theater_);
        }
        // 步兵的 SHP 帧是朝向，要按 facing 选；建筑/装饰的帧是正常/损毁态，不能选。
        const bool face_frames = (um != nullptr && um->category == 2);
        const int foot_w = (um != nullptr) ? um->width : 1;
        const int foot_h = (um != nullptr) ? um->height : 1;
        ok = Build_Shp(image.c_str(), house_color, face_frames,
                       key.step * (256 / kFacingSteps), foot_w, foot_h,
                       house_remap, use_unit_pal, sp.get(), key.phase);
        if (!ok && um != nullptr && um->new_theater && !um->image.empty() &&
            image != um->image) {
            ok = Build_Shp(um->image.c_str(), house_color, face_frames,
                           key.step * (256 / kFacingSteps), foot_w, foot_h,
                           house_remap, use_unit_pal, sp.get(), key.phase);
        }
    }
    if (!ok) {
        // 同名 SHP 再试一次（有些单位的 Image= 和原名都不带 .SHP 后缀命中）
        ok = Build_Shp(type, house_color, false, 0, 1, 1, house_remap,
                       use_unit_pal, sp.get());
    }

    ++built_;
    if (!ok) {
        ++failed_;
        last_failure_ = type;
        // 只报前若干个，免得几百个装饰物刷屏
        if (failed_ <= 8) {
            std::printf("    精灵缺失: %s\n", type);
        }
    }
    sp->ok = ok;
    const ObjectSprite* p = sp.get();
    cache_[key] = std::move(sp);
    return ok ? p : nullptr;
}

}  // namespace ra2
