#pragma once
#include <string>
#include <vector>
#include "factors/_comm/factor_entry_base.h"
namespace factors { namespace impact_efficiency {
static const std::string kFactorSetName="impact_efficiency";
static const size_t kFactorSize=1;
static const std::vector<std::string> kFactorNames={"impact_efficiency_signed_price_impact_w16"};
static const comm::FactorMetadata kFactorMetadata={kFactorSetName,kFactorSize,kFactorNames};
inline const comm::FactorMetadata& GetMetadata(){return kFactorMetadata;}
} }
