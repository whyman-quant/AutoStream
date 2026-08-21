# GitHub 远端审计

- 远端：`git@github.com:whyman-quant/AutoStream.git`
- 本地分支：`main`
- 当前本地提交：`3995225`（统一源码仓库初始提交）
- HTTPS 只读探测：成功，未返回 refs，远端看起来为空。
- SSH 认证：失败，`Permission denied (publickey)`；本机没有可用于 GitHub 的私钥。
- HTTPS 推送：失败，当前环境没有 GitHub username/token 凭据。
- 结论：本地版本边界已建立，但远端尚未更新。配置 GitHub SSH key 或 credential helper 后执行 `git push -u origin main`。

底层旧仓库的完整 71 refs 已归档在本地 `.migration/hf-open5m-factor-demo.bundle`，该文件不进入顶层 Git。
