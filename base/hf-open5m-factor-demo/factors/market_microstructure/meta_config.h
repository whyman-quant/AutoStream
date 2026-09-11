#pragma once

#include <string>
#include <vector>

#include "factors/_comm/factor_entry_base.h"

namespace factors {
namespace market_microstructure {

static const std::string kFactorSetName = "market_microstructure";
static const size_t kFactorSize = 12;
static const std::vector<std::string> kFactorNames = {
    "counterfactual_depth_fragility_shock10_v1",
    "counterfactual_depth_fragility_shock25_v1",
    "trade_pair_parent_size_mismatch_volume_weighted_v1",
    "trade_pair_parent_size_mismatch_equal_weight_v1",
    "resting_order_commitment_survival_age_weighted_v1",
    "resting_order_commitment_survival_no_age_v1",
    "cancel_execution_divergence_separate_norm_v1",
    "cancel_execution_divergence_joint_norm_v1",
    "execution_shock_replenishment_order_elasticity_v1",
    "execution_shock_replenishment_quote_elasticity_v1",
    "distinct_aggressor_run_surprisal_markov_v1",
    "distinct_aggressor_run_surprisal_marginal_v1",
};

static const comm::FactorMetadata kFactorMetadata = {
    kFactorSetName, kFactorSize, kFactorNames};
inline const comm::FactorMetadata& GetMetadata() { return kFactorMetadata; }

}  // namespace market_microstructure
}  // namespace factors
