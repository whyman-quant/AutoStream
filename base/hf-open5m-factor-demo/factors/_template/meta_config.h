#pragma once

// ============================================================================
// 因子集元数据（复制本目录为新因子名后，与本目录 factor_entry.* 一起改）
//
// 职责：声明因子集名称、维度、列名、静态元数据与（可选）StaticInit；供注册表与
// FactorEntry 构造使用。命名空间名、目录名、kFactorSetName、REGISTER_* 宏须一致。
//
// 注意：以 _ 开头的目录不会被 factors/CMakeLists.txt 收录；仅作复制脚手架。
//
// {可删：本文件顶部长注释块；熟悉后整段可删。}
// {推荐删：无。}
// ============================================================================

#include <string>
#include <vector>

#include "factors/_comm/factor_entry_base.h"

namespace factors {
namespace template_name {

// ---------------------------------------------------------------------------
// {推荐删：熟练后本节整块「检查清单」注释可删，减轻噪音；清单内容建议保留在团队 wiki。}
// 从本目录复制出新因子集后的检查清单（逐项确认后再改闸门常量）
//
//  [ ] 目录名、命名空间、`kFactorSetName`、`REGISTER_*` 宏参数一致，且与 JSON
//      factors_config.factor_sets[].name 一致
//  [ ] 将 factor_entry.h / factor_entry.cpp 顶部的 include 路径里的 factors/_template/ 改为
//      factors/<目录名>/
//  [ ] 按业务设置 kFactorSize、kFactorNames（或继续用 GenerateFactorNames）
//  [ ] 截面因子：使用 FactorMetadata 四参数构造并填写 asset_codes 等（仓库内 democs00）
//  [ ] 在 factor_entry.cpp 中实现 DoOnAddQuote / DoOnAddTrans / DoOnAddOrder / DoOnUpdateFactors；
//      不需要的回调可留空实现，但 DoOnUpdateFactors 必须写出真实因子逻辑
//  [ ] 若无需 StaticInit：删本文件 StaticInit 声明、factor_entry.cpp 中 StaticInit 实现，
//      删 factor_entry.h 末尾 REGISTER_FACTOR_WITH_STATIC_INIT，并改用 REGISTER_FACTOR_AUTO
//  [ ] 若需预加载 EV：在 StaticInit 内用 config.ev_path / config.ev_paths 读盘，并处理异常
//  [ ] 非截面且 k_factor_template_acknowledged 为 false 时：构造仅接受占位代码 000000；
//      截面因子勿套用该规则，并应使用四参数 FactorMetadata
//  [ ] 将 k_factor_template_acknowledged 改为 true 后，DoOnUpdateFactors 才写因子值；
//      逻辑稳定后可删除 factor_entry.cpp 内对 !k_factor_template_acknowledged 的分支及构造内占位校验
// ---------------------------------------------------------------------------

// {推荐删：闸门常量；cpp 内相关分支删尽后，本行与常量定义一并删。}
// 闸门：完成清单前保持 false；与 StaticInit 占位名检测、构造体内占位资产、DoOnUpdateFactors 配合。
static const bool k_factor_template_acknowledged = false;

// 因子集在配置里的逻辑名；须与 JSON、命名空间、REGISTER 宏首参一致。
static const std::string kFactorSetName = "template_name";

// {可删：kFactorSize 旁「说明性」注释；业务稳定后可删。}
// 单个资产（或截面下单资产）输出的标量因子个数；总 fvals_ 长度在基类中按是否截面再乘资产数。
static const size_t kFactorSize = 2;

// {可删：kFactorNames 旁说明；若改手写列名可删上一段 Generate 相关说明。}
// 各列名称；可用 GenerateFactorNames 按 kFactorSetName 与 kFactorSize 自动生成，也可手写向量。
static const std::vector<std::string> kFactorNames =
    comm::GenerateFactorNames(kFactorSetName, kFactorSize);

// {可删：若手写列名，可删上一行 GenerateFactorNames，改为手写 static const vector 初始化。}
// 非截面：三参数构造即可（is_cross_sectional 默认为 false）。截面须用四参数并显式 true。
static const comm::FactorMetadata kFactorMetadata = {kFactorSetName, kFactorSize, kFactorNames};

// 注册表通过该函数取元数据；勿改名、勿改签名。
inline const comm::FactorMetadata& GetMetadata() { return kFactorMetadata; }

// {可删：若不用 StaticInit 内占位名检测，可删本函数及 cpp 中对应调用。}
// {推荐删：改名流程成熟后，通常与 StaticInit 首段校验一起删。}
// 判断 kFactorSetName 是否仍为脚手架默认名。用逐字符数组拼出字面量，避免整串 template_name
// 被批量替换脚本误改后失去「是否已改名」的判定能力。
inline bool FactorSetNameStillPlaceholder(const std::string& n) {
	const char k[] = {'t', 'e', 'm', 'p', 'l', 'a', 't', 'e', '_', 'n', 'a', 'm', 'e', '\0'};
	return n == k;
}

// {可删：若改用 REGISTER_FACTOR_AUTO，删本声明与 cpp 中 StaticInit 实现、h 中 WITH 宏。}
// 进程内、本因子集所有实例创建前调用一次；适合做 EV 预加载、全局表构建。实现写在 factor_entry.cpp。
void StaticInit(const comm::FactorEntryConfig& config);

}  // namespace template_name
}  // namespace factors
