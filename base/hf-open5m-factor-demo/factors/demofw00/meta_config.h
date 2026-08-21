#pragma once

#include <string>
#include <vector>

#include "factors/_comm/factor_entry_base.h"

namespace factors {
namespace demofw00 {

static const std::string kFactorSetName = "demofw00";
static const size_t kFactorSize = 54;

static const std::vector<std::string> kFactorNames = {
    "demofw00_timestamp",
    "demofw00_last_quote_time",
    "demofw00_last_trade_time",
    "demofw00_last_order_time",
    "demofw00_quote_count",
    "demofw00_trade_count",
    "demofw00_order_count",
    "demofw00_last_price",
    "demofw00_best_bid",
    "demofw00_best_ask",
    "demofw00_spread",
    "demofw00_mid_price",
    "demofw00_ofi_basic",
    "demofw00_ofi_agg",
    "demofw00_ofi_pca",
    "demofw00_ofi_order",
    "demofw00_ofi_trade",
    "demofw00_ofi_zscore",
    "demofw00_ofi_trend_slope",
    "demofw00_ofi_state",
    "demofw00_ofi_purity_final",
    "demofw00_big_trade_flag",
    "demofw00_big_trade_zscore",
    "demofw00_big_trade_threshold",
    "demofw00_big_order_flag",
    "demofw00_big_order_zscore",
    "demofw00_big_order_threshold",
    "demofw00_synth_seen_orders",
    "demofw00_synth_events",
    "demofw00_synth_filtered_events",
    "demofw00_synth_last_original_volume",
    "demofw00_synth_last_delta_volume",
    "demofw00_synth_last_execution_rate",
    "demofw00_synth_last_estimated",
    "demofw00_synth_last_aggressor_side",
    "demofw00_order_id_velocity",
    "demofw00_order_id_gap_mean",
    "demofw00_order_pressure_buy",
    "demofw00_order_pressure_sell",
    "demofw00_trans_record_id_velocity",
    "demofw00_trans_record_id_gap_mean",
    "demofw00_trans_wash_probability",
    "demofw00_trans_pressure_buy",
    "demofw00_trans_pressure_sell",
    "demofw00_active_composite_score",
    "demofw00_active_momentum_score",
    "demofw00_active_flow_bias_score",
    "demofw00_active_volume_spike_score",
    "demofw00_active_terrain_score",
    "demofw00_chip_below_share",
    "demofw00_chip_above_share",
    "demofw00_chip_peak_share",
    "demofw00_chip_support_ticks",
    "demofw00_chip_resistance_ticks",
};

static const comm::FactorMetadata kFactorMetadata = {
    kFactorSetName, kFactorSize, kFactorNames};

inline const comm::FactorMetadata& GetMetadata() { return kFactorMetadata; }

}  // namespace demofw00
}  // namespace factors
