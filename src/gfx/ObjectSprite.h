// ObjectSprite.h -- 把"单位名 + 朝向 + 阵营色"变成一张能贴的 RGBA 图
//
// 为什么单独一层：战场上有几百个对象，同一个单位会重复出现几十次。
// 每次都重新解 VXL / SHP 再过一遍调色板是不现实的（实测一辆坦克
// 光解码就 8 万个体素）。所以这里按 (类型, 朝向档, 阵营色) 做缓存，
// 同一个 key 只算一次。
//
// 【两类素材，两条路】
//   * 载具（Voxel=yes）走 VXL：真 3D 体素，能任意朝向，自带 768 调色板。
//   * 步兵 / 建筑 / 装饰走 SHP：画好的位图，调色板要从剧场 .pal 取。
//  判据在 UnitModelDB 里（art[image].Voxel=yes），不要在这里重新发明。
//
// 【阵营色】两种方式殊途同归：
//   VXL 自带调色板里 16..31 是御主占位色，SHP 用剧场调色板里同样一段。
//   RemapTable::Make_Palette768 会按 [Colors] 把这段整段替换掉。
//
// 【朝向】原版是 32 向。全量预渲 32×86 个单位太贵，这里量化成 8 档
//   （kFacingSteps）。等做成按需后台预渲再提到 32。

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "data/UnitModel.h"
#include "gfx/HvaFile.h"
#include "gfx/RemapTable.h"
#include "gfx/VxlFile.h"
#include "io/FileSystem.h"

namespace ra2 {

class Dx12Renderer;   ///< 只存指针，不在头文件里拖进 d3d12.h

/// 朝向档数。8 档 = 每 45 度一张。
constexpr int kFacingSteps = 8;

/// 一个体素占几个像素。一格 60px 宽、体素单位 det=1/12，8 倍刚好让
/// 一辆坦克占满一格多一点。等 rules 的 Size= 接进来再按真值调。
constexpr float kVoxelScale = 8.0f;

/// 一张渲染好的精灵，以及它相对"格子中心"的落点偏移。
///
/// 精灵本体**在显存里**（sprite_id 是渲染器的句柄），CPU 侧不存像素。
/// 体素单位是 GPU 直接光栅化出来的（Dx12Renderer::Bake_Voxels），
/// SHP 单位是 CPU 解完索引图上传的 —— 后者只是位图，本来就没有
/// "投影 / 明暗"这些可搬的活。
struct ObjectSprite {
    int sprite_id = -1;   ///< 渲染器里的精灵句柄，-1 = 没素材
    int w = 0, h = 0;
    /// 精灵左上角相对"模型原点投影点"的偏移（像素，未缩放）。
    /// 体素路径下恒等于 (bbox[0]*scale, bbox[1]*scale)，是负的；
    /// 画的时候加到格心屏幕坐标上，模型原点就正好落在格心。
    /// 关键是它**只跟投影有关、跟画布大小无关**，所以 bbox 取保守上界
    /// 也不会让单位浮空或者左右晃。
    int off_x = 0, off_y = 0;
    /// 体素精灵的画布范围（投影坐标 x0,y0,x1,y1）+ 用的 scale。
    /// SHP 精灵不用（它是位图，没有"投影"这回事）。
    /// 自检要靠 bbox[0]/bbox[1] 把 GPU 图和 CPU 参考图对位。
    float bbox[4] = {0, 0, 0, 0};
    float scale = 1.0f;
    bool ok = false;
};

class SpriteCache {
public:
    /// 绑定素材源。会打开所有嵌套子归档建 ID 索引（慢，只做一次）。
    bool Bind(const std::vector<MixFileClass*>& roots);

    /// 绑定渲染器。体素精灵是在 GPU 上烘出来的，没有渲染器就只能退 SHP。
    void Set_Renderer(Dx12Renderer* r) { renderer_ = r; }

    /// 设阵营色表（rules.ini 的 [Colors]）。
    void Set_Remap(const RemapTable& remap) { remap_ = remap; }
    /// 剧场名（URBAN/TEMPERATE/...），决定 SHP 用哪个 .pal。
    /// 换了剧场就把调色板缓存作废（同名的重复设置不算换）。
    void Set_Theater(const std::string& t) {
        if (t != theater_) {
            theater_pal_tried_ = false;
        }
        theater_ = t;
    }

    /// 取一张精灵。facing 是 0..255（原版 256 分度），内部量化成 8 档。
    /// 返回的指针缓存在内部，下次同 key 直接命中。**不要在外面改它**。
    const ObjectSprite* Get(const char* type, int facing, int house_color);

    /// 单位模型库（给外壳打印诊断用）。
    const UnitModelDB& Models() const noexcept { return models_; }

    /// 这个类型是不是体素单位（决定走 GPU 烘焙还是 SHP 上传）。
    bool Is_Voxel(const char* type);

    /// 诊断用：把同一个模型用**旧的 CPU 软光栅**再渲一遍。
    ///
    /// 存在的唯一理由是给 `ra2game --vxlgpu` 当参考图 —— GPU 路径是新写的，
    /// 必须证明它和已经验过的 CPU 路径像素一致，不然"搬上 GPU"就是一句空话。
    /// out_x0/out_y0 是画布原点对应的投影坐标，两张图靠它对位。
    bool Bake_CPU_Reference(const char* type, float yaw, int house_color,
                            std::vector<uint8_t>* rgba, int* w, int* h,
                            float* out_x0, float* out_y0);

    /// 每帧允许新构建几张精灵。
    ///
    /// 为什么需要：一辆坦克的体素是 8 万个，解码 + 等距光栅化是毫秒级。
    /// 一帧里几十个不同单位同时第一次出现，就会直接卡住几百毫秒。
    /// 原版是进图时预渲好的，我们做成"用到了才渲"，所以必须限流：
    /// 超预算的这一帧先退化成色块，下一帧继续补，几帧内自然补齐。
    void Reset_Budget(int per_frame = 4) { budget_left_ = per_frame; }
    int Budget_Left() const noexcept { return budget_left_; }

    /// 索引图 -> RGBA8（索引 0 透明）。界面贴图（SIDE1/TAB/RADAR…）也用它，
    /// 所以放成公开的 —— 那些件不走精灵缓存，但上色规则必须一致。
    static void Index_To_RGBA(const uint8_t* indexed, int n,
                              const uint8_t* pal768, std::vector<uint8_t>* out);

    /// 把一个 .PAL（6 位分量）展开成 768 字节 8 位分量。
    static void Expand_Pal768(const uint8_t* pal6, uint8_t* out768);

    /// 诊断：算过多少张、其中失败多少。
    int Built() const noexcept { return built_; }
    int Failed() const noexcept { return failed_; }
    /// 上一次失败的类型的名字（调试用）。
    const std::string& Last_Failure() const noexcept { return last_failure_; }

private:
    struct Key {
        std::string type;
        int step;
        int color;
        bool operator==(const Key& o) const {
            return step == o.step && color == o.color && type == o.type;
        }
    };
    struct KeyHash {
        size_t operator()(const Key& k) const {
            return std::hash<std::string>()(k.type) ^ (static_cast<size_t>(k.step) << 8) ^
                   (static_cast<size_t>(k.color) << 16);
        }
    };

    /// 一个体素模型的"原始素材"：车体 + 姿态 + 炮塔/炮管。
    struct VoxelModel {
        const VxlFile* body = nullptr;
        std::vector<float> pose;
        const float* pose_ptr = nullptr;
        VxlAttach attach[2];
        int attach_count = 0;
        std::unique_ptr<VxlFile> owned[2];   ///< 炮塔 / 炮管，撑住 attach 里的指针
    };
    bool Load_Voxel_Model(const UnitModel& um, VoxelModel* out);

    /// 一个类型的 GPU 几何。**只建一次**（与朝向无关），之后 8 向 32 向白送。
    struct GpuGeom {
        int id = -1;
        VxlGpuGeom geom;             ///< 留着给 Geom_BBox 算画布
        uint8_t base_pal[768] = {};  ///< VXL 自带调色板（还没做 remap）
        int remap_start = 16, remap_end = 31;
    };
    /// 返回缓存里的几何；失败返回 nullptr（并记下来，下次不再重试）。
    /// 返回的指针指向缓存内部，**不要改、不要存过帧**。
    const GpuGeom* Ensure_Geom(const char* type, const UnitModel& um);

    /// 按给定朝向算画布范围与深度范围（每根肢体 AABB 的 8 个角，O(肢体数)）。
    static void Geom_BBox(const VxlGpuGeom& g, float yaw, float bbox[4],
                          float depth[2]);

    bool Build_Voxel_Gpu(const char* type, const UnitModel& um, float yaw,
                         int house_color, ObjectSprite* out);
    /// facing_frames 为真时按朝向选帧（步兵的 SHP 帧就是 8 个朝向）；
    /// 建筑/装饰为假（帧是"正常/损毁"，不是朝向，按朝向选会抽到损毁帧）。
    /// foot_w/foot_h 是占地格数：地图里建筑的 (x,y) 是**左上格**，
    /// 大建筑要把锚点挪到足迹中心，否则整座城都偏一格。
    bool Build_Shp(const char* image, int house_color, bool facing_frames,
                   int facing, int foot_w, int foot_h, ObjectSprite* out);

    /// 剧场单位调色板（768 字节，8 位，**还没做 remap**）。
    ///
    /// 【缓存它的理由】它整局都不变，但每次 Build_Shp 都要在几百 MB 的 MIX 里
    /// 找一遍 —— SHP 单位有 8 个朝向，一个类型就是 8 次全量扫描。
    /// 实测这就是"一帧烘 4 张精灵要好几秒"的大头。
    /// 找不到时留 768 个 0，并且记下来不再重试。
    const uint8_t* Theater_Palette_Data();

    std::vector<MixFileClass*> roots_;
    UnitModelDB models_;
    RemapTable remap_;
    std::string theater_;
    Dx12Renderer* renderer_ = nullptr;
    std::vector<uint8_t> theater_pal_;
    bool theater_pal_tried_ = false;

    // 素材缓存：同一份 VXL/HVA/SHP 会被多个朝向复用，别重复解
    std::unordered_map<uint32_t, std::unique_ptr<VxlFile>> vxl_cache_;
    std::unordered_map<uint32_t, std::unique_ptr<HvaFile>> hva_cache_;
    std::unordered_map<std::string, std::unique_ptr<GpuGeom>> gpu_geoms_;

    std::unordered_map<Key, std::unique_ptr<ObjectSprite>, KeyHash> cache_;
    int built_ = 0;
    int failed_ = 0;
    int budget_left_ = 4;
    std::string last_failure_;
};

}  // namespace ra2
