#include <cmath>
#include <cstdlib>

#include "factors/market_microstructure/factor_entry.h"

int main() {
    using factors::market_microstructure::FactorEntry;
    using factors::market_microstructure::GetMetadata;
    factors::comm::FactorEntryConfig config;
    FactorEntry entry("600000", GetMetadata(), config);
    for (bool value : entry.GetReadinessMask(92600000)) {
        if (value) return 3;
    }
    FactorEntry shallow("600001", GetMetadata(), config);
    Stock_Internal_Book shallow_quote{};
    shallow_quote.bp_array[0] = 999900;
    shallow_quote.ap_array[0] = 1000100;
    shallow_quote.bv_array[0] = 1000;
    shallow_quote.av_array[0] = 1000;
    shallow.AddQuote(shallow_quote);
    shallow.UpdateFactors(92600000);
    if (shallow.GetReadinessMask(92600000)[0] ||
        !std::isnan(shallow.GetFactorValues()[0])) return 4;
    Stock_Internal_Book quote{};
    quote.bp_array[0] = 999900;
    quote.ap_array[0] = 1000100;
    quote.bv_array[0] = 1000;
    quote.av_array[0] = 1000;
    quote.bp_array[1] = 999800;
    quote.ap_array[1] = 1000200;
    quote.bv_array[1] = 1000;
    quote.av_array[1] = 1000;
    quote.bp_array[2] = 999700;
    quote.ap_array[2] = 1000300;
    quote.bv_array[2] = 1000;
    quote.av_array[2] = 1000;
    entry.AddQuote(quote);
    entry.UpdateFactors(92600000);
    const auto& values = entry.GetFactorValues();
    const auto ready = entry.GetReadinessMask(92600000);
    if (values.size() != 12 || !ready[0] || !ready[1] ||
        !std::isfinite(values[0]) || !std::isfinite(values[1])) return 1;
    for (size_t i = 2; i < values.size(); ++i) {
        if (ready[i] || !std::isnan(values[i])) return 2;
    }
    return 0;
}
