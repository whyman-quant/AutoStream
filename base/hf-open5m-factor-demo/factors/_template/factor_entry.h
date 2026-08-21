#pragma once

// ============================================================================
// 因子入口类声明（实现见 factor_entry.cpp）
//
// 回调约定（与 FactorEntryBase 一致）：
// - DoOnAddQuote / DoOnAddTrans / DoOnAddOrder：逐笔行情路径，按引擎投递顺序调用；可累积状态。
// - DoOnUpdateFactors：在配置的触发时刻写入 fvals_；截面时注意按资产块布局写入。
// - 可选 DoOnGlobalTime / DoOnAfterUpdateFactors 未在本模板展开，需要时参考 democsy00 等。
//
// 注册宏必须放在本头文件末尾（与仓库内 demo0000、demosy00 等保持一致）。
//
// {可删：本文件顶部长注释块；熟悉约定后整段可删，仅保留 include 与类声明。}
// {推荐删：无。}
// ============================================================================

#include "factors/_comm/factor_entry_base.h"
#include "factors/_comm/factor_entry_registry.h"
// {可删：复制到新目录后，将路径中 _template 改为目录名；改完可删本行说明。}
#include "factors/_template/meta_config.h"

namespace factors {
namespace template_name {

// {可删：类前说明性注释；稳定后可删。}
// 因子实现类：每个资产（或截面任务）一个实例，由引擎创建并驱动上述回调。
class FactorEntry : public comm::FactorEntryBase {
   public:
    // 构造函数：asset 与 metadata、config 由引擎传入；基类会初始化 fvals_ 长度与因子名列表。
    FactorEntry(const std::string& asset, const comm::FactorMetadata& metadata,
                const comm::FactorEntryConfig& config);

   private:
    // 快照行情；字段含义见 sdp_handler 侧行情结构体定义。
    void DoOnAddQuote(const Stock_Internal_Book& quote) override;

    // 逐笔成交（新格式）。
    void DoOnAddTrans(
        const Stock_Transaction_Internal_Book_New& quote) override;

    // 逐笔委托（新格式）。
    void DoOnAddOrder(const Stock_Order_Internal_Book_New& quote) override;

    // 在触发时刻把本因子集所有输出写入 fvals_（长度已由基类按元数据分配好）。
    void DoOnUpdateFactors(int64_t timestamp) override;
};

}  // namespace template_name
}  // namespace factors

// {可删：二选一。若不用 StaticInit，改用下一行 REGISTER_FACTOR_AUTO，并删本行与 meta/cpp 中 StaticInit。}
// 使用带静态初始化的因子注册宏（与 meta_config.h 中 StaticInit 声明配对）。
REGISTER_FACTOR_WITH_STATIC_INIT(template_name, FactorEntry)

// {可删：若已选上一行 WITH_STATIC_INIT，则下面两行整段注释可删。}
// 如果因子不需要静态初始化，使用这个宏，并删除 meta 中 StaticInit 声明与 cpp 中 StaticInit 定义：
// REGISTER_FACTOR_AUTO(template_name, FactorEntry)
