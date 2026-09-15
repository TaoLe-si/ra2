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

/// 朝向档数。8 档 = 每 45 度一张。
constexpr int kFacingSteps = 8;

/// 一张渲染好的精灵，以及它相对"格子中心"的落点偏移。
struct ObjectSprite {
    std::vector<uint8_t> rgba;   ///< RGBA8，行优先
    int w = 0, h = 0;
    /// 精灵左上角相对格子中心的偏移（像素，未缩放）。
    /// 体素模型不是以格心为原点的，不减这个会整体偏出去。
    int off_x = 0, off_y = 0;
    bool ok = false;
};

class SpriteCache {
public:
    /// 绑定素材源。会打开所有嵌套子归档建 ID 索引（慢，只做一次）。
    bool Bind(const std::vector<MixFileClass*>& roots);

    /// 设阵营色表（rules.ini 的 [Colors]）。
    void Set_Remap(const RemapTable& remap) { remap_ = remap; }
    /// 剧场名（URBAN/TEMPERATE/...），决定 SHP 用哪个 .pal。
    void Set_Theater(const std::string& t) { theater_ = t; }

    /// 取一张精灵。facing 是 0..255（原版 256 分度），内部量化成 8 档。
    /// 返回的指针缓存在内部，下次同 key 直接命中。**不要在外面改它**。
    const ObjectSprite* Get(const char* type, int facing, int house_color);

    /// 单位模型库（给外壳打印诊断用）。
    const UnitModelDB& Models() const noexcept { return models_; }

    /// 每帧允许新构建几张精灵。
    ///
    /// 为什么需要：一辆坦克的体素是 8 万个，解码 + 等距光栅化是毫秒级。
    /// 一帧里几十个不同单位同时第一次出现，就会直接卡住几百毫秒。
    /// 原版是进图时预渲好的，我们做成"用到了才渲"，所以必须限流：
    /// 超预算的这一帧先退化成色块，下一帧继续补，几帧内自然补齐。
    void Reset_Budget(int per_frame = 4) { budget_left_ = per_frame; }
    int Budget_Left() const noexcept { return budget_left_; }

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

    bool Build_Voxel(const UnitModel& um, float yaw, int house_color,
                     ObjectSprite* out);
    bool Build_Shp(const char* image, int house_color, ObjectSprite* out);

    /// 调色板 -> RGBA 查表。remap 已经合进 pal768。
    static void Index_To_RGBA(const uint8_t* indexed, int n,
                              const uint8_t* pal768, std::vector<uint8_t>* out);

    std::vector<MixFileClass*> roots_;
    UnitModelDB models_;
    RemapTable remap_;
    std::string theater_;

    // 素材缓存：同一份 VXL/HVA/SHP 会被多个朝向复用，别重复解
    std::unordered_map<uint32_t, std::unique_ptr<VxlFile>> vxl_cache_;
    std::unordered_map<uint32_t, std::unique_ptr<HvaFile>> hva_cache_;

    std::unordered_map<Key, std::unique_ptr<ObjectSprite>, KeyHash> cache_;
    int built_ = 0;
    int failed_ = 0;
    int budget_left_ = 4;
    std::string last_failure_;
};

}  // namespace ra2
