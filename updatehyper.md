

---

# 5D Chess Hypercuboid 搜索算法优化技术文档

## 1. 算法范式评估

### 当前架构
当前实现采用了一种 **基于冲突驱动的子句学习 (CDCL) / 懒惰 SMT (Lazy Satisfiability Modulo Theories)** 的变体来解决高维约束满足问题 (CSP)。
*   **核心机制**：将解空间建模为超立方体 (Hypercuboid)，通过采样 (`take_point`) -> 验证 (`find_problem`) -> 剪枝 (`remove_slice`) 的迭代过程逼近解。
*   **评价**：在宏观范式上，该方法优于传统的深度优先搜索 (DFS) 或简单的回溯法，能够有效应对5D Chess因时间线分支导致的组合爆炸问题。

### 存在问题
虽然范式正确，但当前实现过于“懒惰 (Lazy)”。大量的约束检查被推迟到了采样之后（Post-sampling），导致无效状态的重复生成和昂贵的验证开销。

---

## 2. 核心瓶颈分析

1.  **状态管理的内存开销 (Critical)**：
    在 `find_checks` 中，为了验证一个采样点，代码执行了 `state newstate = s;`。全量深拷贝一个包含所有时间线和棋盘的复杂状态对象是极度昂贵的操作，且处于搜索的最内层循环。

2.  **约束传播滞后 (Inefficient)**：
    *   **跳跃匹配**：`take_point` 内部使用图匹配算法寻找合法的出发-到达对。这不仅是重复计算，而且是在运行时解决本应是静态的二元约束。
    *   **Present 线完整性**：许多导致 Present 线断裂的组合可以在构建空间时预先剔除，无需等到模拟阶段。

3.  **冲突学习的不充分 (Sub-optimal Pruning)**：
    当前 `find_checks` 在发现第一个 Check 时即返回。如果存在多重 Check（多路同时将军），当前的 Slice 只能移除导致第一路将军的移动，而没有学习到“必须同时解决所有将军”这一更强的约束。

---

## 3. 优化实施方案

### 3.1 状态机重构：从 Copy 到 Reversible (DO/UNDO)

**目标**：消除内层循环的内存分配和深拷贝。

**实施细节**：
*   弃用 `state newstate = s`。
*   在 `state` 类中实现可撤销操作接口：
    *   `void apply_move_inplace(const full_move& m)`
    *   `void undo_move_inplace(const full_move& m)`
*   **流程变更**：
    ```cpp
    // 伪代码
    for (auto& m : moves) s.apply_move_inplace(m);
    auto result = s.find_checks(!c);
    for (auto& m : reverse(moves)) s.undo_move_inplace(m); // 恢复现场
    ```
*   **预期收益**：性能提升级数（O(1) vs O(N) 内存操作）。

### 3.2 约束传播前置：弧一致性 (Arc Consistency)

**目标**：将动态检查转化为静态约束，缩小初始搜索空间。

**实施细节**：
*   **静态跳跃绑定**：
    *   在 `build_HC` 阶段建立依赖表：`DependencyMap[Axis_Depart] = Axis_Arrive`。
    *   若 Axis_Depart 选择了移动 $i$，则强制 Axis_Arrive 必须选择对应的移动 $j$。如果 $j$ 已被剪枝，则 $i$ 也必须被剪枝（AC-3 算法逻辑）。
*   **移除运行时图匹配**：
    *   `take_point` 不应再构建图求解匹配，而应直接根据上述依赖表填充坐标。

### 3.3 增强的冲突学习 (Conflict Clause Optimization)

**目标**：提高剪枝效率，减少迭代次数。

**实施细节**：
*   **多重威胁处理**：
    *   修改 `find_checks` 使其不提前返回，而是收集**所有**当前的威胁。
    *   生成的 `slice` 应该是所有独立威胁解决方案的**交集**。
    *   逻辑：`Constraint = Solve(Check_A) AND Solve(Check_B)`。
*   **最小冲突集 (Minimal Conflict Set)**：
    *   确保 `slice` 中只包含真正导致问题的轴。例如，如果 Check 仅发生在 Timeline 1 且无跨时间线交互，则不应将 Timeline 2 的坐标加入 Slice。

### 3.4 启发式采样 (Heuristics)

**目标**：尽快找到可行解或尽快证明无解（Fail-first）。

**实施细节**：
*   **变量排序 (Variable Ordering)**：在 `take_point` 中，优先确定那些“最困难”的轴（例如：正处于被将军状态的时间线，或合法移动极少的时间线）。
*   **值排序 (Value Ordering)**：在构建 `axis_coords` 时，将更有希望的移动排在前面：
    1.  能够吃子的移动。
    2.  能够阻挡射线的移动。
    3.  非跳跃移动（减少分支复杂度）。

---

## 4. 改进后的搜索流程伪代码

```cpp
// 1. 预处理 (Pre-processing)
HC_info info = build_HC(s);
enforce_arc_consistency(info); // 静态剔除无效的跳跃组合

// 2. 搜索循环
while (!search_space.empty()) {
    HC current_hc = select_hc_heuristically(search_space);
    
    // 3. 启发式采样 (不再使用图匹配)
    point p = heuristic_sample(current_hc); 
    
    // 4. 验证 (In-place)
    s.apply_moves(p);
    Constraint c = s.find_all_violations(); // 获取所有违规的交集
    s.undo_moves(p);
    
    if (c.is_empty()) {
        yield p; // 找到解
        search_space.remove_point(p);
    } else {
        // 5. 强剪枝
        search_space.subtract(c); // 从空间中切除坏块
    }
}
```

## 5. 总结

该优化方案保持了原算法优秀的高维空间建模能力，通过引入**可撤销状态机**解决了内存瓶颈，通过**弧一致性**加强了约束传播，并利用**多重冲突学习**提升了收敛速度。实施这些改进后，该算法在处理复杂多时间线局面时的效率将得到本质提升。