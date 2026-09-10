#include "factors/liquidity_resilience/factor_entry.h"
#include <cmath>
#include <iostream>
#include <vector>
namespace {
Stock_Internal_Book Quote(uint32_t bid, uint32_t ask, uint32_t bid_volume, uint32_t ask_volume) {
    Stock_Internal_Book quote{};
    quote.bp_array[0] = bid; quote.ap_array[0] = ask;
    quote.bv_array[0] = bid_volume; quote.av_array[0] = ask_volume;
    return quote;
}
double Value(factors::liquidity_resilience::FactorEntry& entry) {
    return entry.UpdateFactors(100000000).at(0);
}
std::vector<double> Values(factors::liquidity_resilience::FactorEntry& entry) {
    return entry.UpdateFactors(100000000);
}
bool Near(double actual, double expected) {
    return (std::isnan(actual) && std::isnan(expected)) || std::abs(actual - expected) <= 1e-12;
}
}
int main() {
    if (factors::liquidity_resilience::GetMetadata().factor_size != 16 ||
        factors::liquidity_resilience::GetMetadata().factor_names.size() != 16) return 1;
    factors::comm::FactorEntryConfig config;
    const std::vector<std::string> expected_names = {
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
        "liquidity_resilience_l4g1_param_a",
        "liquidity_resilience_l4g1_param_b",
        "liquidity_resilience_l4g1_mechanism_a",
        "liquidity_resilience_l4g1_mechanism_b",
    };
    if (factors::liquidity_resilience::GetMetadata().factor_names != expected_names) return 1;

    factors::liquidity_resilience::FactorEntry entry("000001", factors::liquidity_resilience::GetMetadata(), config);
    // Values before the declared warmup are unavailable, not zero.
    entry.AddQuote(Quote(10000, 10100, 100, 100));
    const auto cold = Values(entry);
    if (cold.size() != 16 || !std::isnan(cold.at(0)) || !std::isnan(cold.at(8))) return 1;
    const auto cold_ready = entry.GetReadinessMask(92700000);
    if (cold_ready.size() != 16 || cold_ready.at(0) || cold_ready.at(8)) return 1;

    // A rising-only window has no drawdown and must not be classified as shock recovery.
    factors::liquidity_resilience::FactorEntry rising("000001", factors::liquidity_resilience::GetMetadata(), config);
    for (uint32_t volume = 10; volume <= 160; volume += 10) rising.AddQuote(Quote(10000, 10100, volume, volume));
    const auto rising_values = Values(rising);
    if (!std::isnan(rising_values.at(8)) || !std::isnan(rising_values.at(12)) ||
        rising.GetReadinessMask(100000000).at(12)) return 1;

    // A large drop establishes a shock, but speed remains unavailable until recovery.
    for (int i = 0; i < 16; ++i) entry.AddQuote(Quote(10000, 10100, 100, 100));
    entry.AddQuote(Quote(10000, 10100, 10, 10));
    if (!std::isnan(Values(entry).at(8))) return 1;
    entry.AddQuote(Quote(10000, 10100, 60, 60));
    const auto recovering = Values(entry);
    if (!(std::isfinite(recovering.at(8)) && recovering.at(8) > 0.0)) return 1;
    if (!entry.GetReadinessMask(100000000).at(8)) return 1;
    factors::liquidity_resilience::FactorEntry invalid("000001", factors::liquidity_resilience::GetMetadata(), config);
    invalid.AddQuote(Quote(20000, 10000, 100, 100));
    const double invalid_value = Value(invalid);
    if (!std::isnan(invalid_value) || invalid.GetReadinessMask(100000000).at(0)) return 1;
    factors::liquidity_resilience::FactorEntry left("000001", factors::liquidity_resilience::GetMetadata(), config);
    factors::liquidity_resilience::FactorEntry right("000001", factors::liquidity_resilience::GetMetadata(), config);
    for (int index = 0; index < 4; ++index) {
        const auto quote = Quote(10000, 10100, 100 + index, 100 + index);
        left.AddQuote(quote); right.AddQuote(quote);
        if (!Near(Value(left), Value(right))) return 1;
    }
    right.AddQuote(Quote(10000, 11000, 1, 1));
    if (!std::isnan(Value(left))) return 1;
    return 0;
}
