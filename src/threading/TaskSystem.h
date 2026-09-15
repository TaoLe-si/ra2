// TaskSystem.h -- 任务系统（**新增**，原引擎没有这一层）
//
// 原引擎的线程使用情况（来自导入表，见 docs/binary-baseline.md）：
//   CreateThread / SetThreadPriority / GetCurrentThreadId
//   InitializeCriticalSection / EnterCriticalSection / LeaveCriticalSection
//   CreateMutexA / OpenMutexA / ReleaseMutex
//   CreateEventA / SetEvent / ResetEvent / OpenEventA
//   InterlockedIncrement / InterlockedDecrement
// 说明它**已经有**线程（网络收包、音频、文件 IO 等），但游戏逻辑本身
// 是单线程的 —— 逻辑跑在主线程里，靠锁步帧队列串行推进。
//
// 本文件提供一个最小但正确的并行骨架，设计上只服务于一个目标：
//   "在单个逻辑帧内部并行，同时保持结果与严格串行完全一致。"
//
// 三条硬约束：
//   1. 不允许跨帧并行（锁步模型下帧与帧必须严格有序）；
//   2. 并行区内禁止任何依赖"执行顺序"的归约（比如浮点累加、取最先完成者）；
//   3. 每个工作线程拥有自己的临时缓冲，避免伪共享与数据竞争。

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace ra2 {

/// 并行区间。
struct Range {
    size_t begin = 0;
    size_t end = 0;
    size_t Size() const noexcept { return end - begin; }
};

/// 把 [0, total) 切成 count 段。
std::vector<Range> Split_Range(size_t total, size_t count);

/// 固定大小线程池。
///
/// 刻意做得简单：没有任务窃取、没有优先级、没有嵌套。
/// 原因：锁步模拟里，越可预测越好调优，调度花活只会让失步排查变难。
class TaskSystem {
public:
    explicit TaskSystem(size_t worker_count = 0);
    ~TaskSystem();

    TaskSystem(const TaskSystem&) = delete;
    TaskSystem& operator=(const TaskSystem&) = delete;

    size_t Worker_Count() const noexcept { return workers_.size(); }

    /// 把 [0, total) 分给工作线程执行 body。
    /// 阻塞直到全部完成 —— 即 fork/join，帧内的同步点。
    void Parallel_For(size_t total, const std::function<void(Range)>& body);

    /// 同上，但每个线程先拿到自己的 thread_index（用于索引线程局部缓冲）。
    void Parallel_For(size_t total,
                      const std::function<void(Range, size_t thread_index)>& body);

private:
    void Worker_Loop(size_t index);

    // Job 必须是**成员**而不是 Parallel_For 的栈上局部量：
    // 主线程先跑完自己那份就会退出等待，而工作线程可能还在读它，
    // 局部量会导致 use-after-free（第一版就踩了这个坑，表现为偶发段错误）。
    struct Job {
        std::function<void(Range, size_t)> body;
        std::vector<Range> ranges;
        std::atomic<size_t> next{0};
        std::atomic<size_t> done{0};
    };

    std::vector<std::thread> workers_;
    Job job_{};
    std::atomic<size_t> job_gen_{0};   ///< 每次投递一个 job 就 +1
    std::atomic<bool> quit_{false};
    std::mutex mq_;
    std::condition_variable cv_job_;
    std::condition_variable cv_done_;
};

}  // namespace ra2
