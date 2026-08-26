#include "factors/liquidity_resilience/factor_entry.h"
#include <cmath>
#include <iostream>
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
bool Near(double actual, double expected) { return std::abs(actual - expected) <= 1e-12; }
}
int main() {
    factors::comm::FactorEntryConfig config;
    factors::liquidity_resilience::FactorEntry entry("000001", factors::liquidity_resilience::GetMetadata(), config);
    entry.AddQuote(Quote(10000, 10100, 100, 100));
    if (!Near(Value(entry), 1.0)) return 1;
    entry.AddQuote(Quote(10000, 10300, 20, 20));
    const double shock = Value(entry);
    if (!(shock > 0.0 && shock < 0.5)) return 1;
    entry.AddQuote(Quote(10000, 10100, 100, 100));
    if (!Near(Value(entry), 1.0)) return 1;
    factors::liquidity_resilience::FactorEntry invalid("000001", factors::liquidity_resilience::GetMetadata(), config);
    invalid.AddQuote(Quote(20000, 10000, 100, 100));
    const double invalid_value = Value(invalid);
    if (!Near(invalid_value, 0.0) || !std::isfinite(invalid_value)) return 1;
    factors::liquidity_resilience::FactorEntry left("000001", factors::liquidity_resilience::GetMetadata(), config);
    factors::liquidity_resilience::FactorEntry right("000001", factors::liquidity_resilience::GetMetadata(), config);
    for (int index = 0; index < 4; ++index) {
        const auto quote = Quote(10000, 10100, 100 + index, 100 + index);
        left.AddQuote(quote); right.AddQuote(quote);
        if (!Near(Value(left), Value(right))) return 1;
    }
    right.AddQuote(Quote(10000, 11000, 1, 1));
    if (!Near(Value(left), 1.0)) return 1;
    return 0;
}
