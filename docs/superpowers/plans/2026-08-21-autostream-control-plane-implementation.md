# AutoStream Control Plane Implementation Plan

> For agentic workers: use subagent-driven-development or executing-plans to implement this plan task by task.

**Goal:** 建立统一 Git 边界，并恢复候选→流式因子→历史输出→回测→经验账本的最小闭环。

**Architecture:** 顶层仓库是唯一版本边界；现有 C++ 底座并入 `base/` 普通目录；研究控制面和评价器分别放在 `campaigns/` 与 `evaluations/`。

**Tech Stack:** Git/subtree、C++/CMake/HDF5、Python 3.8、JSON/TSV/Parquet。

---

### Task 1: 建立顶层 Git 边界

- [ ] 检查 nested worktree 和用户改动。
- [ ] 创建顶层 `.gitignore`，排除 build、模型、原始数据、日志、Slurm、缓存。
- [ ] 初始化顶层 `main`。
- [ ] 提交仓库布局和忽略策略。

### Task 2: 导入底层历史为普通目录

- [ ] 生成并验证 nested repository bundle。
- [ ] 以 subtree 等价方式导入到 `base/hf-open5m-factor-demo/`。
- [ ] 检查顶层 index 不含 mode `160000`。
- [ ] 验证底层历史可追踪。

### Task 3: 远端和可复现性审计

- [ ] 添加 `origin=git@github.com:whyman-quant/AutoStream.git`，暂不 push。
- [ ] 记录 HTTPS 可见性、SSH host key 和认证结果。
- [ ] 运行 `make build-factor` 与 `main --version`。

### Task 4: 恢复研究控制面

- [ ] 比较 `codex/sfm-autoresearch-pipeline`、`codex/sfm-campaign-cli` 等历史分支。
- [ ] 先恢复 manifest、provenance、状态读取和一个真实候选 family。
- [ ] 记录 schema 1/2 与正式 schema 3 的差异。

### Task 5: 最小回测评价器

- [ ] 为 HDF5 元数据、逐列质量门槛、warmup NaN 和 valid zero 编写测试。
- [ ] 为 RankIC、分组收益、换手、Sharpe、最大回撤和 Calmar 编写确定性测试。
- [ ] 实现 `evaluations/run_factor_eval.py`，无标签时明确输出 `data_missing`。

### Task 6: 实验账本和晋级闸门

- [ ] 定义追加式状态和机器可读 reject reason。
- [ ] 定义真实单日 acceptance：逐列覆盖率、未来信息、HDF5 schema、provenance。
- [ ] 将训练、验证、留出和成本后风险指标设为 promotion 必需条件。

### Task 7: 验证与推送

- [ ] 检查 status、submodule mode、build 和 evaluator tests。
- [ ] 本地审计通过后才 push；远端认证失败时记录错误，不声称已更新。

