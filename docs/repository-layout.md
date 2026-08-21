# AutoStream 仓库布局

顶层 `/home/fangwei/mnt-ssd/AutoStream` 是唯一版本边界。

| 路径 | 职责 | Git 策略 |
|---|---|---|
| `base/hf-open5m-factor-demo/` | C++ 行情读取、因子引擎、FactorEntry、HDF5 输出 | 作为普通目录并入，保留历史归档 |
| `campaigns/` | 候选、谱系、状态机、实验账本、provenance | 提交小型 JSON/TSV/清单 |
| `evaluations/` | 标签、IC、分组收益、成本和风险评价 | 提交代码、测试、schema |
| `research/` | 研报和来源索引 | 按 SHA256 管理大文件 |
| `data/` | 原始数据和验收产物 | 原始大文件不提交，保留 manifest/hash |
| `docs/` | 架构、断点、运行和验收文档 | 提交 |

禁止把 `build/`、`build-*`、模型二进制、原始行情、运行日志、Slurm 工作目录、缓存和临时文件作为普通源码提交。
