#pragma once

#include <string>
#include <vector>

#include "factors/_comm/factor_entry_base.h"

namespace factors {
namespace flow_pressure {

static const std::string kFactorSetName = "flow_pressure";
static const size_t kFactorSize = 16;
static const std::vector<std::string> kFactorNames = {
    "flow_pressure_signed_trade_flow_w16",
    "flow_pressure_signed_trade_flow_w32_lag0",
    "flow_pressure_signed_trade_flow_w64_lag1",
    "flow_pressure_signed_trade_flow_w128_lag2",
    "flow_pressure_trade_flow_zscore_w16_lag0",
    "flow_pressure_trade_flow_zscore_w32_lag0",
    "flow_pressure_trade_flow_zscore_w64_lag1",
    "flow_pressure_trade_flow_zscore_w128_lag2",
    "flow_pressure_decayed_trade_flow_w16_lag0",
    "flow_pressure_decayed_trade_flow_w32_lag0",
    "flow_pressure_decayed_trade_flow_w64_lag1",
    "flow_pressure_decayed_trade_flow_w128_lag2",
    "flow_pressure_l4g1_param_a",
    "flow_pressure_l4g1_param_b",
    "flow_pressure_l4g1_mechanism_a",
    "flow_pressure_l4g1_mechanism_b",
};

static const comm::FactorMetadata kFactorMetadata = {
    kFactorSetName, kFactorSize, kFactorNames};
inline const comm::FactorMetadata& GetMetadata() { return kFactorMetadata; }

}  // namespace flow_pressure
}  // namespace factors
