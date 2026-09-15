# Round 001 简报

本轮从零研究 6 类盘口微观结构机制，先用 12 个“机制代表 + 单变量对照”种子验证新链路，再按 `因子 × 事件 × 股票池 × 标签` 独立判断。逻辑来自盘口因果结构：虚拟穿透成本、成交双方母单容量错配、挂单生命周期承诺、撤单与成交消耗分歧、冲击后补单、去母单拆分后的主动序列惊讶度。

## 结果

训练/观察期（2021–2024）共 576 个最小单元：192 个 supported、194 个 promising、46 个 unsupported、144 个 not_evaluable。筛选阈值在读取 2025 前冻结；2025 只用于展示，不反馈到选择、经验或下一轮逻辑。

| 因子 | 训练/观察结论 | supported 单元 | 主要位置 |
|---|---:|---:|---|
| `trade_pair_parent_size_mismatch_equal_weight_v1` | 入选 | 42 | 三股票池、多数日内事件、两标签 |
| `resting_order_commitment_survival_age_weighted_v1` | 入选 | 39 | 三股票池、多数日内事件、两标签 |
| `trade_pair_parent_size_mismatch_volume_weighted_v1` | 入选 | 37 | 三股票池、多数日内事件、两标签 |
| `cancel_execution_divergence_joint_norm_v1` | 入选 | 14 | 主要在早中盘和部分股票池 |
| `cancel_execution_divergence_separate_norm_v1` | 入选 | 13 | 主要在早中盘和部分股票池 |
| `counterfactual_depth_fragility_shock25_v1` | 入选 | 4 | 14:30，000906/003800 |
| `counterfactual_depth_fragility_shock10_v1` | 入选 | 2 | 14:30，000906 |
| `distinct_aggressor_run_surprisal_markov_v1` | 入选 | 2 | 10:00，raw926，000985/003800 |
| `resting_order_commitment_survival_no_age_v1` | 不重复展示 | 39 | 与 age-weighted 评价面板完全相同，保留为重复证据 |
| `distinct_aggressor_run_surprisal_marginal_v1` | 未入选 | 0 | 有同向迹象，但未达到冻结的幅度/一致性门槛 |
| `execution_shock_replenishment_order_elasticity_v1` | 无法评价 | 0 | 全单元 RankIC 不可定义，需重做冲击样本形成 |
| `execution_shock_replenishment_quote_elasticity_v1` | 无法评价 | 0 | 全单元 RankIC 不可定义，需重做冲击样本形成 |

这里的“入选”仅表示允许查看冻结后的 2025 效果图，不是自动晋级；“未入选”也不是淘汰。

## 数据与输出

- 2021–2024：969 天，12 因子，12 组评价，12 份画像。
- 2025：228 天，8 个冻结展示因子，6 组评价，每组 1,824 行。
- Arrow 只有 `symbol/date/event + 因子值`，没有 `ready_*`、`reason_*` 或 readiness sidecar；未准备值为 NaN，真实 0 保留。
- 每个入选因子生成一张 6×3 图：三股票池 × 两标签为 6 行，IC 累积、多头累积、冻结最优事件十分层为 3 列，配色为 coolwarm。

## 下一轮

已生成 6 份机制级 Experience Record。Round 002 计划 96 个候选，每个机制 16 个结构，优先扩展机制与表示，不做盲目窗口网格；其中冲击恢复分支先改变冲击阈值和响应样本形成，不能把本轮不可评价误写成因子失败。
