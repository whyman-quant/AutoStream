# AutoStream Mixed Research Harness Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现一个可恢复的单命令研究 Harness，将四个机制家族的 16 个下一代 Candidate 一次混跑至 L4 画像、2025 样本外效果图、Experience Record 和下一轮 Candidate/Batch，然后停止。

**Architecture:** 用 `research_round` 预注册契约和追加式 stage receipt 驱动现有 L2/L3/L4 工具，不重写生产器。一个混合 Release 在每次行情回放中同时输出 48 个父因子和 16 个新因子；证据仍按四家族及 `factor × event × universe × label × split` 分开。2025 由独立、可审计的 holdout-display 门禁控制，仅在 2023–2024 冻结入选、方向和每个股票池×label 的最优 event 后开放。

**Tech Stack:** C++17 factor modules, CMake/CTest, Python 3.8/3.10, JSON Schema, HDF5, Arrow/Feather, pandas/numpy/matplotlib, Slurm/mybatch, unittest, SHA256, Feishu custom bot.

---

## 文件结构

- `campaigns/contracts/research_round.schema.json`：研究预注册、阶段、选择和 holdout 边界契约。
- `campaigns/research_harness.py`：单命令 `plan/start/status/resume` 控制器；只编排现有工具。
- `campaigns/research_round.py`：纯函数状态机、原子收据、hash 和阶段门禁。
- `campaigns/sfm_stream_001/rounds/l4g1_mixed_v1.json`：本轮冻结预注册。
- `campaigns/sfm_stream_001/manifests/l4g1-mixed-holdout-authorization.json`：用户已确认的条件式 2025 展示授权。
- `evaluations/research_selection.py`：最小单元证据分级、因子展示入选和最优 event 冻结。
- `evaluations/effect_timeseries.py`：从日频评价产物构造 IC、多头和十分层累计时序。
- `evaluations/effect_plot.py`：每个入选因子的 6×3、18 子图 `coolwarm` PNG。
- `evaluations/round_report.py`：消费单元分级与新图表，不再把“有 pass cell”叫作有效。
- `evaluations/feishu_notify.py`：研究语言的开始/结束消息及图片发送清单。
- `base/hf-open5m-factor-demo/factors/{book_imbalance,flow_pressure,impact_efficiency,liquidity_resilience}/`：每家族新增 4 个实现和元数据。
- `base/hf-open5m-factor-demo/config_factor_sfm_stream_001_l4g1_mixed.json`：64 因子混合发布配置。
- 对应 `campaigns/tests/`、`evaluations/tests/` 和 `factors/tests/`：契约、状态机、泄漏、评价、绘图及 C++ 语义测试。

### Task 1: 冻结研究轮次契约和预注册

**Files:**
- Create: `campaigns/contracts/research_round.schema.json`
- Modify: `campaigns/contracts/__init__.py`
- Create: `campaigns/tests/test_research_round_contract.py`
- Create: `campaigns/sfm_stream_001/rounds/l4g1_mixed_v1.json`
- Create: `campaigns/sfm_stream_001/manifests/l4g1-mixed-holdout-authorization.json`

- [ ] **Step 1: 写失败的契约测试。**

  测试有效文档必须声明四个 Batch、16 个 Candidate、48 个父对照、L2/L3/L4/L6-display 阶段、8 events、3 universes、2 labels、training/observation/holdout 日期列表 hash、每家族假设、成功/拒绝/停止条件，以及 `promotion_allowed=false`。测试拒绝缺少父画像、允许自动递归、L4 可读 holdout 或把 2025 用于 selection 的文档。

- [ ] **Step 2: 运行测试并确认失败。**

  Run: `python3 -m unittest campaigns.tests.test_research_round_contract -v`

  Expected: FAIL，因为 `research_round` schema 尚未注册。

- [ ] **Step 3: 实现 schema 和注册。**

  `research_round.schema.json` 使用 Draft 7、`additionalProperties=false`，冻结 `round_id=l4g1_mixed_v1`、`candidate_count=16`、`control_count=48`、`events`、`universes`、`labels`、各阶段输入与可读 split、`stop_after=next_batch_generated`、`recursive=false` 和 `promotion_allowed=false`。

- [ ] **Step 4: 写入本轮预注册。**

  每个家族分别写研究问题、单变量对照、父 Candidate、预期支持事件、`supported/promising/unsupported/not_evaluable/error` 的预注册判断规则。本轮选择只读 2021–2024；holdout 授权记录用户在 2026-09-10 确认“名单与最优 event 冻结后可读取 2025 画图”，并明确禁止反馈。

- [ ] **Step 5: 验证并提交。**

  Run: `python3 -m unittest campaigns.tests.test_research_round_contract -v`

  Expected: PASS。

  Commit: `feat: freeze mixed research round contract`

### Task 2: 把 16 个 Candidate 从设计稿变成可执行研究变量

**Files:**
- Modify: `campaigns/sfm_stream_001/candidates/*/l4_formal_history_next_v1/*.json`
- Modify: `campaigns/sfm_stream_001/batches/l4_formal_history_next_v1_*.json`
- Modify: `campaigns/sfm_stream_001/manifests/formal-history-v3-readiness-next-candidate-implementation-readiness.json`
- Modify: `base/hf-open5m-factor-demo/factors/book_imbalance/{factor_entry.cpp,factor_entry.h,meta_config.h}`
- Modify: `base/hf-open5m-factor-demo/factors/flow_pressure/{factor_entry.cpp,factor_entry.h,meta_config.h}`
- Modify: `base/hf-open5m-factor-demo/factors/impact_efficiency/{factor_entry.cpp,factor_entry.h,meta_config.h}`
- Modify: `base/hf-open5m-factor-demo/factors/liquidity_resilience/{factor_entry.cpp,factor_entry.h,meta_config.h}`
- Modify/Create: `base/hf-open5m-factor-demo/factors/tests/*l4g1*test.cpp`
- Create: `base/hf-open5m-factor-demo/config_factor_sfm_stream_001_l4g1_mixed.json`
- Create: `campaigns/tests/test_l4g1_candidate_implementation.py`

- [ ] **Step 1: 写四家族失败的语义与一致性测试。**

  每个新 Candidate 测试：名称与 metadata 一致；公式/参数只改变预注册维度；父 Candidate 存在；warmup、lag、unsupported events 和 readiness 与实现一致；不使用未来事件；`ready=true` 必须 finite；真实零保留。Python 一致性测试要求 16 个唯一名称和 canonical hash 与 C++ metadata 完全一致。

- [ ] **Step 2: 运行测试并确认失败。**

  Run: `python3 -m unittest campaigns.tests.test_l4g1_candidate_implementation -v`

  Run: `ctest --test-dir base/hf-open5m-factor-demo/build-flow-pressure --output-on-failure`

  Expected: 新 Candidate 尚未出现在实现与 metadata，测试失败。

- [ ] **Step 3: 实现 book_imbalance 四个变体。**

  实现预注册的微价格偏离和价差条件变体；参数变体只改变窗口/深度。盘口不可定义时返回 unavailable，不把缺失写成零。保留父 12 个输出不变。

- [ ] **Step 4: 实现 flow_pressure 四个变体。**

  实现成交压力持续性、指数衰减和局部参数对照；窗口不足为 unavailable，成交方向未知的数据不进入状态。保留父 12 个输出不变。

- [ ] **Step 5: 实现 impact_efficiency 四个变体。**

  实现订单流价格响应和吸收差异变体；明确 09:27 的支持策略，不能用无价格响应的 0 冒充有效信号。保留父 12 个输出不变。

- [ ] **Step 6: 实现 liquidity_resilience 四个变体。**

  只在显式、向后可见的流动性冲击后计算恢复；实现冲击门槛、恢复窗口和 warmup 的单变量对照。没有冲击为 unavailable，不是零。保留父 12 个输出不变。

- [ ] **Step 7: 冻结混合配置和代码血缘。**

  配置同时启用四个家族，共 64 列（48 父对照 + 16 新 Candidate），更新 Candidate implementation path、source commit、canonical hash 和 readiness receipt 为 `implementation_complete/l2_allowed=true`。

- [ ] **Step 8: 构建、验证并提交。**

  Run: `cmake --build base/hf-open5m-factor-demo/build-flow-pressure -j 8`

  Run: `ctest --test-dir base/hf-open5m-factor-demo/build-flow-pressure --output-on-failure`

  Run: `python3 -m unittest campaigns.tests.test_l4g1_candidate_implementation -v`

  Expected: 全部 PASS，父 48 因子回归值未改变。

  Commit: `feat: implement l4g1 mixed candidates`

### Task 3: 实现可恢复的轮次状态机

**Files:**
- Create: `campaigns/research_round.py`
- Create: `campaigns/tests/test_research_round.py`

- [ ] **Step 1: 写状态机失败测试。**

  覆盖合法序列 `planned → released → l2_complete → l3_produced → l3_evaluated → l4_produced → l4_evaluated → selected → holdout_displayed → experience_generated → next_batch_generated → complete`。拒绝跳阶段、hash 漂移、重复提交、只凭文件存在恢复、选择前 holdout、完成后递归。

- [ ] **Step 2: 运行并确认失败。**

  Run: `python3 -m unittest campaigns.tests.test_research_round -v`

- [ ] **Step 3: 实现纯函数门禁与原子收据。**

  提供 `load_round`、`plan_transition`、`validate_receipt`、`advance`、`resume_point`、`sha256` 和 `atomic_write_json`。每个 receipt 记录输入 hash、输出 hash、开始/结束时间、Job IDs、重试、错误类别和 holdout access。

- [ ] **Step 4: 测试故障恢复。**

  模拟进程在外部提交前、提交后写收据前、部分日期成功和产物 hash 改变时中断。确认恢复不会重复提交已确认 Job，也不会接受未验收文件。

- [ ] **Step 5: 验证并提交。**

  Run: `python3 -m unittest campaigns.tests.test_research_round -v`

  Commit: `feat: add resumable research round state machine`

### Task 4: 实现单命令 Harness 编排器

**Files:**
- Create: `campaigns/research_harness.py`
- Create: `campaigns/tests/test_research_harness.py`
- Modify: `campaigns/l4_release.py`
- Modify: `campaigns/l4_production.py`

- [ ] **Step 1: 写命令和阶段编排失败测试。**

  测试 `plan` 不产生外部写入；`start` 冻结 Release 后发送开始通知并执行当前门禁；`status` 只读；`resume` 从收据恢复。使用 fake runner 验证精确调用 L2、17 日 L3、969 日 L4、转换、评价、画像、选择、holdout-display、Experience 和下一 Batch。

- [ ] **Step 2: 运行并确认失败。**

  Run: `python3 -m unittest campaigns.tests.test_research_harness -v`

- [ ] **Step 3: 实现适配层而非复制旧逻辑。**

  Harness 调用现有 `run_campaign_technical`、`l4_release`、`l4_production`、`l4_preflight`、`pilot_postprocess` 和评价模块。每一阶段都由 round receipt 输入/输出，不通过全局变量传状态。

- [ ] **Step 4: 实现 Candidate 级隔离和共享错误硬停。**

  单 Candidate 实现错误标记 `isolated`；共享数据、未来数据、分母、schema 或 hash 问题标记 `hard_stop`。Slurm 瞬时失败遵循冻结重试上限。

- [ ] **Step 5: 加入 dry-run 和命令预览。**

  `plan --json` 输出候选、日期、预计 Job 数、CPU/内存、依赖、输出根、holdout 门禁和每阶段命令；默认不提交。只有 `start --submit` 可调用 mybatch。

- [ ] **Step 6: 验证并提交。**

  Run: `python3 -m unittest campaigns.tests.test_research_harness -v`

  Commit: `feat: orchestrate mixed factor research round`

### Task 5: 实现最小单元证据分级和最优 event 冻结

**Files:**
- Create: `evaluations/research_selection.py`
- Create: `evaluations/tests/test_research_selection.py`
- Modify: `evaluations/l4_portrait.py`

- [ ] **Step 1: 写证据分级失败测试。**

  fixture 覆盖 16 因子×8 events×3 universes×2 labels×2 splits。断言状态互斥、负向稳定信号可 supported、not_ready 不计失败、training/observation 不一致为 promising/unsupported、data_error 硬停，且不产生全局总分。

- [ ] **Step 2: 写 selection 冻结测试。**

  只给 2021–2024 指标，按预注册选择 `selected_for_holdout_display`、冻结 raw_signed 方向和每个 universe×label 的 best event。随后注入极强 2025 指标，断言名单、方向、best event 和 selection receipt hash 不变。

- [ ] **Step 3: 运行并确认失败。**

  Run: `/usr/local/python3.8.10/bin/python3 -m unittest evaluations.tests.test_research_selection -v`

- [ ] **Step 4: 实现分类器和冻结收据。**

  输出每个最小单元的状态、原因、覆盖、RankIC/IC/LS/单调性、父因子增量和相关性；因子摘要保留 supported 范围而不是二元“有效”。best event 的决胜顺序预注册并确定性实现。

- [ ] **Step 5: 验证并提交。**

  Run: `/usr/local/python3.8.10/bin/python3 -m unittest evaluations.tests.test_research_selection -v`

  Commit: `feat: classify research evidence by cell`

### Task 6: 实现 2023–2025 累积时序数据

**Files:**
- Create: `evaluations/effect_timeseries.py`
- Create: `evaluations/tests/test_effect_timeseries.py`

- [ ] **Step 1: 写三类时序失败测试。**

  fixture 包含三年日期、8 events、D1–D10、RankIC、ready mask 和缺失日。断言 IC 为每日 RankIC 的累加；多头及各层为 `(1+r).cumprod()-1`；负向冻结因子多头取 Q1、正向取 Q10；`ready=false` 跳过不填 0；输出保留覆盖率和年度边界。

- [ ] **Step 2: 写 holdout 泄漏测试。**

  未提供合法 selection receipt 和 authorization 时，任何 2025 日期立即报错。合法时只允许读取已入选因子和冻结 best event，输出 hash 与访问日志。

- [ ] **Step 3: 运行并确认失败。**

  Run: `/usr/local/python3.8.10/bin/python3 -m unittest evaluations.tests.test_effect_timeseries -v`

- [ ] **Step 4: 实现时序生成器。**

  输出一份机器可读 Arrow/Parquet 和 JSON receipt；2023–2024 与 2025 使用同一因子方向、同一 event 选择和同一收益定义。

- [ ] **Step 5: 验证并提交。**

  Run: `/usr/local/python3.8.10/bin/python3 -m unittest evaluations.tests.test_effect_timeseries -v`

  Commit: `feat: build frozen factor effect time series`

### Task 7: 实现每因子 6×3 的 18 子图

**Files:**
- Create: `evaluations/effect_plot.py`
- Create: `evaluations/tests/test_effect_plot.py`
- Modify: `evaluations/round_report.py`
- Modify: `evaluations/tests/test_round_report.py`

- [ ] **Step 1: 写布局与语义失败测试。**

  断言每个入选因子只生成一张 PNG；18 个 axes；行顺序固定为 `000906/raw926`、`000906/ease926`、`003800/raw926`、`003800/ease926`、`000985/raw926`、`000985/ease926`；前两列各 8 条 event 曲线；第三列 Q1–Q10；全部 cmap 为 `coolwarm`；图中有 2024、2025 分界和 holdout 标识。

- [ ] **Step 2: 运行并确认失败。**

  Run: `/usr/local/python3.8.10/bin/python3 -m unittest evaluations.tests.test_effect_plot -v`

- [ ] **Step 3: 实现确定性绘图。**

  使用固定 figsize、DPI、字体、event 颜色位置和 Q1–Q10 颜色位置。每行第三列标题标出冻结 best event；覆盖不足用文字注释，不把 NaN 绘成零。

- [ ] **Step 4: 改正 round_report 的“有效”语义。**

  移除 `pass_cells > 0` 即有效的规则，改为读取 selection receipt；报告 supported/promising/unsupported/not_evaluable/error 单元，列出 supported 的具体维度和父因子增量。

- [ ] **Step 5: 像素级烟雾验收并提交。**

  读取 fixture PNG 验证尺寸、非空图元、legend、标题和 18 axes 元数据 sidecar。

  Commit: `feat: render 18-panel factor effect charts`

### Task 8: 集成飞书研究通知

**Files:**
- Modify: `evaluations/feishu_notify.py`
- Modify: `evaluations/tests/test_feishu_notify.py`
- Modify: `campaigns/research_harness.py`

- [ ] **Step 1: 写通知失败测试。**

  开始消息必须写四家族问题、变量、父对照、数据范围、成功/失败/停止条件和 holdout 状态。结束消息必须写单元状态数、supported 范围、父因子增量、下一轮动作、图表清单和 `promotion_allowed=false`；不得将 pass cell 数叫有效因子数。

- [ ] **Step 2: 运行并确认失败。**

  Run: `/usr/local/python3.8.10/bin/python3 -m unittest evaluations.tests.test_feishu_notify -v`

- [ ] **Step 3: 实现通知和收据。**

  webhook 只从 `AUTOSTREAM_FEISHU_WEBHOOK` 读取，不落库；发送结果、时间、payload hash 和失败原因进入 stage receipt。通知失败 fail-soft，不影响研究结论。

- [ ] **Step 4: 验证并提交。**

  Commit: `feat: notify mixed research round evidence`

### Task 9: 实现 Experience 和下一轮的确定性停止点

**Files:**
- Create: `campaigns/experience_generator.py`
- Create: `campaigns/tests/test_experience_generator.py`
- Modify: `campaigns/research_harness.py`

- [ ] **Step 1: 写生成器失败测试。**

  每家族至少一份 Experience；每条 fact 必须指向 cell/portrait hash；interpretation 与 fact 分开；action 有可证伪测试；下一 Batch 的每个 Candidate 有父画像和父 Experience。拒绝使用 2025 指标生成动作。

- [ ] **Step 2: 运行并确认失败。**

  Run: `python3 -m unittest campaigns.tests.test_experience_generator -v`

- [ ] **Step 3: 实现生成与停止。**

  生成预算按 supported 利用、promising 对照、unsupported 停止重复扩参、not_evaluable 可用性修正分配。输出下一 Batch 为 `proposed/design_only`，状态机随后进入 `complete`，不提交新 Batch。

- [ ] **Step 4: 验证并提交。**

  Commit: `feat: close research round with next hypotheses`

### Task 10: 全链路 fixture 和故障注入验收

**Files:**
- Create: `campaigns/tests/test_research_harness_e2e.py`
- Create: `evaluations/tests/fixtures/research_round/` 下的小型生成 fixture
- Modify: `docs/因子挖掘框架.md`

- [ ] **Step 1: 构造 3 因子、3 日期、2 event、2 universe、2 label、3 split 的微型端到端数据。**

  fixture 同时包含 supported、not_ready 和 error 注入版本，不包含真实行情或密钥。

- [ ] **Step 2: 一条命令跑完整 fixture。**

  Run: `python3 -m campaigns.research_harness start --round <fixture-round> --local-fixture`

  Expected: 自动生成各阶段 receipt、selection、holdout display、18 子图、Experience、next Batch，最终 `complete` 且 `recursive=false`。

- [ ] **Step 3: 注入关键故障。**

  分别注入 hash 漂移、重复键、ready=true/NaN、股票池分母变化、selection 前 holdout 访问、2025 反向修改 best event、Slurm 重复提交和图表缺列；每个故障必须在正确阶段停止，并保留先前可信收据。

- [ ] **Step 4: 更新框架文档。**

  增加混跑定义、研究/观察/holdout-display 边界、18 子图规范、飞书轮次消息和自动化停止点。

- [ ] **Step 5: 验证并提交。**

  Run: `python3 -m unittest campaigns.tests.test_research_harness_e2e -v`

  Commit: `test: certify mixed research harness end to end`

### Task 11: 冻结真实 Release，先跑 L2/L3 preflight

**Files:**
- Runtime output only under new `l4g1-mixed-v1` roots
- Create through tools: `campaigns/sfm_stream_001/manifests/l4g1-mixed-*.json`

- [ ] **Step 1: 运行完整本地测试。**

  Run: `python3 -m unittest discover -q campaigns/tests`

  Run: `/usr/local/python3.8.10/bin/python3 -m unittest discover -q evaluations/tests`

  Run: `ctest --test-dir base/hf-open5m-factor-demo/build-flow-pressure --output-on-failure`

  Run: `git diff --check`

- [ ] **Step 2: 冻结干净 commit 的混合 Release。**

  记录 64 因子顺序、二进制、配置、Candidate、Batch、研究轮次、转换器、评价器和日期表 SHA。Release 不包含 holdout 日期文件。

- [ ] **Step 3: 发送本轮开始飞书消息。**

  使用已经验证可工作的 webhook 环境变量；消息为研究问题和预注册，不是“开始开发”。记录 Feishu `code=0` 或 fail-soft 错误。

- [ ] **Step 4: 运行 L2 和 17 日 L3。**

  验证 64 因子、8 events、readiness、finite、唯一键、股票池分母和 12 个评价 panel。L3 只作技术/观察门禁，不据收益淘汰。

- [ ] **Step 5: 审核 L3 receipt。**

  只有 `data_error=0`、共享发布 hash 不漂移、事件和分母一致时进入 L4；否则 Harness 自动停止并报告。

### Task 12: 真实 L4、选择、2025 展示和研究闭环

**Files:**
- Runtime output only under versioned roots
- Create through tools: final receipts, portraits, selection, charts, Experience and next Batch

- [ ] **Step 1: 混跑 969 日 L4。**

  以受控并发一次回放 64 因子，生产/转换/评价 2021–2024；训练/观察面板、画像和 cell matrix 均校验后才选择。

- [ ] **Step 2: 冻结入选名单、方向和六个 best event。**

  每个入选因子在 3 universes×2 labels 上各冻结一个 2023–2024 best event。receipt 生成后只读，记录 hash。

- [ ] **Step 3: 执行独立的 2025 holdout-display。**

  验证用户授权和 selection hash 后，只生产/评价入选因子及必要父对照；不得重新选择。生成 2023–2025 的三类累计时序和每因子一张 18 子图。

- [ ] **Step 4: 生成 Experience 与下一轮 Candidate/Batch。**

  只使用 2021–2024 证据决定下一轮；2025 作为展示/审阅附件，不进入生成器输入。

- [ ] **Step 5: 发送结束飞书消息并正常停止。**

  消息列出 supported 范围、未支持/不可评价范围、父因子增量、图表路径和下一轮研究问题。最终 receipt 为 `complete`、`recursive=false`、`promotion_allowed=false`。

- [ ] **Step 6: 最终验证、提交和推送。**

  重新运行 Task 11 的全部测试，核对所有 runtime receipt/hash，提交小型研究产物并推送到 GitHub `main`；不提交 HDF5、Arrow、Parquet、PNG 大文件或 webhook。
