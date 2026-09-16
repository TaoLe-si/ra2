// PathFinder.h -- 寻路系统还原
//
// 二进制证据：
//   0x0042C900  327 指令，同时引用两条日志 —— 这是最关键的一条证据：
//       "Regular findpath failure: (%d,%d) to (%d, %d)"
//       "Hierarchical findpath failure: (%d,%d) to (%d, %d)"
//     说明寻路分两级：先做粗粒度分层规划，失败再退回常规 A*。
//
//   RTTI 确认存在的相关类（db/classes.md）：
//       WaypointPathClass         路径结果（路点链）
//       MapRegionClass            区域划分 —— 分层的"区域"
//       PlanningNodeClass         规划节点
//       PlanningBranchClass       规划分支
//       PlanningMemberClass       分支成员
//       PlanningTokenClass        规划令牌
//       PlanningModeCommandClass  调试命令
//   二进制里还有 "LoopWaypointPath" 与 "Waypoint Path CRCs"。
//
//   这套 Planning* 类正好是一个分层规划器（HPA* 一族）的标准组成：
//   区域图 -> 抽象层 A* -> 细化成格子级路点。
//
// 为什么它是多核改造的第一目标：
//   1. 计算量大且与帧率强相关（单位一多就掉帧）；
//   2. 输入（地图静态代价 + 起点终点）在一帧内是只读快照；
//   3. 输出是互不影响的独立路径 —— 天然的任务并行；
//   4. 只要结果按"请求序号"有序写回，就完全不破坏确定性。

#pragma once

#include <cstdint>
#include <vector>

#include "core/Types.h"

namespace ra2 {

class MapClass;

/// 寻路结果。
struct PathResult {
    bool found = false;
    /// 路点序列（格子级）。
    std::vector<CellStruct> waypoints;
    /// 总代价，供上层比较。
    int32_t cost = 0;
    /// 用了哪一级规划。
    bool used_hierarchical = false;
};

/// 一次寻路请求。
struct PathRequest {
    CellStruct from;
    CellStruct to;
    /// 请求序号：并行执行后按它排序写回，保证结果与串行一致。
    uint32_t ticket = 0;
    int32_t max_cost = -1;  ///< 代价上限，超过即放弃
};

/// 寻路器。
///
/// 接口刻意做成"批量"：一次 Solve() 处理一批请求。
/// 串行实现逐条跑；并行实现（见 threading/TaskSystem.h）把请求分摊到
/// 多个工作线程，但**按 ticket 顺序**返回结果 —— 这是保持确定性的关键。
class PathFinder {
public:
    explicit PathFinder(MapClass* map = nullptr) : map_(map) {}
    void Set_Map(MapClass* map) noexcept { map_ = map; }

    /// 单条寻路。内部走分层规划，失败则退回常规 A*。
    PathResult Find_Path(CellStruct from, CellStruct to, int32_t max_cost = -1);

    /// 分层规划（粗粒度）。对应 "Hierarchical findpath"。
    PathResult Hierarchical_Find_Path(CellStruct from, CellStruct to, int32_t max_cost);

    /// 常规 A*（细粒度）。对应 "Regular findpath"。
    PathResult Regular_Find_Path(CellStruct from, CellStruct to, int32_t max_cost);

    /// 批量求解。结果顺序与输入顺序一致（按 ticket 归位）。
    std::vector<PathResult> Solve(const std::vector<PathRequest>& requests);

    /// 并行批量求解：与 Solve() 逐条结果**完全一致**，只是更快。
    /// 安全性来自两点：
    ///   1. Find_Path 只用局部缓冲，不碰共享状态（下面的 visited_stamp_ 不参与）；
    ///   2. 结果按 ticket 下标写回固定槽位，与完成顺序无关。
    std::vector<PathResult> Solve_Parallel(class TaskSystem& pool,
                                           const std::vector<PathRequest>& requests);

    /// 统计：最近一次 Solve 走了多少条常规 / 分层。
    uint32_t Regular_Count() const noexcept { return regular_count_; }
    uint32_t Hierarchical_Count() const noexcept { return hierarchical_count_; }

private:
    int32_t Cost_Heuristic(CellStruct a, CellStruct b) const noexcept;
    bool Passable(CellStruct c) const noexcept;

    MapClass* map_ = nullptr;
    uint32_t regular_count_ = 0;
    uint32_t hierarchical_count_ = 0;

    /// A* 的开放/关闭标记复用缓冲，避免每次寻路都分配。
    /// 并行化时每个工作线程需要**各自一份**。
    std::vector<uint32_t> visited_stamp_;
    uint32_t stamp_ = 0;
};

}  // namespace ra2
