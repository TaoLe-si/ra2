// TaskSystem.cpp

#include "threading/TaskSystem.h"

#include <algorithm>

namespace ra2 {

std::vector<Range> Split_Range(size_t total, size_t count) {
    std::vector<Range> out;
    if (count == 0 || total == 0) {
        return out;
    }
    count = std::min(count, total);
    const size_t base = total / count;
    const size_t rem = total % count;
    size_t cur = 0;
    for (size_t i = 0; i < count; ++i) {
        const size_t n = base + (i < rem ? 1 : 0);
        out.push_back(Range{cur, cur + n});
        cur += n;
    }
    return out;
}

TaskSystem::TaskSystem(size_t worker_count) {
    if (worker_count == 0) {
        worker_count = std::thread::hardware_concurrency();
        if (worker_count == 0) {
            worker_count = 1;
        }
        // 留一个核给主线程与渲染，避免把机器压死。
        if (worker_count > 1) {
            --worker_count;
        }
    }
    workers_.reserve(worker_count);
    for (size_t i = 0; i < worker_count; ++i) {
        workers_.emplace_back(&TaskSystem::Worker_Loop, this, i);
    }
}

TaskSystem::~TaskSystem() {
    quit_.store(true, std::memory_order_release);
    cv_job_.notify_all();
    for (auto& t : workers_) {
        if (t.joinable()) {
            t.join();
        }
    }
}

void TaskSystem::Worker_Loop(size_t index) {
    size_t seen_gen = 0;
    for (;;) {
        {
            std::unique_lock<std::mutex> lk(mq_);
            cv_job_.wait(lk, [this, &seen_gen] {
                return job_gen_.load(std::memory_order_acquire) != seen_gen ||
                       quit_.load(std::memory_order_acquire);
            });
            if (quit_.load(std::memory_order_acquire) &&
                job_gen_.load(std::memory_order_acquire) == seen_gen) {
                return;
            }
            seen_gen = job_gen_.load(std::memory_order_acquire);
        }

        for (;;) {
            const size_t i = job_.next.fetch_add(1, std::memory_order_relaxed);
            if (i >= job_.ranges.size()) {
                break;
            }
            if (job_.body) {
                job_.body(job_.ranges[i], index);
            }
        }

        job_.done.fetch_add(1, std::memory_order_acq_rel);
        {
            std::lock_guard<std::mutex> lk(mq_);
            cv_done_.notify_all();
        }
    }
}

void TaskSystem::Parallel_For(size_t total, const std::function<void(Range)>& body) {
    Parallel_For(total, [&body](Range r, size_t) { body(r); });
}

void TaskSystem::Parallel_For(size_t total,
                              const std::function<void(Range, size_t)>& body) {
    if (total == 0) {
        return;
    }
    if (workers_.empty()) {
        body(Range{0, total}, 0);
        return;
    }

    // 切片数取线程数的若干倍：切片更细，负载更均衡，
    // 但切片太多会让同步开销上升。4 倍是实测比较稳的起点。
    const size_t slices = workers_.size() * 4;

    {
        std::lock_guard<std::mutex> lk(mq_);
        job_.body = body;
        job_.ranges = Split_Range(total, slices);
        job_.next.store(0, std::memory_order_relaxed);
        job_.done.store(0, std::memory_order_relaxed);
        job_gen_.fetch_add(1, std::memory_order_release);
    }
    cv_job_.notify_all();

    // 主线程也参与干活，避免占着核空等。
    for (;;) {
        const size_t i = job_.next.fetch_add(1, std::memory_order_relaxed);
        if (i >= job_.ranges.size()) {
            break;
        }
        body(job_.ranges[i], workers_.size());
    }

    std::unique_lock<std::mutex> lk(mq_);
    cv_done_.wait(lk, [this] {
        return job_.done.load(std::memory_order_acquire) >= workers_.size();
    });
}

}  // namespace ra2
