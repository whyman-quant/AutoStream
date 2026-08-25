#pragma once

#include <string>
#include <vector>

#include "factors/_comm/factor_entry_base.h"

namespace factors {
namespace book_imbalance {

static const std::string kFactorSetName = "book_imbalance";
static const size_t kFactorSize = 12;
static const std::vector<std::string> kFactorNames = {
    "book_imbalance_weighted_depth_imbalance_w16_v0_lag0",
    "book_imbalance_microprice_displacement_w32_v0_lag1",
    "book_imbalance_spread_conditioned_imbalance_w64_v0_lag2",
    "book_imbalance_weighted_depth_imbalance_w128_v1_lag0",
    "book_imbalance_microprice_displacement_w16_v1_lag1",
    "book_imbalance_spread_conditioned_imbalance_w32_v1_lag2",
    "book_imbalance_weighted_depth_imbalance_w64_v2_lag0",
    "book_imbalance_microprice_displacement_w128_v2_lag1",
    "book_imbalance_spread_conditioned_imbalance_w16_v2_lag2",
    "book_imbalance_weighted_depth_imbalance_w32_v3_lag0",
    "book_imbalance_microprice_displacement_w64_v3_lag1",
    "book_imbalance_spread_conditioned_imbalance_w128_v3_lag2",
};

static const comm::FactorMetadata kFactorMetadata = {
    kFactorSetName, kFactorSize, kFactorNames};
inline const comm::FactorMetadata& GetMetadata() { return kFactorMetadata; }

}  // namespace book_imbalance
}  // namespace factors
