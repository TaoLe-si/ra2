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
#include "engine/FrameQueue.h"
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

int main() {
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
    return 0;
}
