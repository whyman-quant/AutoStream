#include "factors/liquidity_resilience/factor_entry.h"

#include <algorithm>
#include <cmath>

namespace factors {
namespace liquidity_resilience {
namespace {
double QuoteLiquidity(const Stock_Internal_Book& quote) {
    const double bid = static_cast<double>(quote.bp_array[0]);
    const double ask = static_cast<double>(quote.ap_array[0]);
    const double bid_volume = static_cast<double>(quote.bv_array[0]);
    const double ask_volume = static_cast<double>(quote.av_array[0]);
    if (!(bid > 0.0 && ask > bid && bid_volume >= 0.0 && ask_volume >= 0.0)) return 0.0;
    const double spread = ask - bid;
    const double mid = 0.5 * (bid + ask);
    const double reference = std::max(1.0, mid * 0.001);
    const double denominator = 1.0 + spread / reference;
    const double value = (bid_volume + ask_volume) / denominator;
    return std::isfinite(value) && value > 0.0 ? value : 0.0;
}
}  // namespace

FactorEntry::FactorEntry(const std::string& asset, const comm::FactorMetadata& metadata, const comm::FactorEntryConfig& config)
    : comm::FactorEntryBase(asset, metadata, config) {}

void FactorEntry::DoOnAddQuote(const Stock_Internal_Book& quote) {
    current_liquidity_ = QuoteLiquidity(quote);
    current_valid_ = current_liquidity_ > 0.0;
    if (!current_valid_) return;
    liquidity_history_.push_back(current_liquidity_);
    if (liquidity_history_.size() > kWindowEvents) liquidity_history_.pop_front();
}

void FactorEntry::DoOnAddTrans(const Stock_Transaction_Internal_Book_New& trade) { (void)trade; }
void FactorEntry::DoOnAddOrder(const Stock_Order_Internal_Book_New& order) { (void)order; }

void FactorEntry::DoOnUpdateFactors(int64_t timestamp) {
    (void)timestamp;
    if (!current_valid_ || liquidity_history_.empty()) { fvals_[0] = 0.0; return; }
    const double peak = *std::max_element(liquidity_history_.begin(), liquidity_history_.end());
    const double value = peak > 0.0 ? current_liquidity_ / peak : 0.0;
    fvals_[0] = std::isfinite(value) ? std::max(0.0, std::min(1.0, value)) : 0.0;
}

}  // namespace liquidity_resilience
}  // namespace factors
