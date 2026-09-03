#pragma once

#include <string>
#include <vector>

#include "factors/_comm/factor_entry_base.h"

namespace factors { namespace liquidity_resilience_v2 {
static const std::string kFactorSetName = "liquidity_resilience_v2";
static const size_t kFactorSize = 12;
// Event-count contract: recovery windows require a full window; lagged variants
// require window + lag observations. Shock-speed outputs remain unavailable
// until a drawdown of at least 20% followed by one recovery quote is observed.
static const size_t kRecoveryWarmupEvents[4] = {16, 32, 65, 130};
static const size_t kRecoveryLagEvents[4] = {0, 0, 1, 2};
static const double kShockThreshold = 0.8;
static const std::vector<std::string> kFactorNames = {
    "liquidity_resilience_v2_spread_adjusted_depth_recovery_w16",
    "liquidity_resilience_v2_spread_adjusted_depth_recovery_w32_lag0",
    "liquidity_resilience_v2_spread_adjusted_depth_recovery_w64_lag1",
    "liquidity_resilience_v2_spread_adjusted_depth_recovery_w128_lag2",
    "liquidity_resilience_v2_multi_level_depth_recovery_w16_lag0",
    "liquidity_resilience_v2_multi_level_depth_recovery_w32_lag0",
    "liquidity_resilience_v2_multi_level_depth_recovery_w64_lag1",
    "liquidity_resilience_v2_multi_level_depth_recovery_w128_lag2",
    "liquidity_resilience_v2_shock_recovery_speed_w16_lag0",
    "liquidity_resilience_v2_shock_recovery_speed_w32_lag0",
    "liquidity_resilience_v2_shock_recovery_speed_w64_lag1",
    "liquidity_resilience_v2_shock_recovery_speed_w128_lag2",
};
static const comm::FactorMetadata kFactorMetadata{kFactorSetName, kFactorSize, kFactorNames};
inline const comm::FactorMetadata& GetMetadata() { return kFactorMetadata; }
} }
