# Round 001 实现状态

本轮不继承旧画像、旧经验或旧回测结论，只复用生产、转换、评价等基础设施。

统一研究事件为：`926, 1000, 1030, 1100, 1130, 1330, 1400, 1430`。生产端 `92700` 是集合竞价快照，转换后记为研究事件 `926`。

## 本批逻辑与来源

逻辑来自盘口、逐笔成交和母单生命周期的数据语义审查，以及独立机制提出与反证审阅。六个问题分别研究：盘口反事实脆弱度、成交双方母单容量错配、挂单承诺存活、撤单与成交背离、冲击后补单弹性、去母单拆分后的主动方序列惊奇度。

每个机制只生成一个代表因子和一个单变量对照，共 12 个 Candidate；没有使用 `window × lag` 参数网格。

## 当前完成

- Agent Round、Operator Catalog、Coverage Space 和七阶段 Harness 已建立。
- 6 个 IdeaSpec 已通过结构去重和可证伪检查。
- 12 个 CandidateProposal 已冻结设计顺序；该顺序与 C++ metadata 一致。
- `OrderSnapshot.estimated` 已从沪市母单重构状态透传至 `TradePairEvent`，精确母单与估算母单可以分开。
- 新建独立 `market_microstructure` 因子集。
- 盘口反事实脆弱度的两个因子已经实现；有效双边三档盘口下输出有限值。
- 其余 10 个因子尚未接完状态机，当前明确输出 `NaN` 且 `readiness=false`，不会用 0 冒充信号。
- readiness reason codebook 已固定为机器可读 JSON，并已接通 `FactorEntry → RowWriter → CalculationThread → ScanThread → HDF5 → Arrow`；Arrow 辅助列为 `reason_<factor>`。
- 已完成 20251014 单股票真实数据 smoke：HDF5 8 个事件、12 列，reason 矩阵与 readiness 同形；Arrow 8 行、39 列，逐格满足 `ready == (reason == 0)`。

## 09:26 规则

只有两个纯盘口反事实因子条件支持 `926`。其余依赖委托或成交生命周期的 10 个因子只支持 `1000` 及以后事件；它们在 `926` 保留行，但不参与评价。

## 尚未完成

- 其余 10 个因子的因果状态机和质量覆盖计数。
- 12 因子 L2、17 日 L3、回测、经验总结和下一批逻辑。

因此本轮当前状态是 `code_implementation_in_progress`，不能提交生产、不能评价效果，也没有“有效因子”结论。
