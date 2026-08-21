# .vscode 配置说明

本目录下为 VSCode/Cursor 的构建、调试与代码分析配置。因 JSON 不支持注释，此处用本文档说明各文件用途。

## settings.json

- **clangd**：启用 clangd 语言服务，指定编译数据库目录为 `.clangd-build`，用于 C++ 代码补全、跳转、实时语法检查。
- **C_Cpp**：关闭 MSVC 系 IntelliSense/错误波浪线，避免与 clangd 重复或冲突。
- **cmake.configureOnOpen**：关闭打开工程时自动配置 CMake，避免与顶层 Makefile 构建流程冲突。
- **files.associations**：将部分 C 标准库头文件关联为 cpp，便于 clangd 正确解析。

## tasks.json

| 任务 label | 用途 |
|------------|------|
| **chmod-clangd-script** | 给 `.tools/clangd/update_compdb.sh` 加上可执行权限，供 refresh-clangd-compile-db 依赖。 |
| **refresh-clangd-compile-db** | 运行脚本在 `.clangd-build/` 下生成 compile_commands.json，供 clangd 使用；IDE 里 C++ 报错不准或头文件找不到时可运行此任务后重载窗口。 |
| **build-debug** | 执行 `make clean && make build DEBUG=1`，Debug 构建（默认组，可快捷键触发）。 |
| **build-release** | 执行 `make clean && make build`，Release 构建。 |
| **clean** | 执行 `make clean`，清理 build 目录。 |

## launch.json

- **Debug-factor**：调试因子应用可执行文件 `build/app_factor/main`；启动前会执行 `preLaunchTask: build-debug`。若想跳过编译直接调试，可注释掉该配置中的 `preLaunchTask` 行。
- **Debug-model**：调试模型应用可执行文件 `build/app_model/main`；同样可在配置中注释 `preLaunchTask` 以跳过编译。
- **Debug-live**：用 GDB（`cppdbg`）启动 Python，运行 `app_live/run_strategy.py`，在加载 `build/app_live/libstrategy.so` 后可在 `app_live` 下 C++ 源码中下断点；`cwd` 为 `app_live`，默认与脚本中的 `so_path`、`config_live.json` 路径一致。启动前同样会跑 `build-debug`。若本机只有 `python3.8` 等，请在 `launch.json` 里把 `program` 改成对应解释器路径。依赖模拟平台侧 Python 包（如 `my.simu_v3`），需环境已安装。

当前仓库若未构建 `app_factor` / `app_model`，前两项可能无对应可执行文件；**live 策略**请用 **Debug-live**。
