# GitHub 远端审计

- 远端：`git@github.com:whyman-quant/AutoStream.git`
- 本地分支：`main`
- 当前提交：以 `git rev-parse HEAD` 为准（已持续推送研究控制面与评价器增量）
- HTTPS 只读探测：成功，未返回 refs，远端看起来为空。
- SSH 认证：使用仓库级 `core.sshCommand` 和 `~/.ssh/id_ed25519_autostream` 成功认证为 `whyman-quant`。
- 推送：已成功推送 `main`；远端 `refs/heads/main` 与本地提交哈希一致。
- 当前配置：`ssh -i ~/.ssh/id_ed25519_autostream -o IdentitiesOnly=yes`。

底层旧仓库的完整 71 refs 已归档在本地 `.migration/hf-open5m-factor-demo.bundle`，该文件不进入顶层 Git。
