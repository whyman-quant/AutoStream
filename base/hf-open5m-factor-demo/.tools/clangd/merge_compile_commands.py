#!/usr/bin/env python3
import argparse
import json
import os
from typing import Dict, List, Tuple

# ============================= 使用说明 =============================
# 本脚本通常由 update_compdb.sh 调用，你也可以手工调用：
#
#   python3 .tools/clangd/merge_compile_commands.py \
#     --output compile_commands.json \
#     .clangd-build/live/compile_commands.json \
#     .clangd-build/factor/compile_commands.json \
#     .clangd-build/model/compile_commands.json
#
# 适用场景：
#   - 同一仓库包含多个 app（live/factor/model）时，将多份 compile DB 合并成一份；
#   - 降低 clangd 对文件“推断编译命令”导致的误配概率。
# ===================================================================


# 将路径规范化为真实路径（解析软链），避免 /home 与 /mnt 混用时出现“同一文件多条目”。
def _norm_path(path: str) -> str:
    return os.path.realpath(path)


# compile_commands 里可能用 command 或 arguments 两种格式，这里统一抽取为文本便于打分。
def _entry_cmd_text(entry: dict) -> str:
    if "command" in entry and isinstance(entry["command"], str):
        return entry["command"]
    if "arguments" in entry and isinstance(entry["arguments"], list):
        return " ".join(str(x) for x in entry["arguments"])
    return ""


# 对同一 file 的多个候选编译命令打分。
# 目标：尽量选中“属于同一 app 子树”的命令，减少 clangd inferred command 误配。
def _score(entry: dict, file_path: str) -> int:
    cmd = _entry_cmd_text(entry)
    score = 0

    # 优先规则1：文件位于哪个 app_* 子树，就优先命中对应 BUILD_APP_* 或 include 路径。
    if "/app_live/" in file_path:
        if "BUILD_APP_LIVE" in cmd or "/app_live" in cmd:
            score += 100
    elif "/app_factor/" in file_path:
        if "BUILD_APP_FACTOR" in cmd or "/app_factor" in cmd:
            score += 100
    elif "/app_model/" in file_path:
        if "BUILD_APP_MODEL" in cmd or "/app_model" in cmd:
            score += 100

    # 优先规则2：带有 generated 或 .clangd-build 路径的命令，通常上下文更完整。
    if "/build/generated" in cmd or "/.clangd-build/" in cmd:
        score += 10

    return score


# 读取并校验单个 compile DB。
def _load_db(path: str) -> List[dict]:
    with open(path, "r", encoding="utf-8") as f:
        data = json.load(f)
    if not isinstance(data, list):
        raise ValueError(f"{path} is not a compile_commands list")
    return data


# 合并多个 compile DB：
# - 先按规范化后的 file 路径分组；
# - 每组保留一个“最高分”命令；
# - 保持首次出现顺序，尽量稳定 diff。
def merge_compile_dbs(inputs: List[str]) -> List[dict]:
    grouped: Dict[str, List[dict]] = {}
    order: List[str] = []

    for db_path in inputs:
        for entry in _load_db(db_path):
            file_path = entry.get("file")
            if not file_path:
                continue
            key = _norm_path(file_path)
            if key not in grouped:
                grouped[key] = []
                order.append(key)
            grouped[key].append(entry)

    merged: List[dict] = []
    for key in order:
        candidates = grouped[key]
        best = max(candidates, key=lambda e: _score(e, key))
        merged.append(best)

    return merged


def main() -> int:
    parser = argparse.ArgumentParser(
        description="合并多份 compile_commands.json 为一份（供 clangd 使用）"
    )
    parser.add_argument(
        "--output",
        required=True,
        help="输出 compile_commands.json 路径",
    )
    parser.add_argument(
        "inputs",
        nargs="+",
        help="输入 compile_commands.json 文件列表",
    )
    args = parser.parse_args()

    merged = merge_compile_dbs(args.inputs)
    out_path = args.output
    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(merged, f, indent=2, ensure_ascii=False)
        f.write("\n")

    # 输出统计信息，方便在任务面板里快速确认结果。
    print(f"[merge_compile_commands] merged {len(args.inputs)} files -> {out_path}")
    print(f"[merge_compile_commands] total entries: {len(merged)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
