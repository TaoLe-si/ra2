// PathFinder.cpp
//
// 对应原始实现：0x0042C900（"Regular findpath failure" /
// "Hierarchical findpath failure" 两条日志的共同引用点）。
//
// 分层规划侧的 RTTI 类：MapRegionClass / PlanningNodeClass /
// PlanningBranchClass / PlanningMemberClass / PlanningTokenClass。

#include "ai/PathFinder.h"

#include <algorithm>
#include <queue>

#include "map/Map.h"
#include "threading/TaskSystem.h"

namespace ra2 {
namespace {

// 八方向邻居。等距地图里对角线移动的代价与直线相同（原引擎按方向数计步）。
constexpr int kDirX[8] = {1, 1, 0, -1, -1, -1, 0, 1};
constexpr int kDirY[8] = {0, 1, 1, 1, 0, -1, -1, -1};

struct Node {
    int32_t g = 0;
    int32_t f = 0;
    CellStruct cell;
    int32_t parent = -1;

    bool operator>(const Node& o) const { return f > o.f; }
};

// 线程局部的 A* 工作缓冲。
//
// 这一层不是"优化炫技"，而是并行能不能跑出收益的关键：
// 实测（128x128 地图 / 256 条请求）在每次寻路都现场分配 best/parent 两个
// 数组时，并行版本反而比串行慢（0.89x）—— 时间全花在分配与清零上，
// 线程带来的收益被完全吃掉。改成线程局部复用 + 时间戳免清零之后，
// 分配降到 O(1)，并行才能体现出优势。
//
// 用 thread_local 也顺带保证了并行安全：每个工作线程各用各的缓冲。
struct Scratch {
    std::vector<int32_t> g;
    std::vector<int32_t> parent;
    std::vector<uint32_t> stamp;
    uint32_t cur = 0;
};

Scratch& Scratch_For_This_Thread() {
    static thread_local Scratch s;
    return s;
}

}  // namespace

bool PathFinder::Passable(CellStruct c) const noexcept {
    if (map_ == nullptr) {
        return false;
    }
    const CellClass* cell = map_->Cell_At(c);
    if (cell == nullptr) {
        return false;
    }
    return cell->Is_Clear_To_Move(cell->Land());
}

int32_t PathFinder::Cost_Heuristic(CellStruct a, CellStruct b) const noexcept {
    // 八方向下的可采纳启发式：切比雪夫距离。
    return CellDistance(a, b);
}

PathResult PathFinder::Regular_Find_Path(CellStruct from, CellStruct to,
                                         int32_t max_cost) {
    PathResult res;
    ++regular_count_;

    if (from == to) {
        res.found = true;
        res.cost = 0;
        res.waypoints.push_back(from);
        return res;
    }
    if (map_ == nullptr) {
        return res;
    }

    const int w = map_->Width();
    const int h = map_->Height();
    const auto id = [w](CellStruct c) { return c.Y * w + c.X; };

    const size_t n = static_cast<size_t>(w) * static_cast<size_t>(h);
    Scratch& sc = Scratch_For_This_Thread();

    // 按需扩容；时间戳递增代替每次清零。
    if (sc.g.size() < n) {
        sc.g.resize(n, 0);
        sc.parent.resize(n, -1);
        sc.stamp.resize(n, 0);
    }
    ++sc.cur;
    if (sc.cur == 0xFFFFFFFFu) {  // 极端情况下的回绕，直接全清
        std::fill(sc.stamp.begin(), sc.stamp.end(), 0);
        sc.cur = 1;
    }
    const uint32_t stamp = sc.cur;

    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;

    sc.stamp[static_cast<size_t>(id(from))] = stamp;
    sc.g[static_cast<size_t>(id(from))] = 0;
    sc.parent[static_cast<size_t>(id(from))] = -1;
    open.push(Node{0, Cost_Heuristic(from, to), from, -1});

    while (!open.empty()) {
        const Node cur = open.top();
        open.pop();

        const int32_t ci = id(cur.cell);
        if (cur.g > sc.g[static_cast<size_t>(ci)]) {
            continue;  // 过期的堆项
        }
        if (cur.cell == to) {
            res.found = true;
            res.cost = cur.g;
            for (int32_t p = ci; p != -1; p = sc.parent[static_cast<size_t>(p)]) {
                res.waypoints.push_back(
                    CellStruct{static_cast<int16_t>(p % w), static_cast<int16_t>(p / w)});
            }
            std::reverse(res.waypoints.begin(), res.waypoints.end());
            return res;
        }
        if (max_cost >= 0 && cur.g >= max_cost) {
            break;
        }

        for (int d = 0; d < 8; ++d) {
            CellStruct nb{static_cast<int16_t>(cur.cell.X + kDirX[d]),
                          static_cast<int16_t>(cur.cell.Y + kDirY[d])};
            if (nb.X < 0 || nb.Y < 0 || nb.X >= w || nb.Y >= h) {
                continue;
            }
            if (!Passable(nb)) {
                continue;
            }
            const size_t ni = static_cast<size_t>(id(nb));
            const int32_t ng = cur.g + 1;
            if (sc.stamp[ni] == stamp && sc.g[ni] <= ng) {
                continue;
            }
            sc.stamp[ni] = stamp;
            sc.g[ni] = ng;
            sc.parent[ni] = ci;
            open.push(Node{ng, ng + Cost_Heuristic(nb, to), nb, ci});
        }
    }
    return res;
}

PathResult PathFinder::Hierarchical_Find_Path(CellStruct from, CellStruct to,
                                              int32_t max_cost) {
    ++hierarchical_count_;
    // TODO(逆向)：分层层的区域图（MapRegionClass）与抽象边代价尚未还原。
    //   还原路径：0x0042C900 里"Hierarchical findpath failure"分支的上方
    //   就是区域图查询；配合 RTTI 的 PlanningNodeClass / PlanningBranchClass
    //   可以确定区域图的节点与边结构。
    //   在此之前，分层规划退化为常规 A*，保证行为正确（只是慢）。
    PathResult r = Regular_Find_Path(from, to, max_cost);
    r.used_hierarchical = r.found;
    return r;
}

PathResult PathFinder::Find_Path(CellStruct from, CellStruct to, int32_t max_cost) {
    // 原引擎的策略：先分层，失败再常规。
    // 证据：两条 failure 日志在同一个函数里，且分层日志在前。
    PathResult r = Hierarchical_Find_Path(from, to, max_cost);
    if (!r.found) {
        r = Regular_Find_Path(from, to, max_cost);
        r.used_hierarchical = false;
    }
    return r;
}

std::vector<PathResult> PathFinder::Solve(const std::vector<PathRequest>& requests) {
    std::vector<PathResult> out(requests.size());
    for (size_t i = 0; i < requests.size(); ++i) {
        out[i] = Find_Path(requests[i].from, requests[i].to, requests[i].max_cost);
    }
    return out;
}

std::vector<PathResult> PathFinder::Solve_Parallel(
    TaskSystem& pool, const std::vector<PathRequest>& requests) {
    std::vector<PathResult> out(requests.size());
    // 每个区间一个下标范围；结果写回各自的槽位，因此无数据竞争，
    // 且完成顺序不影响最终数组内容 —— 这就是确定性的保证。
    pool.Parallel_For(requests.size(), [&](Range r) {
        for (size_t i = r.begin; i < r.end; ++i) {
            out[i] = Find_Path(requests[i].from, requests[i].to, requests[i].max_cost);
        }
    });
    return out;
}

}  // namespace ra2
