# AutoStream 最小评价器

`run_factor_eval.py` 负责把 FactorCalculationEngine 写出的 HDF5 转成可审计 JSON。当前支持：

- HDF5 `factordata` / `factorlist` 读取；
- 逐列 finite、NaN、Inf、zero 和 warmup 后覆盖率；
- 常数列拒绝；
- Spearman RankIC；
- 分组平均收益和 Q_last-Q_first 多空差；
- 无未来收益标签时明确返回 `data_missing`。

当前实现只依赖 Python 标准库和系统 `h5dump`，避免评价器受制于研究节点上的 Python 科学计算环境。

示例：

```bash
python3 -m evaluations.run_factor_eval \
  --factor-file data/sfm_autoresearch_001/acceptance/20251013/book_imbalance/20251013/092700.h5 \
  --output /tmp/autostream-factor-eval.json
```

没有 `--labels-csv` 时，输出可以证明技术质量，但不会声称因子有投资收益。标签 CSV 至少需要一列 `label`，行顺序必须与 HDF5 `factordata` 行顺序一致。
