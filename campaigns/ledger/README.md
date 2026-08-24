# 实验账本

账本采用追加式 JSONL/TSV。每条记录必须带 campaign、family、candidate、代码 commit、配置 hash、数据快照和评价器版本。

状态分为：`generated`、`compiled`、`technical_pass`、`technical_reject`、`pilot_pass`、`pilot_reject`、`backtest_pass`、`backtest_reject`、`holdout_pass`、`promoted`、`crash`、`timeout`、`data_missing`、`lookahead_reject`、`duplicate_reject`。

技术验收通过不等于投资验收通过。没有收益标签必须进入 `data_missing`，旧 family 的技术证据必须标记为 `technical_evidence_only`。
