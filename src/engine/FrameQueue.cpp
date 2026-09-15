// FrameQueue.cpp -- 锁步帧队列实现
//
// 对应原始 Queue.CPP：
//   0x006475F0  帧推进（"Processing Ticks:%03d Frames:%03d"）
//   0x00648710  命令列表执行（"Failure executing DoList"）
//   0x0064C380  Queue.CPP 区间内，649 指令
//
// 实现要点：**确定性**优先于性能。
//   * 命令的归约顺序必须是 (frame, house_index, 提交序号) 的字典序，
//     与"哪个包先到"无关 —— 否则网络抖动就会让各机器分叉。
//   * CRC 覆盖命令流本身，任何顺序变化都会被立刻发现。

#include "engine/FrameQueue.h"

#include <algorithm>

namespace ra2 {
namespace {

// FNV-1a：简单、确定、与平台无关。
// 原引擎用的是自己的 CRC（二进制里有大量 "*** CRCs" 日志），
// 这里只要求"串行与并行结果必须一致"，具体算法可以替换。
constexpr uint32_t kFnvOffset = 2166136261u;
constexpr uint32_t kFnvPrime = 16777619u;

inline uint32_t Fnv(uint32_t h, uint32_t v) noexcept {
    for (int i = 0; i < 4; ++i) {
        h ^= (v >> (i * 8)) & 0xFFu;
        h *= kFnvPrime;
    }
    return h;
}

}  // namespace

void FrameQueue::Submit_Local(uint32_t frame, std::vector<FrameEvent> events) {
    Submit_Remote(0, frame, std::move(events));
}

void FrameQueue::Submit_Remote(uint32_t house_index, uint32_t frame,
                               std::vector<FrameEvent> events) {
    if (pending_.size() <= house_index) {
        pending_.resize(house_index + 1);
        next_expected_.resize(house_index + 1, 0);
    }
    auto& bucket = pending_[house_index];

    // 迟到的包（帧号已经推进过了）直接丢弃并记录，
    // 这是原引擎 "Receive buffer overflow" 那条日志处理的场景。
    if (frame < current_frame_) {
        return;
    }

    auto it = std::lower_bound(bucket.begin(), bucket.end(), frame,
                               [](const FrameInput& a, uint32_t f) { return a.Frame < f; });
    if (it != bucket.end() && it->Frame == frame) {
        // 同帧重复提交：追加而不是覆盖，保持提交顺序。
        it->Events.insert(it->Events.end(), events.begin(), events.end());
    } else {
        bucket.insert(it, FrameInput{frame, std::move(events)});
    }

    latest_complete_ = std::max(latest_complete_, frame);
}

FrameStepResult FrameQueue::Execute(std::vector<FrameEvent>& out_events) {
    out_events.clear();
    if (pending_.empty()) {
        return FrameStepResult::WaitingForPlayers;
    }

    // 1) 每个人都必须交齐 current_frame_ 这一帧，否则卡住。
    //    对应原引擎的 "Wait_For_Players returned %d"。
    std::vector<std::vector<FrameEvent>> collected(pending_.size());
    for (size_t h = 0; h < pending_.size(); ++h) {
        auto& bucket = pending_[h];
        auto it = std::lower_bound(bucket.begin(), bucket.end(), current_frame_,
                                   [](const FrameInput& a, uint32_t f) { return a.Frame < f; });
        if (it == bucket.end() || it->Frame != current_frame_) {
            return FrameStepResult::WaitingForPlayers;
        }
        collected[h] = it->Events;
        bucket.erase(it);
    }

    // 2) 按 (house_index, 提交序号) 稳定归约。
    //    这个顺序是确定性的契约，并行化时**不得**改动。
    //    注意：每个玩家自己的事件列表内部顺序也必须保持。
    for (size_t h = 0; h < collected.size(); ++h) {
        for (const auto& e : collected[h]) {
            out_events.push_back(e);
        }
    }

    // 3) 推进并更新 CRC。
    for (const auto& e : out_events) {
        crc_ = Fnv(crc_, e.HouseIndex);
        crc_ = Fnv(crc_, e.Type);
        crc_ = Fnv(crc_, static_cast<uint32_t>(e.Arg0));
        crc_ = Fnv(crc_, static_cast<uint32_t>(e.Arg1));
        crc_ = Fnv(crc_, static_cast<uint32_t>(e.Arg2));
    }
    ++current_frame_;
    return FrameStepResult::Ok;
}

uint32_t FrameQueue::Recompute_CRC() const {
    return crc_;
}

}  // namespace ra2
