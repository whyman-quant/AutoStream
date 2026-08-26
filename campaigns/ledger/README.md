# 实验账本

账本采用追加式 JSONL/TSV。每条记录必须带 campaign、family、candidate、代码 commit、配置 hash、数据快照和评价器版本。

状态分为：`generated`、`compiled`、`technical_pass`、`technical_reject`、`pilot_pass`、`pilot_reject`、`observation_only`、`formal_observation`、`backtest_pass`、`backtest_reject`、`holdout_pass`、`promoted`、`crash`、`timeout`、`data_missing`、`lookahead_reject`、`duplicate_reject`。

`observation_only` 表示只形成观察证据，不允许据此淘汰或晋级；`formal_observation` 表示已形成正式长历史画像，但尚未冻结候选池。只有 `promoted` 状态允许 `promotion_allowed=true`，其他状态必须为 `false` 或省略该字段。历史记录省略该字段时按 `false` 解释。

技术验收通过不等于投资验收通过。没有收益标签必须进入 `data_missing`，旧 family 的技术证据必须标记为 `technical_evidence_only`。
