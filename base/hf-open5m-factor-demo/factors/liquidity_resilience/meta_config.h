#pragma once

#include <string>
#include <vector>

#include "factors/_comm/factor_entry_base.h"

namespace factors {
namespace liquidity_resilience {

static const std::string kFactorSetName = "liquidity_resilience";
static const size_t kFactorSize = 12;
static const std::vector<std::string> kFactorNames = {
    "liquidity_resilience_spread_adjusted_depth_recovery_w16",
    "liquidity_resilience_spread_adjusted_depth_recovery_w32_lag0",
    "liquidity_resilience_spread_adjusted_depth_recovery_w64_lag1",
    "liquidity_resilience_spread_adjusted_depth_recovery_w128_lag2",
    "liquidity_resilience_multi_level_depth_recovery_w16_lag0",
    "liquidity_resilience_multi_level_depth_recovery_w32_lag0",
    "liquidity_resilience_multi_level_depth_recovery_w64_lag1",
    "liquidity_resilience_multi_level_depth_recovery_w128_lag2",
    "liquidity_resilience_shock_recovery_speed_w16_lag0",
    "liquidity_resilience_shock_recovery_speed_w32_lag0",
    "liquidity_resilience_shock_recovery_speed_w64_lag1",
    "liquidity_resilience_shock_recovery_speed_w128_lag2",
};
static const comm::FactorMetadata kFactorMetadata = {
    kFactorSetName, kFactorSize, kFactorNames};
inline const comm::FactorMetadata& GetMetadata() { return kFactorMetadata; }

}  // namespace liquidity_resilience
}  // namespace factors
