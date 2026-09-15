// FrameQueue.h -- 锁步帧队列（对应原始 Queue.CPP）
//
// 二进制证据（全部来自 db/strings.json，由 tools/query.py 定位）：
//   0x006475F0  Queue.CPP，888 指令
//       引用 "Processing Ticks:%03d Frames:%03d"
//             "Wait_For_Players returned %d"
//             "Failure executing DoList"
//             "Frame %d, my sent = %d"
//   0x00648710  Queue.CPP 内最大函数，1444 指令，被 0x006475F0 调用
//
// 结论：RA2/YR 用的是**确定性锁步（deterministic lockstep）**：
//   * 逻辑按固定帧推进（15 FPS），不随渲染帧率变化；
//   * 每一帧必须收齐所有玩家的输入才能推进（Wait_For_Players）；
//   * 所有机器跑同一份确定性模拟，只同步"输入"而不是"状态"。
//
// 这直接决定了多核改造的边界：
//   ✅ 一帧**内部**的计算可以并行（多条互不依赖的子系统）；
//   ❌ 绝不能改变一帧**之内**的执行顺序或引入非确定性归约，
//      否则各机器算出的状态会分叉，联机立刻失步。
//   详见 docs/multicore-plan.md。

#pragma once

#include <cstdint>
#include <vector>

namespace ra2 {

/// 一帧内要执行的一条命令（移动、攻击、建造……）。
/// RTTI 确认存在 EventClass；二进制里还有
/// "Adding DATA_ACK packet %d from %s ; %d to multicast queue" 等日志
/// （0x00541820），说明命令是按帧打包成网络包广播的。
struct FrameEvent {
    uint32_t HouseIndex = 0;   ///< 发出者
    uint32_t Type = 0;         ///< 命令类型
    int32_t Arg0 = 0;
    int32_t Arg1 = 0;
    int32_t Arg2 = 0;
};

/// 单个玩家的某一帧输入包。
struct FrameInput {
    uint32_t Frame = 0;
    std::vector<FrameEvent> Events;
};

/// 帧状态机返回值。
enum class FrameStepResult : int32_t {
    Ok = 0,              ///< 本帧已推进
    WaitingForPlayers,   ///< 还在等其它玩家的输入（不能推进）
    Overflow,            ///< 接收缓冲溢出（原引擎：Receive buffer overflow）
    Desync,              ///< 校验不一致
};

/// 锁步帧队列。
///
/// 这是整个引擎的"心跳"，也是多核改造的同步锚点：
/// 并行任务必须在每帧的 Execute() 里 fork，在帧末 join，
/// 不能跨帧。
class FrameQueue {
public:
    static constexpr int kMaxAheadFrames = 32;  ///< 最多可领先其它玩家多少帧

    /// 记录本地玩家在 frame 的输入。
    void Submit_Local(uint32_t frame, std::vector<FrameEvent> events);

    /// 记录远端玩家的输入。
    void Submit_Remote(uint32_t house_index, uint32_t frame,
                       std::vector<FrameEvent> events);

    /// 尝试推进一帧。返回 Ok 表示 current_frame_ 已经 +1，
    /// 并且 events 被填充为本帧要执行的全部命令（按 (house, 序号) 稳定排序，
    /// 保证所有机器顺序一致）。
    FrameStepResult Execute(std::vector<FrameEvent>& out_events);

    uint32_t Current_Frame() const noexcept { return current_frame_; }
    uint32_t Latest_Complete_Frame() const noexcept { return latest_complete_; }

    /// 帧校验和。原引擎有大量的 CRC 日志（"*************** Building CRCs"
    /// "Em Pulse CRCs"、"Tiberium CRCs"、"Sides CRCs"、
    /// "Waypoint Path CRCs"，见 db/strings.json），说明它就是靠 CRC 比对
    /// 来检测失步的。并行化后这个校验必须保留且结果必须与串行一致。
    uint32_t Compute_CRC() const noexcept { return crc_; }

private:
    uint32_t Recompute_CRC() const;

    uint32_t current_frame_ = 0;
    uint32_t latest_complete_ = 0;
    uint32_t crc_ = 0;

    /// 每个玩家尚未执行的输入，按帧号分桶。
    /// 索引 = house_index。
    std::vector<std::vector<FrameInput>> pending_;
    std::vector<uint32_t> next_expected_;  ///< 每个玩家下一帧期望的帧号
};

}  // namespace ra2
