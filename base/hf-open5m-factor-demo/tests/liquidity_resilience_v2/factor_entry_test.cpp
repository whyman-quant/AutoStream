#include "factors/liquidity_resilience_v2/factor_entry.h"
#include <cmath>
#include <vector>

namespace {
Stock_Internal_Book Quote(uint32_t bid, uint32_t ask, uint32_t bv, uint32_t av) {
    Stock_Internal_Book q{}; q.bp_array[0]=bid; q.ap_array[0]=ask;
    q.bv_array[0]=bv; q.av_array[0]=av; return q;
}
}

int main() {
    factors::comm::FactorEntryConfig cfg;
    // A rising-only window has no drawdown, so it must not be classified as
    // shock recovery merely because its first value is far below a later peak.
    factors::liquidity_resilience_v2::FactorEntry rising(
        "000001", factors::liquidity_resilience_v2::GetMetadata(), cfg);
    for (uint32_t volume = 10; volume <= 160; volume += 10) {
        rising.AddQuote(Quote(10000, 10100, volume, volume));
    }
    const auto& rising_only = rising.UpdateFactors(100000000);
    if (!std::isnan(rising_only.at(8))) return 1;

    factors::liquidity_resilience_v2::FactorEntry e("000001", factors::liquidity_resilience_v2::GetMetadata(), cfg);
    // Values before the declared warmup are not valid zeros.
    e.AddQuote(Quote(10000, 10100, 100, 100));
    const auto& cold = e.UpdateFactors(92700000);
    if (cold.size() != 12 || !std::isnan(cold.at(0)) || !std::isnan(cold.at(8))) return 1;
    const auto cold_ready = e.GetReadinessMask(92700000);
    if (cold_ready.size() != 12 || cold_ready.at(0) || cold_ready.at(8)) return 1;
    // A large drop establishes a shock, but speed remains unavailable until recovery.
    for (int i=0;i<16;++i) e.AddQuote(Quote(10000, 10100, 100, 100));
    e.AddQuote(Quote(10000, 10100, 10, 10));
    const auto& shocked = e.UpdateFactors(100000000);
    if (!std::isnan(shocked.at(8))) return 1;
    e.AddQuote(Quote(10000, 10100, 60, 60));
    const auto& recovering = e.UpdateFactors(100000000);
    if (!(std::isfinite(recovering.at(8)) && recovering.at(8) > 0.0)) return 1;
    if (!e.GetReadinessMask(100000000).at(8)) return 1;
    return 0;
}
