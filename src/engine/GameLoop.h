// GameLoop.h -- 主循环（对应原始 MainLoop.CPP）
//
// 二进制证据：
//   0x0055E420  MainLoop.CPP，986 指令，代码区间 0x0055E420-0x0055F1D3
//       引用 "D:\ra2mdpost\MainLoop.CPP"、聊天相关文本
//       （TXT_OBSERVER_CANT_SEND / TXT_NAME_NOT_SPECIFIED / TXT_BEACON_MESSAGE ...）
//   0x0055DEE0  外层循环，235 指令，调用 0x0055E420
//   0x00541820  网络包泵，760 指令，被 0x0055E420 调用，
//       引用 "Multicast channel has gone bad"、
//             "Adding DATA_ACK packet %d from %s ; %d to multicast queue"
//             "RouterChannel recive buffer overflow %d"
//   0x006475F0  帧推进核心（Queue.CPP），见 FrameQueue.h
//
// 原始主循环的形态（从调用图反推）：
//   while (running) {
//       处理窗口消息 / 输入
//       收网络包（0x00541820）
//       推进锁步帧（0x006475F0 -> 0x00648710）
//       渲染
//   }
//
// 单线程是原引擎的架构选择：逻辑、AI、渲染排队在同一条时间线上。
// 本文件的还原版本把这个结构显式化，并把"每帧要做的几大类工作"
// 拆成独立阶段，为后续并行留出接口（phase 之间保留严格顺序）。

#pragma once

#include <cstdint>
#include <vector>

#include "engine/FrameQueue.h"

namespace ra2 {

class MapClass;

/// 一帧内的执行阶段。
/// 顺序不是随意排的：它必须与原引擎的串行顺序在"可观测效果"上等价，
/// 否则会破坏锁步的确定性。
enum class LoopPhase : int32_t {
    Input = 0,        ///< 采集鼠标/键盘/网络输入
    FrameSync,        ///< 锁步：等齐所有玩家输入
    Logic,            ///< 对象 AI、移动、战斗、生产
    Pathfinding,      ///< 寻路（最贵，且是并行的首选目标）
    Visibility,       ///< 视野 / 战争迷雾重算
    Render,           ///< 绘制
    Count,
};

/// 运行时统计，用于对多核改造做收益度量。
struct LoopStats {
    uint32_t frames = 0;
    double avg_logic_ms = 0.0;
    double avg_path_ms = 0.0;
    double avg_visibility_ms = 0.0;
    double avg_render_ms = 0.0;
    double avg_frame_ms = 0.0;
};

class GameLoop {
public:
    explicit GameLoop(MapClass* map) : map_(map) {}

    /// 进入主循环，直到 Request_Stop() 被调用。
    void Run();

    void Request_Stop() noexcept { running_ = false; }

    const LoopStats& Stats() const noexcept { return stats_; }
    uint32_t Frame() const noexcept { return queue_.Current_Frame(); }

    FrameQueue& Queue() noexcept { return queue_; }
    MapClass* Map() noexcept { return map_; }

protected:
    // 各阶段。做成虚函数便于将来替换成并行实现（见 multicore-plan.md）。
    virtual void Phase_Input();
    virtual void Phase_FrameSync();
    virtual void Phase_Logic();
    virtual void Phase_Pathfinding();
    virtual void Phase_Visibility();
    virtual void Phase_Render();

private:
    MapClass* map_ = nullptr;
    FrameQueue queue_;
    bool running_ = false;
    LoopStats stats_{};

    /// 本帧待执行的命令，由 Phase_FrameSync 填充。
    std::vector<FrameEvent> pending_events_;
};

}  // namespace ra2
