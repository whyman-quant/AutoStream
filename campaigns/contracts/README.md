# AutoStream 研究数据契约

这里冻结因子挖掘控制面的四类 JSON 契约。当前版本均为 `schema_version: 1`，使用 JSON Schema Draft 7。

## 四类产物

| 契约 | 一句话含义 | 主要回答 |
|---|---|---|
| Candidate | 一个可独立计算的因子定义 | 它为什么存在、公式是什么、用什么数据、状态与可用性语义是什么 |
| Batch | 一轮有共同研究目的的候选集合 | 这一轮改变什么、由什么经验触发、预算和候选边界是什么 |
| Factor Portrait | 一个候选在固定数据集上的完整有符号画像 | 数据是否可靠、不同标签和股票池表现怎样、能否做阶段判断 |
| Experience Record | 从正式画像提炼出的事实、解释和可证伪动作 | 学到了什么、为什么这样解释、下一批如何验证 |

对应文件：

- `candidate.schema.json`
- `batch.schema.json`
- `factor_portrait.schema.json`
- `experience_record.schema.json`

`validate_document(kind, document)` 校验单个文件；`consistency.py` 校验 Candidate、Batch、idea 和 C++ metadata 之间的跨文件血缘。

## 冻结规则

1. 版本1的字段语义不再原地改变；不兼容变化必须增加 `schema_version` 并保留旧读取能力。
2. 文档顶层默认 `additionalProperties=false`，拼错字段不能静默进入账本。
3. Candidate 的研究方向固定为 `raw_signed`，不允许用短样本把方向改成正值。
4. Pilot Portrait 必须是 `scope=pilot`、`evidence_level=L3`、`decision.status=observation_only`、`promotion_allowed=false`。
5. Experience Record 必须分开记录事实、解释和动作。动作必须包含预期现象、否决条件和下一次使用的数据范围。
6. JSON Schema 只校验单文件形状；候选数量、C++ 因子名、canonical hash、receipt hash 和跨文件集合关系必须由一致性测试验证。

## 当前实例

- Batch：`campaigns/sfm_stream_001/batches/book_imbalance_seed_v1.json`
- Candidates：`campaigns/sfm_stream_001/candidates/book_imbalance/`
- Pilot Portraits：`campaigns/sfm_stream_001/portraits/pilot/book_imbalance/`

现有12个 `book_imbalance` Candidate 精确对应 C++ `kFactorNames`。历史名称中的 `lagN` 当前是公式缩放参数，不是事件读取延迟；Candidate 的公式保留这一事实，而 `availability.lag_events` 明确记录为0。

当前 Pilot Portrait 只迁移17日原始有符号指标。receipt 未包含成对相关性矩阵，因此 redundancy 标为 `not_measured_in_receipt`，不能杜撰相关性数值。
