#pragma once
#include <string>
#include <vector>
#include "factors/_comm/factor_entry_base.h"
namespace factors { namespace impact_efficiency {
static const std::string kFactorSetName="impact_efficiency";
static const size_t kFactorSize=12;
static const std::vector<std::string> kFactorNames={
"impact_efficiency_signed_price_impact_w16","impact_efficiency_signed_price_impact_w32","impact_efficiency_signed_price_impact_w64","impact_efficiency_signed_price_impact_w128",
"impact_efficiency_mid_price_response_w16","impact_efficiency_mid_price_response_w32","impact_efficiency_mid_price_response_w64","impact_efficiency_mid_price_response_w128",
"impact_efficiency_ofi_response_w16","impact_efficiency_ofi_response_w32","impact_efficiency_absorption_divergence_w16","impact_efficiency_absorption_divergence_w32"};
static const comm::FactorMetadata kFactorMetadata={kFactorSetName,kFactorSize,kFactorNames};
inline const comm::FactorMetadata& GetMetadata(){return kFactorMetadata;}
} }
