#include "factors/_template/factor_entry.h"

// {可删：若删尽 StaticInit 与构造/DoOnUpdateFactors 中的 runtime_error，可删本 include。}
#include <stdexcept>

// {可删：若改用 REGISTER_FACTOR_AUTO 且无 StaticInit，则本文件内 StaticInit 整函数可删；同时删 meta 声明与 h 中 WITH 宏。}
// 本编译单元提供 StaticInit 与 FactorEntry 成员定义；注册宏在 factor_entry.h 末尾。

namespace factors {
namespace template_name {

// {推荐删：占位名校验与「date 非空」等示例校验；业务稳定后只保留 EV 预加载等真正需要的逻辑，其余可删。}
// 静态初始化：在任意 FactorEntry 实例创建前执行；抛错可使启动失败并暴露配置问题。
void StaticInit(const comm::FactorEntryConfig& config) {
    // {可删：若已保证配置与改名流程，FactorSetNameStillPlaceholder 分支及 meta 中该辅助函数可一并删。}
    // 防止复制目录后忘记改 kFactorSetName，仍与脚手架默认名相同。
    if (FactorSetNameStillPlaceholder(kFactorSetName)) {
        throw std::runtime_error(
            "因子集: meta_config.h 中 kFactorSetName 仍为默认占位，请改为与目录名、命名空间一致的名称。");
    }
    // 交易日由 JSON 解析注入；为空通常表示配置或命令行未传 date。
    if (config.date.empty()) {
        throw std::runtime_error(
            "因子集: FactorEntryConfig.date 为空，请检查 JSON 是否传入交易日等运行配置。");
    }
    // 读 EV 时优先使用 config.ev_path（新字段）；ev_paths 为兼容旧配置，新代码可逐步迁移。
    (void)config.ev_path;
    (void)config.ev_paths;
}

FactorEntry::FactorEntry(const std::string& asset, const comm::FactorMetadata& metadata,
                         const comm::FactorEntryConfig& config)
    : comm::FactorEntryBase(asset, metadata, config) {
    // {推荐删：清单完成且 k_factor_template_acknowledged 已 true 后，整段 if 与 meta 中该常量一并删。}
    // 非截面且未完成清单确认时，仅允许占位代码 000000，避免未读完模板就接入全市场资产。
    // 截面因子由 asset_codes 驱动，勿套用本段；截面元数据见 democs00。
    if (!k_factor_template_acknowledged && !metadata.is_cross_sectional) {
        const std::string code = asset.size() >= 6 ? asset.substr(0, 6) : std::string();
        if (code != "000000") {
            throw std::runtime_error(
                "因子集: k_factor_template_acknowledged 为 false 时，仅接受资产代码 000000 作为占位联调；"
                "或先完成 meta_config.h 清单并改为 true 后删除 factor_entry.cpp 构造内本段校验。");
        }
    }
}

void FactorEntry::DoOnAddQuote(const Stock_Internal_Book& quote) {
    (void)quote;
    // {可删：(void)quote；若函数体内有真实读 quote 的逻辑，可删此行。}
    // 若本因子集依赖快照，在此实现；不需要则保持空实现。
}

void FactorEntry::DoOnAddTrans(
    const Stock_Transaction_Internal_Book_New& quote) {
    (void)quote;
    // {可删：(void)quote；有真实逻辑后删。}
    // 逐笔成交路径：可做成交量累计、主动买卖方向等；不需要则空实现。
}

void FactorEntry::DoOnAddOrder(const Stock_Order_Internal_Book_New& quote) {
    (void)quote;
    // {可删：(void)quote；有真实逻辑后删。}
    // 逐笔委托路径：可做委托簿特征等；不需要则空实现。
}

void FactorEntry::DoOnUpdateFactors(int64_t timestamp) {
    (void)timestamp;
    // {可删：(void)timestamp；若使用 timestamp 参与计算可删。}
    // {推荐删：清单完成后整段 if/throw 与 meta 中 k_factor_template_acknowledged 一并删。}
    // 未完成清单确认前禁止写因子，避免静默输出全零被误认为已上线逻辑。
    if (!k_factor_template_acknowledged) {
        throw std::runtime_error(
            "因子集: 请实现 DoOnUpdateFactors 内业务逻辑，并在 meta_config.h 将 "
            "k_factor_template_acknowledged 改为 true；否则拒绝产出因子。");
    }
    // {推荐删：下面 for 占位写零；有真实因子公式后整段替换，勿长期保留全零上线。}
    // 占位：正式实现中按列语义写入 fvals_[i]；截面时索引需按「资产 × 每资产因子数」展开。
    for (size_t i = 0; i < fvals_.size(); ++i) {
        fvals_[i] = static_cast<fval_t>(0);
    }
}

}  // namespace template_name
}  // namespace factors
