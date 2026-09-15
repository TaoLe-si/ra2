// main.cpp -- 冒烟测试：验证"并行结果与串行逐条一致"
//
// 这个测试是整个多核改造的验收基准：
// 只要它挂了，就说明某个并行改动破坏了确定性，联机必然失步。
//
// 构建： cmake -B build && cmake --build build
// 运行： ./build/ra2core

#include <cstdio>
#include <random>
#include <vector>

#include "ai/PathFinder.h"
#include "core/VTableMap.h"
#include "re/ObjectSizes.h"
#include "engine/FrameQueue.h"
#include "io/FileSystem.h"
#include "io/MixCrypto.h"
#include "map/Map.h"
#include "threading/TaskSystem.h"

using namespace ra2;

namespace {

bool SameResult(const PathResult& a, const PathResult& b) {
    if (a.found != b.found || a.cost != b.cost || a.used_hierarchical != b.used_hierarchical) {
        return false;
    }
    if (a.waypoints.size() != b.waypoints.size()) {
        return false;
    }
    for (size_t i = 0; i < a.waypoints.size(); ++i) {
        if (a.waypoints[i] != b.waypoints[i]) {
            return false;
        }
    }
    return true;
}

}  // namespace

// 传入真实 MIX 文件路径，验证加密 MIX 的解密链路是否完好。
// 例：  ra2core.exe D:\westwood\RA2YR\ra2md.mix
//
// 这是整个工程最有价值的一条回归测试：它同时验证了
//   RSA 密钥派生、Blowfish 解密、索引布局、Westwood CRC、嵌套 MIX
// 五处实现。任何一处错了，(条目数, 数据区长度) 就对不上，或者 CRC 反查失败。
static int Verify_Mix(const char* path) {
    // CRC 算法的实测锚点：这两个名字的 CRC 是从真实 ra2md.mix 里撞出来的。
    struct Anchor { const char* name; uint32_t crc; };
    const Anchor anchors[] = {
        {"LOCALMD.MIX", 0xFBE0D09D},
        {"CACHEMD.MIX", 0x68EEC99A},
        {"AIMD.INI", 0x116F3F76},
        {"MNBTTN.SHP", 0x1BB65278},
    };
    for (const Anchor& a : anchors) {
        const uint32_t got = Westwood_CRC(a.name);
        if (got != a.crc) {
            std::printf("FAIL Westwood CRC：%s 算出 0x%08X，应为 0x%08X\n",
                        a.name, got, a.crc);
            return 1;
        }
    }
    std::printf("OK   Westwood CRC 通过 %d 个实测锚点\n",
                static_cast<int>(sizeof(anchors) / sizeof(anchors[0])));

    MixFileClass mix;
    if (!mix.Open(path)) {
        std::printf("FAIL MIX 打不开 %s（flags=%s）\n", path,
                    MixFileClass::Describe_Flags(mix.Flags()).c_str());
        return 1;
    }

    int bad = 0;
    const bool ok = mix.Validate_Index(&bad);
    std::printf("MIX  %s\n     flags=%s 条目=%d 数据区=%llu 起=%llu %s（越界 %d）\n",
                path, MixFileClass::Describe_Flags(mix.Flags()).c_str(),
                mix.Count(), (unsigned long long)mix.Data_Size(),
                (unsigned long long)mix.Data_Start(),
                ok ? "索引自洽" : "索引不自洽", bad);
    if (!ok) {
        return 1;
    }

    // 按名字取条目（CRC 反查 + 索引都对了才会命中）
    if (const MixEntry* e = mix.Find("LOCALMD.MIX")) {
        std::printf("OK   按名找到 LOCALMD.MIX：off=%u size=%u\n", e->offset, e->size);
        auto sub = mix.Open_Sub(*e);
        if (sub) {
            int sub_bad = 0;
            sub->Validate_Index(&sub_bad);
            std::printf("OK   嵌套 MIX 打开成功：条目=%d 数据区=%llu 越界=%d\n",
                        sub->Count(), (unsigned long long)sub->Data_Size(), sub_bad);
        } else {
            std::printf("FAIL LOCALMD.MIX 应当是个嵌套 MIX\n");
            return 1;
        }
    } else {
        std::printf("FAIL 没找到 LOCALMD.MIX（CRC 或索引有误）\n");
        return 1;
    }

    // 递归取出那个每个 MIX 都带的密钥文件，内容应当以 [PublicKey] 开头。
    // 这个文件本身就是算法正确性的铁证：里面的公钥必须与二进制里的一致。
    std::vector<uint8_t> keyfile = mix.Read_Deep_By_ID(0x763C81DD);
    if (keyfile.size() != 151) {
        std::printf("FAIL 密钥文件大小 %zu，应为 151\n", keyfile.size());
        return 1;
    }
    const std::string head(reinterpret_cast<const char*>(keyfile.data()), 11);
    if (head != "[PublicKey]") {
        std::printf("FAIL 密钥文件开头是 %s，应为 [PublicKey]\n", head.c_str());
        return 1;
    }
    std::printf("OK   递归取出密钥文件（151 字节），开头 = [PublicKey]\n");
    std::printf("     %.*s\n", 58, reinterpret_cast<const char*>(keyfile.data()) + 11);
    return 0;
}

int main(int argc, char** argv) {
    if (argc > 1) {
        return Verify_Mix(argv[1]);  // 只做 MIX 校验，不跑下面的基准
    }

    // ---- 地图 ----
    MapClass map;
    constexpr int kW = 128;
    constexpr int kH = 128;
    map.Init_Clear(kW, kH);

    // 随机铺一些障碍，构造出需要绕路的场景。
    std::mt19937 rng(20011031);  // 固定种子：测试必须可复现
    for (int i = 0; i < kW * kH / 8; ++i) {
        CellStruct c{static_cast<int16_t>(rng() % kW), static_cast<int16_t>(rng() % kH)};
        if (CellClass* cell = map.Cell_At(c)) {
            cell->Set_Land(LandType::Rock);
        }
    }

    // ---- 构造一批寻路请求 ----
    std::vector<PathRequest> reqs;
    for (int i = 0; i < 256; ++i) {
        PathRequest r;
        r.from = CellStruct{static_cast<int16_t>(rng() % kW), static_cast<int16_t>(rng() % kH)};
        r.to = CellStruct{static_cast<int16_t>(rng() % kW), static_cast<int16_t>(rng() % kH)};
        r.ticket = static_cast<uint32_t>(i);
        r.max_cost = 4096;
        reqs.push_back(r);
    }

    // ---- 串行基准 ----
    PathFinder serial(&map);
    const auto t0 = std::chrono::steady_clock::now();
    std::vector<PathResult> base = serial.Solve(reqs);
    const auto t1 = std::chrono::steady_clock::now();
    const double serial_ms =
        std::chrono::duration<double, std::milli>(t1 - t0).count();

    // ---- 并行 ----
    TaskSystem pool;
    PathFinder parallel(&map);
    const auto t2 = std::chrono::steady_clock::now();
    std::vector<PathResult> test = parallel.Solve_Parallel(pool, reqs);
    const auto t3 = std::chrono::steady_clock::now();
    const double par_ms = std::chrono::duration<double, std::milli>(t3 - t2).count();

    // ---- 一致性校验 ----
    int found = 0;
    for (size_t i = 0; i < base.size(); ++i) {
        if (base[i].found) {
            ++found;
        }
        if (!SameResult(base[i], test[i])) {
            std::printf("FAIL: 第 %zu 条路径串行/并行结果不一致\n", i);
            return 1;
        }
    }

    // ---- 帧队列的确定性 ----
    FrameQueue q;
    std::vector<FrameEvent> out;
    q.Submit_Local(0, {FrameEvent{0, 1, 10, 20, 0}, FrameEvent{0, 2, 1, 2, 3}});
    q.Submit_Remote(1, 0, {FrameEvent{1, 3, 5, 6, 7}});
    if (q.Execute(out) != FrameStepResult::Ok || out.size() != 3) {
        std::printf("FAIL: 帧队列未能在两方输入齐备时推进\n");
        return 1;
    }
    // 只有一方提交时应当卡住 —— 这正是锁步的语义。
    q.Submit_Local(1, {FrameEvent{0, 1, 0, 0, 0}});
    std::vector<FrameEvent> out2;
    if (q.Execute(out2) != FrameStepResult::WaitingForPlayers) {
        std::printf("FAIL: 缺少远端输入时不应推进\n");
        return 1;
    }

    std::printf("OK  路径 %zu 条（命中 %d），串行 %.2f ms，并行 %.2f ms，加速 %.2fx\n",
                base.size(), found, serial_ms, par_ms,
                par_ms > 0.0 ? serial_ms / par_ms : 0.0);
    std::printf("OK  锁步帧队列行为正确，帧 CRC = 0x%08X\n", q.Compute_CRC());

    // ---- 类层次 ----
    // 这部分不再是推断，而是 gamemd.exe 的 MSVC RTTI 原文（src/re/ClassHierarchy.h）。
    // 下面几条 static_assert 是回归测试：一旦重新抽取的类表不再满足这些关系，
    // 说明要么 RTTI 解析退化了，要么我们对引擎对象模型的理解是错的。
    static_assert(re::IsDerivedFrom(vtable::cls::kUnitClass, vtable::cls::kTechnoClass),
                  "UnitClass 必须派生自 TechnoClass");
    static_assert(re::IsDerivedFrom(vtable::cls::kInfantryClass, vtable::cls::kFootClass),
                  "InfantryClass 必须派生自 FootClass");
    static_assert(re::IsDerivedFrom(vtable::cls::kBuildingClass, vtable::cls::kTechnoClass),
                  "BuildingClass 必须派生自 TechnoClass");
    static_assert(!re::IsDerivedFrom(vtable::cls::kAnimClass, vtable::cls::kTechnoClass),
                  "AnimClass 挂在 ObjectClass 下，不是 TechnoClass");

    // 类大小：来自二进制里 `push <size>; call operator new` 的实测值，
    // 不是按字段推算的。这几个数对多核改造是硬约束 —— 对象 2KB 级别，
    // 一次遍历的访存量远大于计算量，并行必须按缓存行切分而不是按对象切分。
    const uint32_t sz_unit = re::SizeOf("UnitClass");
    const uint32_t sz_inf = re::SizeOf("InfantryClass");
    const uint32_t sz_air = re::SizeOf("AircraftClass");
    if (sz_unit == 0 || sz_inf == 0 || sz_air == 0) {
        std::printf("FAIL: 关键类的 sizeof 未能从二进制中提取\n");
        return 1;
    }
    std::printf("INFO sizeof 实测：UnitClass=%u InfantryClass=%u AircraftClass=%u（共 %d 条）\n",
                sz_unit, sz_inf, sz_air, re::kSizeCount);

    std::printf("INFO 类层次取自 RTTI 实证：%d 个类 / %d 个虚表槽位\n",
                re::kClassCount, re::kFlatSlotCount);
    std::printf("     UnitClass 链：");
    for (int id = vtable::cls::kUnitClass; id >= 0;
         id = re::kClassTable[id].base_index) {
        std::printf("%s(%d槽) ", re::kClassTable[id].name, re::kClassTable[id].slots);
    }
    std::printf("\n");
    return 0;
}
