#pragma once

#include <string>
#include <vector>

#include "factors/_comm/factor_entry_base.h"

namespace factors {
namespace flow_pressure {

static const std::string kFactorSetName = "flow_pressure";
static const size_t kFactorSize = 1;
static const std::vector<std::string> kFactorNames = {
    "flow_pressure_signed_trade_flow_w16",
};

static const comm::FactorMetadata kFactorMetadata = {
    kFactorSetName, kFactorSize, kFactorNames};
inline const comm::FactorMetadata& GetMetadata() { return kFactorMetadata; }

}  // namespace flow_pressure
}  // namespace factors
