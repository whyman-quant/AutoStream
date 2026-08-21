#!/bin/bash

# 因子创建脚本
# 用途：从模板创建新的因子模块
# 使用方法：./create_factor.sh <factor_name>
# 示例：./create_factor.sh my_factor
#
# 权限设置：
# 如果遇到 "权限不够" 错误，请先为脚本添加可执行权限：
#   chmod +x create_factor.sh
# 或者：
#   chmod 755 create_factor.sh

set -e  # 遇到错误立即退出

# 检查参数
if [ $# -ne 1 ]; then
    echo "Usage: $0 <factor_name>"
    echo "Example: $0 my_factor"
    exit 1
fi

FACTOR_NAME=$1
TEMPLATE_DIR="_template"
TARGET_DIR="$FACTOR_NAME"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# 切换到脚本所在目录
cd "$SCRIPT_DIR"

# 检查因子名称合法性
if [[ ! $FACTOR_NAME =~ ^[a-zA-Z][a-zA-Z0-9_]*$ ]]; then
    echo "Error: Factor name must start with a letter and contain only letters, numbers, and underscores."
    exit 1
fi

# 检查目标目录是否已存在
if [ -d "$TARGET_DIR" ]; then
    echo "Error: Directory $TARGET_DIR already exists!"
    exit 1
fi

# 检查模板目录是否存在
if [ ! -d "$TEMPLATE_DIR" ]; then
    echo "Error: Template directory $TEMPLATE_DIR not found!"
    echo "Please ensure the template directory exists in the factors directory."
    exit 1
fi

# 复制模板目录
echo "Creating new factor module: $FACTOR_NAME"
cp -r "$TEMPLATE_DIR" "$TARGET_DIR"

# 替换模板中的占位符（meta_config.h 中 FactorSetNameStillPlaceholder 使用逐字符数组承载
# 默认名，避免此处整体替换 template_name 时误伤「是否仍为占位名」的比较逻辑）
echo "Customizing factor module..."
find "$TARGET_DIR" -type f \( -name "*.h" -o -name "*.cc" -o -name "*.cpp" -o -name "CMakeLists.txt" \) \
    -exec sed -i "s/template_name/$FACTOR_NAME/g" {} \;

find "$TARGET_DIR" -type f \( -name "*.h" -o -name "*.cc" -o -name "*.cpp" \) \
    -exec sed -i "s|factors/_template/|factors/$FACTOR_NAME/|g" {} \;

echo ""
echo "Done. New factor module: $TARGET_DIR"
echo "Next: edit meta_config.h / factor_entry.h / factor_entry.cpp; 注册宏在 factor_entry.h 末尾，与 demo0000 等示例一致。"
