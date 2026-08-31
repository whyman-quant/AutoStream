# L4 Recent-History Production Design

## 目标和边界

为第一轮 48 个候选生产可审计的 L4 正式历史画像，同时把实际回放范围从 2017–2024 缩短为最近四年。保留已冻结的 `formal_history_v1`，创建实际执行用的 v2；不覆盖 v1，不读取、生产或评价 2025 holdout。

## 数据集 v2

- 数据集 ID：`sfm_stream_001_formal_history_v2`。
- training：`20210104–20221230`，485 个交易日。
- observation：`20230103–20241231`，484 个交易日。
- L4 实际生产：969 个交易日。
- holdout：`20250102–20251210`，228 个交易日，保持封存至 L6。
- 日期仍取 v1 行情/标签交集清单的子集，并冻结独立日期文件与 SHA-256。
- v2 成为 campaign 的 active formal-history dataset；v1 保留为 superseded definition，不删除。

## 统一 48 因子回放

每个交易日只读取一次行情，在同一进程中按以下顺序启用四个家族：

1. `book_imbalance`；
2. `flow_pressure`；
3. `liquidity_resilience`；
4. `impact_efficiency`。

输出必须包含 48 个因子，名称与四个 Batch 的 `candidate_ids` 顺序拼接完全一致。输出根目录与 Pilot 隔离：

`data/sfm_autoresearch_001/formal-history-v2/[DATE]/all_families/factors.h5`

冻结一个 clean release 二进制和配置 SHA。调度申请 12 CPU、243G 内存，JSON 内部计算线程为 8，为数据加载和扫描线程留出余量。

## 五日技术预检

正式批量生产前仅运行五日：

- `20210104`：training 起点；
- `20220301`：training 中段；
- `20221230`：training 终点；
- `20230103`：observation 起点；
- `20241231`：observation 终点。

每一天都必须通过：48 个名称和顺序一致、8 个事件完整、值全部有限、事件内股票代码唯一、事件间股票数量一致、Arrow 行数等于股票数乘 8、`(symbol,event)` 唯一。记录运行时间、最大内存、二进制/配置/转换器 SHA。预检失败时停止，不提交 969 天生产。

## 批量生产

预检通过后，将 969 天按每批 5 个交易日分组。每批内部顺序执行，最多 4 批并发；同一并发槽使用 `afterok` 串行依赖。已经存在且严格校验通过的日期可以幂等跳过；存在但校验失败的输出不得自动覆盖，必须人工复核。

任何日期非零退出都会阻断该槽后续任务。禁止使用 2025 日期，提交器必须从 v2 的 production date list 读取日期，而不是接受任意起止日期。

## 评价和画像

969 天全部严格转换后，对统一 48 因子目录运行六组评价：

- 标签：`raw926`、`ease926`；
- 股票池：`000985`、`003800`、`000906`。

training 和 observation 分开生成画像。允许比较 observation 的稳定性，但不允许据此修改当前候选、窗口、方向或生成下一 Batch。画像至少包含 RankIC、IC、D1–D10、LS、单调性、均值/波动/IR、正负比例、年度/季度分层、事件点稳定性、标签/股票池一致性、参数邻域、因子值相关性、IC 序列相关性和异常日贡献。

L4 仍为 `formal_observation`，`promotion_allowed=false`。2025 holdout 不生产、不转换、不评价。

## 失败和停止条件

以下情况硬停止：日期越出 v2 清单、固定哈希变化、因子名/顺序变化、事件缺失、重复键、NaN/Inf、时间戳或因果性不一致、未知运行失败。以下情况暂停复核：单日输出缺失超过 1%、任一 training/observation 指标覆盖低于 95%、某年度或季度被静默遗漏、参数邻域高度不稳或同批高度重复。

L4 完成要求是 48 个候选均具备 training 和 observation 的完整正式画像、数据质量记录、谱系和异常贡献；不以 RankIC 正负作为完成或淘汰条件。

## 测试和审计

- 契约测试验证 v2 是 v1 的严格子集、969/485/484/228 数量、日期哈希和 holdout 封存。
- 配置测试验证四个家族顺序、48 个 Batch 名称和隔离输出目录。
- 预检测试验证五日 HDF5/Arrow 的 48×8 结构。
- 提交器测试验证只读取 v2 production dates、5 日分批、最多 4 路和 `afterok` 行为。
- 全量提交前运行 Campaign、Evaluation 和 C++ 回归。
