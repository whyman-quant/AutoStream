# AutoStream 统一版本边界与挖掘闭环设计

## 目标

将 AutoStream 组织成一个可追踪、可复现、可审计的统一工程：同一个 Git commit 能同时定位计算底座、候选谱系、数据快照、因子输出、回测结果和晋级结论。

## 已确认事实

- 顶层没有 `.git`；现有 Git 仓库位于 `base/hf-open5m-factor-demo`。
- `app_factor` 已支持历史 Tick/Trans/Order 回放、流式 FactorEntry 计算和 HDF5 因子输出。
- 研究控制面、campaign、candidate manifest、provenance、pilot 和断点审计曾在历史分支中实现，但没有进入当前 main 的统一运行路径。
- 当前没有可确认的正式收益回测、训练/验证/留出和晋级成绩单。

## 架构边界

研报/文献 → 研究蓝图 → 候选谱系 → C++ 流式因子 → HDF5/Arrow → 数据质量 → IC/分组 → 风险/成本 → 训练 → 验证 → 留出 → 晋级注册表。

顶层职责：`base/` 是计算底座；`campaigns/` 是研究账本和候选谱系；`evaluations/` 是独立评价器；`research/` 是来源资料；`data/` 默认只保留 manifest/hash；`docs/` 是运行和验收文档。

## 版本策略

1. 顶层使用唯一远端 `git@github.com:whyman-quant/AutoStream.git`。
2. `base/hf-open5m-factor-demo` 作为普通目录并入，不使用 submodule。
3. 忽略构建目录、模型二进制、原始 HDF5/Arrow、日志、Slurm 工作目录和缓存。
4. 大型结果以路径、大小、SHA256 和生成 commit 的 manifest 管理。
5. 正式晋级必须记录 campaign、family、candidate、代码 commit、配置 hash、数据快照和评价器版本。

## 本周末验收顺序

1. 顶层 Git 初始化、忽略规则、底层历史导入和远端连接审计。
2. 恢复历史研究控制面到隔离分支，确认候选/谱系/状态文件能够读取。
3. 用真实单日完成行情到 HDF5 再到逐列质量检查。
4. 实现最小回测器并输出可审计 JSON/TSV。
5. 单日闭环通过后才启动 24 日 pilot；pilot 通过后再申请完整历史。

## 非目标

本轮不直接提交完整历史 Slurm 任务，不把 13G 构建/模型/原始数据整体纳入 Git，也不把旧 family-0001 技术证据当作正式投资表现。
