# 因子扩展说明

面向需要在 **`factors/`** 下新增或修改**具体因子集**的读者；与接入形态、JSON 字段总览、三种策略模式对照相关的说明在仓库根目录 **`README.md`**。

## 1. 目录与收录规则

- **一个子目录 = 一套因子集**（与配置里 `factor_sets` 的名称对应）。
- **目录名不得以下划线 `_` 开头**：`factors/CMakeLists.txt` 会忽略这类目录，因此 **`_comm`**、**`_share`**、**`_template`** 等不参与自动收录，仅作公共代码或脚手架。
- 本仓内示例目录包括 **`demo0000`**、**`demo0001`**、**`democs00`**（截面）、**`demosy00`**、**`reserved`** 等，新增因子时建议先对照其一复制或改模板。

## 2. 新建因子目录（从模板复制）

1. 在 **`factors/`** 下复制 **`_template`** 目录，重命名为你的因子集目录名（须以字母开头，仅字母、数字、下划线；且**不得**以下划线 `_` 开头，否则不会被 CMake 收录）。  
2. 在新目录内将 **`template_name`** 全部替换为目录名（命名空间、`kFactorSetName`、`REGISTER_*` 宏参数等）。  
3. 将头文件里 **`factors/_template/`** 形式的 include 路径改为 **`factors/<你的目录名>/`**。  
4. 按需修改 **`meta_config.h`**、**`factor_entry.h`**（及若拆出的 **`factor_entry.cpp`**），对照 **`demo0000`** 等现有因子集核对构造与注册方式。

## 3. 单套因子集内必备文件

| 文件 | 作用 |
|------|------|
| **`meta_config.h`** | `kFactorSetName`、`kFactorSize`、因子名列、`GetMetadata()`；可选 **`StaticInit(const comm::FactorEntryConfig&)`** |
| **`factor_entry.h` / `factor_entry.cpp`** | 继承 **`factors::comm::FactorEntryBase`** 的 **`FactorEntry`** 实现 |

因子类**必须**提供构造函数：

```cpp
FactorEntry(const std::string& asset, const comm::FactorMetadata& metadata,
            const comm::FactorEntryConfig& config);
```

## 4. 元数据与截面

- **`kFactorSetName`** 的字符串须与 **`factors_config`** 里启用的因子集名称一致（大小写一致）。
- **`comm::FactorMetadata`** 可用三参数构造（默认非截面），或四参数构造以 **`is_cross_sectional = true`** 标记截面因子集；截面含义与引擎线程分工见根目录 **`README.md`** 中数据流示意，实现参考 **`democs00/meta_config.h`**。

## 5. 须实现的回调（类型以基类为准）

基类 **`FactorEntryBase`** 要求子类实现：

- **`DoOnAddQuote(const Stock_Internal_Book& quote)`**
- **`DoOnAddTrans(const Stock_Transaction_Internal_Book_New& quote)`**
- **`DoOnAddOrder(const Stock_Order_Internal_Book_New& quote)`**
- **`DoOnUpdateFactors(int64_t timestamp)`**

可选覆写（基类默认空实现）：

- **`DoOnGlobalTime(int exch_time)`**
- **`DoOnAfterUpdateFactors(int64_t timestamp)`**

## 6. 注册宏

在 **`factor_entry.h`**（或对应实现单元）末尾二选一：

- **`REGISTER_FACTOR_AUTO(命名空间目录名, FactorEntry)`** — 无静态初始化。
- **`REGISTER_FACTOR_WITH_STATIC_INIT(命名空间目录名, FactorEntry)`** — 需在 **`meta_config.h`** 中提供 **`StaticInit`**，用于预加载 **`ev`** 文件、全局缓存等。

宏的第一个参数与 **`namespace factors { namespace 该名 { ... } }`** 一致，并与 **`kFactorSetName`** 在配置中可解析为同一套因子集。

## 7. `reserved` 因子集

**`reserved`** 为框架保留的辅助因子集（如 **`CodeInt`**、**`last_time`**、**`deadline`** 等列），**`kFactorSetName`** 为 **`RESERVED`**。新业务因子集请使用自有名称，勿覆盖或重复其职责。

## 8. 与配置、EV 的关系

- 运行期 **`FactorEntryConfig`**（日期、**`ev_paths`** 等）来自 JSON 解析；**`ev`** 键与路径约定见 **`docs/ev_management.md`**。
- **`StaticInit`** 内读取 **`config.ev_paths`** 等时，须与因子 **`ev`** 配置一致，并做好异常处理。

## 9. 开发 Checklist（精简）

- [ ] 命名空间唯一，避免 **`common` / `utils`** 等易冲突短名。
- [ ] 避免在头文件中使用无项目前缀的 **`#define`**；优先常量、内联函数、**`enum class`**。
- [ ] 新增头文件使用 **`#pragma once`** 或带 **`FACTORS_<目录名>_...`** 风格的全局唯一 include guard。
- [ ] **`kFactorSetName` / `GetMetadata` / 注册宏** 三者与 JSON 一致。
- [ ] 重新编译后 **`./build/app_factor/main --version`** 或 Live 侧 **`--version`** 中能看到该因子集名称。

## 10. 常见问题

- **从 `_template` 复制后跑不起来**：模板内 **`k_factor_template_acknowledged`**、占位名检测与 **`000000`** 资产闸门见 **`factors/_template/meta_config.h`** 与 **`factor_entry.h`** 注释；按清单改完后再编译。
- **配置里写了名称但 `--version` 没有**：检查子目录名是否被 **`_` 规则**排除、是否缺少注册宏、是否未重新配置/编译。
- **链接或重复符号**：检查命名空间、宏、**`#ifndef`** 是否与已有因子集冲突。
- **截面与非截面行为不符合预期**：核对 **`FactorMetadata`** 是否显式指定了 **`is_cross_sectional`**。
