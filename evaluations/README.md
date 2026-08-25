# AutoStream 最小评价器

`run_factor_eval.py` 负责把 FactorCalculationEngine 写出的 HDF5 转成可审计 JSON。当前支持：

- HDF5 `factordata` / `factorlist` 读取；
- 逐列 finite、NaN、Inf、zero 和 warmup 后覆盖率；
- 常数列拒绝；
- Spearman RankIC；
- 分组平均收益和 Q_last-Q_first 多空差；
- 组合收益序列的累计收益、年化收益、年化波动、Sharpe、最大回撤、Calmar 和可选换手率；
- 无未来收益标签时明确返回 `data_missing`。

当前实现只依赖 Python 标准库和系统 `h5dump`，避免评价器受制于研究节点上的 Python 科学计算环境。

示例：

```bash
python3 -m evaluations.run_factor_eval \
  --factor-file data/sfm_autoresearch_001/acceptance/20251013/book_imbalance/20251013/092700.h5 \
  --output /tmp/autostream-factor-eval.json
```

没有 `--labels-csv` 时，输出可以证明技术质量，但不会声称因子有投资收益。标签 CSV 至少需要一列 `label`，行顺序必须与 HDF5 `factordata` 行顺序一致。

`portfolio_metrics` 是回测层的无依赖核心函数：传入按时间排序的十进制收益序列，
可选传入连续持仓权重向量（换手定义为相邻权重向量 L1 变化的一半）。
它不负责撮合、手续费或标签构造；这些必须由上游回测适配器显式完成，并在成绩单中记录数据快照、
成本模型和分段（training/validation/holdout）。

## 真实标签单日验收

现有历史评价工具的标签契约已落在 [label_contract.json](./label_contract.json)：
`raw926` 使用 `v_1D_v_demean`，`ease926` 使用 `v_1D_v_neuted`，文件来自
`atan_day_myrisk_neuted_ease_926/YYYYMMDD.arrow`，按 `symbol,event` 连接。

`run_real_label_scorecard.py` 从六个结果目录（两个标签 × 三个股票池）生成机器可读成绩单。
该成绩单只汇总有符号的 `D1-D10/LS/Monotonicity/IC/RankIC`，禁止在单日 acceptance
阶段选择方向或晋级：

```bash
python3.8 -m evaluations.run_real_label_scorecard \
  --result-root data/sfm_autoresearch_001/evaluation/20251013 \
  --output campaigns/sfm_stream_001/manifests/real-label-scorecard-20251013.json \
  --date 20251013 --expected-factor-count 12 \
  --factor-input data/sfm_autoresearch_001/acceptance/20251013/book_imbalance-arrow/20251013.arrow \
  --label-input /home/fangwei/beta_team_share/sfutils/factor_zoo/data/arrow_label_zoo/huyifan/atan_day_myrisk_neuted_ease_926/20251013.arrow \
  --label-contract evaluations/label_contract.json \
  --evaluator-source /home/fangwei/mnt-ssd/factor_eval_toolkit/scripts/evaluate_factors.py
```

Parquet 输入必须精确包含 20251013 的八个事件、单一日期、完整的四类指标和一致的因子集合；
scorecard 会记录六个结果文件的 SHA-256，以及 factor/label/contract/evaluator 输入哈希。
