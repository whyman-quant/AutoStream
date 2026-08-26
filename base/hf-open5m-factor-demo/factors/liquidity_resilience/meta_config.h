#pragma once

#include <string>
#include <vector>

#include "factors/_comm/factor_entry_base.h"

namespace factors {
namespace liquidity_resilience {

static const std::string kFactorSetName = "liquidity_resilience";
static const size_t kFactorSize = 1;
static const std::vector<std::string> kFactorNames = {
    "liquidity_resilience_spread_adjusted_depth_recovery_w16",
};
static const comm::FactorMetadata kFactorMetadata = {
    kFactorSetName, kFactorSize, kFactorNames};
inline const comm::FactorMetadata& GetMetadata() { return kFactorMetadata; }

}  // namespace liquidity_resilience
}  // namespace factors
