# 研究控制面恢复记录

日期：2026-08-21

## 当前基线

- 顶层仓库提交：以 `git rev-parse HEAD` 为准；本次记录前基线为 `8d07a9f`
- 计算底座来源：`base/hf-open5m-factor-demo`
- 底层完整历史归档：`.migration/hf-open5m-factor-demo.bundle`
- 顶层工作树：无嵌套 Git、无已跟踪 HDF5/ONNX/Arrow/模型库文件
- `app_factor --version`：可运行，已注册 `demofw00`

## 历史控制面位置

底层归档仓库中已确认存在以下分支和内容：

- `codex/sfm-autoresearch-pipeline`：研究流水线后续修复和断点状态机；
- `codex/sfm-campaign-cli`：campaign/candidate CLI 与契约测试；
- `codex/streaming-factor-miner-control-plan`：冻结的流式因子挖掘控制计划；
- `codex/sfm-book-imbalance-real-tools`：真实盘口不平衡工具；
- `codex/sfm-alpha48-integration`：alpha48 流式算子集合；
- `codex/sfm-campaign-manifest`：campaign manifest；
- `codex/sfm-candidate-generator`：候选生成器；
- `codex/sfm-eval-adapter-agent`：评价适配器；
- `codex/sfm-production-runner-agent`：生产运行器。

## 已确认的正式断点

1. 正式血缘适配器存在跨机制去重和能力复核问题；
2. `family-0002` 尚未原子创建；
3. C++ 正式 schema 3 公式执行未闭环；
4. Builder 已存在但没有被正式候选编译器完整绑定；
5. 当前本地技术检查不能证明使用真实 20220222 行情；
6. 旧覆盖率门槛不能保证每一列有效；
7. 新 family 的 24 日 pilot 尚无正式报告；
8. 训练、验证、2025 留出和 promotion 尚无真实成绩单。

## 恢复顺序

第一批只恢复可以本地审计的控制面文件和一个候选族：

1. campaign manifest、candidate manifest、lineage 和 provenance 的读取；
2. `book_imbalance` 单日技术验收；
3. 逐列 coverage / finite / NaN / Inf / zero 检查；
4. 评价器输入契约和 `data_missing` 终态；
5. 单日 8 个 checkpoint 技术验收命令；
6. 补齐 RankIC、分组收益、累计/年化收益、波动、Sharpe、最大回撤、Calmar、换手核心指标；
7. 单日通过后再恢复 pilot runner。

暂不恢复：

- 完整历史 Slurm 提交；
- 自动晋级；
- 旧 `family-0001` 的投资表现结论；
- 任何没有当前 schema 和数据快照的旧结果。

## 恢复验收

恢复的每个文件必须记录来源分支、来源 commit、顶层导入 commit、schema 版本、是否允许进入正式晋级和未解决的 B1–B8 断点。

在没有真实收益标签和留出期成绩单之前，任何输出只能标记为 `technical_evidence_only`。

## 2026-08-24 进展

- `evaluations/run_campaign_technical.py` 已能扫描 20251013 的 8 个 HDF5 checkpoint，
  当前结果为 8/8 技术通过、12/12 因子列一致；由于没有按代码/事件对齐的未来收益标签，
  评价状态仍为 `data_missing`，`promotion_allowed=false`。
- 可用原始行情挂载已确认：`/mnt/beegfs_npqssd`、`/mnt/beegfs_npq107`；
  尚未确认与该 HDF5 行顺序绑定的 label/交易日收益产物，因此不把原始文件存在误报为可回测。
- 已确认并实际使用历史评价器的真实标签目录：
  `/home/fangwei/beta_team_share/sfutils/factor_zoo/data/arrow_label_zoo/huyifan/atan_day_myrisk_neuted_ease_926`。
  `raw926=v_1D_v_demean`、`ease926=v_1D_v_neuted`，均按 `symbol,event` 连接。
- 20251013 已完成 `raw926/ease926 × 000985/003800/000906` 六个单日评价，
  每个组合 12 因子 × 8 事件；成绩单仍标记 `single_day_acceptance`，不能替代分段回测。
