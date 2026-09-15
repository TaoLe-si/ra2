// GameLoop.cpp -- 主循环实现
//
// 对应原始 MainLoop.CPP（0x0055E420）与外层循环（0x0055DEE0）。
//
// 与原始实现的差异（有意为之，服务于多核改造）：
//   1. 把每帧工作显式切成 LoopPhase 若干阶段；
//   2. 每个阶段的耗时都进 LoopStats，改造前后可以量化对比；
//   3. 阶段之间是严格的顺序边界 —— 并行只允许发生在阶段**内部**。

#include "engine/GameLoop.h"

#include <chrono>

#include "map/Map.h"

namespace ra2 {
namespace {

using Clock = std::chrono::steady_clock;

double ElapsedMS(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

}  // namespace

void GameLoop::Run() {
    running_ = true;
    while (running_) {
        const auto t0 = Clock::now();

        Phase_Input();
        const auto t1 = Clock::now();

        Phase_FrameSync();
        const auto t2 = Clock::now();

        Phase_Logic();
        const auto t3 = Clock::now();

        Phase_Pathfinding();
        const auto t4 = Clock::now();

        Phase_Visibility();
        const auto t5 = Clock::now();

        Phase_Render();
        const auto t6 = Clock::now();

        // 指数滑动平均，避免统计本身成为热点。
        const double alpha = 0.05;
        stats_.avg_logic_ms += alpha * (ElapsedMS(t2, t3) - stats_.avg_logic_ms);
        stats_.avg_path_ms += alpha * (ElapsedMS(t3, t4) - stats_.avg_path_ms);
        stats_.avg_visibility_ms += alpha * (ElapsedMS(t4, t5) - stats_.avg_visibility_ms);
        stats_.avg_render_ms += alpha * (ElapsedMS(t5, t6) - stats_.avg_render_ms);
        stats_.avg_frame_ms += alpha * (ElapsedMS(t0, t6) - stats_.avg_frame_ms);
        ++stats_.frames;
        (void)t1;
    }
}

void GameLoop::Phase_Input() {
    // TODO(逆向)：对应 0x0055E420 前段的输入采集。
    //   原函数引用了 TXT_OBSERVER_CANT_SEND / TXT_NAME_NOT_SPECIFIED /
    //   TXT_BEACON_MESSAGE 等聊天文本，说明聊天输入也是在这一段处理的。
}

void GameLoop::Phase_FrameSync() {
    // 锁步：等齐所有玩家输入后才允许推进。
    // 注意这里**不能**为了让 CPU 忙起来而"提前算下一帧"：
    // 下一帧的输入可能还没到，提前计算等于引入分支预测式的投机，
    // 一旦输入不同就得回滚，反而更慢且容易出错。
    pending_events_.clear();
    FrameStepResult r = queue_.Execute(pending_events_);
    if (r == FrameStepResult::WaitingForPlayers) {
        // 原引擎在这里自旋（"Wait_For_Players returned %d"）。
        return;
    }
    (void)r;
}

void GameLoop::Phase_Logic() {
    // 执行本帧命令 + 所有对象的 Update。
    // 这是单帧里最重的部分之一，也是并行收益最大的地方。
    for (const FrameEvent& e : pending_events_) {
        (void)e;
        // TODO(逆向)：命令分发对应 Queue.CPP 的 "Failure executing DoList"
        //   所在的 0x00648710（1444 指令）。
    }
}

void GameLoop::Phase_Pathfinding() {
    // 见 ai/PathFinder.h。原引擎把寻路请求排成队列逐帧消化，
    // 这是"同一帧内多个单位抢一个寻路器"的典型瓶颈。
}

void GameLoop::Phase_Visibility() {
    // 视野 / 战争迷雾。RTTI 里的 FoggedObjectClass 说明迷雾是逐对象状态。
}

void GameLoop::Phase_Render() {
    // 渲染走 DirectDraw（二进制仅导入 DDRAW.dll 的 DirectDrawCreate 一个函数，
    // 其余为动态获取）。渲染本身可以在独立线程做，但要注意与逻辑的状态隔离。
}

}  // namespace ra2
