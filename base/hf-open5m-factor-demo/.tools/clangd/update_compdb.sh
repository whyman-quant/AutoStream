#!/usr/bin/env bash
# Bash 严格模式：-e 遇错即停；-u 未定义变量报错；-o pipefail 管道任一子命令失败即失败。
set -euo pipefail

# ============================= 使用说明 =============================
# 常见入口（推荐）：
#   1) make clangd-db
#   2) VSCode/ Cursor 任务：refresh-clangd-compile-db
#
# 也可直接运行本脚本：
#   bash ./.tools/clangd/update_compdb.sh
#
# 可选环境变量：
#   BUILD_TYPE=Debug|Release   （默认 Debug）
#   GENERATOR=Ninja|Unix Makefiles （默认 Ninja）
# 示例：
#   BUILD_TYPE=Release bash ./.tools/clangd/update_compdb.sh
#
# 执行结果：
#   - 在 .clangd-build/* 下生成各 app 的 compile_commands.json
#   - 在 .clangd-build/compile_commands.json 生成统一数据库供 clangd 使用
# ===================================================================

# 本脚本用于为 clangd 生成“稳定且可覆盖多 app 场景”的 compile_commands.json。
# 设计目标：
# 1) 不影响用户默认构建目录 build（只在隐藏目录 .clangd-build 下工作）；
# 2) 自动探测当前仓库包含哪些 app_* 目录（live/factor/model）；
# 3) 单 app 项目直接复用该 app 的 compile DB，多 app 项目自动合并。

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd -P)"
WORK_DIR="${ROOT_DIR}/.clangd-build"
MERGE_PY="${ROOT_DIR}/.tools/clangd/merge_compile_commands.py"
OUT_DB="${WORK_DIR}/compile_commands.json"

# 允许通过环境变量覆盖，便于在 CI 或不同机器上复用。
BUILD_TYPE="${BUILD_TYPE:-Debug}"
GENERATOR="${GENERATOR:-Ninja}"

apps=()
# 自动探测：发布后若只保留一个 app_* 目录，也能正常工作。
if [[ -d "${ROOT_DIR}/app_live" ]]; then apps+=("live"); fi
if [[ -d "${ROOT_DIR}/app_factor" ]]; then apps+=("factor"); fi
if [[ -d "${ROOT_DIR}/app_model" ]]; then apps+=("model"); fi

if [[ ${#apps[@]} -eq 0 ]]; then
  echo "[update_compdb] no app directories found (app_live/app_factor/app_model)" >&2
  exit 1
fi

mkdir -p "${WORK_DIR}"
declare -a db_files=()

# 对单个 app 进行 CMake configure，并生成 compile_commands.json。
# 注意这里只做 configure，不做实际编译，速度更快，也不会污染默认 build 行为。
run_configure() {
  local app="$1"
  local bdir="${WORK_DIR}/${app}"
  local live="OFF" factor="OFF" model="OFF"

  case "$app" in
    live) live="ON" ;;
    factor) factor="ON" ;;
    model) model="ON" ;;
    *) echo "[update_compdb] unknown app: ${app}" >&2; exit 1 ;;
  esac

  mkdir -p "${bdir}"
  echo "[update_compdb] configure ${app} -> ${bdir}"
  # 只进行配置阶段（configure），不触发编译，速度更快。
  cmake -S "${ROOT_DIR}" -B "${bdir}" -G "${GENERATOR}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DBUILD_APP_LIVE="${live}" \
    -DBUILD_APP_FACTOR="${factor}" \
    -DBUILD_APP_MODEL="${model}" \
    -DTIME_STATS=ON >/dev/null

  # 每个 app 都应产出一份 compile DB；缺失则直接失败，避免后续合并出错。
  local db="${bdir}/compile_commands.json"
  if [[ -f "${db}" ]]; then
    db_files+=("${db}")
  else
    echo "[update_compdb] missing compile DB: ${db}" >&2
    exit 1
  fi
}

for app in "${apps[@]}"; do
  run_configure "${app}"
done

# 单 app：直接复制；
# 多 app：调用 Python 脚本做“按文件去重 + 优先匹配”合并。
if [[ ${#db_files[@]} -eq 1 ]]; then
  cp "${db_files[0]}" "${OUT_DB}"
  echo "[update_compdb] single app mode: copied ${db_files[0]} -> ${OUT_DB}"
else
  python3 "${MERGE_PY}" --output "${OUT_DB}" "${db_files[@]}"
fi

echo "[update_compdb] done. clangd compile DB: ${OUT_DB}"
