#include "factors/liquidity_resilience/factor_entry.h"

#include <algorithm>
#include <cmath>

namespace factors {
namespace liquidity_resilience {
namespace {
double QuoteLiquidity(const Stock_Internal_Book& quote, size_t depth_levels) {
    const double bid = static_cast<double>(quote.bp_array[0]);
    const double ask = static_cast<double>(quote.ap_array[0]);
    if (!(bid > 0.0 && ask > bid)) return 0.0;
    double volume = 0.0;
    for (size_t level = 0; level < depth_levels; ++level) {
        const double bid_volume = static_cast<double>(quote.bv_array[level]);
        const double ask_volume = static_cast<double>(quote.av_array[level]);
        if (bid_volume >= 0.0) volume += bid_volume;
        if (ask_volume >= 0.0) volume += ask_volume;
    }
    const double spread = ask - bid;
    const double mid = 0.5 * (bid + ask);
    const double reference = std::max(1.0, mid * 0.001);
    const double denominator = 1.0 + spread / reference;
    const double value = volume / denominator;
    return std::isfinite(value) && value > 0.0 ? value : 0.0;
}

struct Bounds { size_t begin; size_t end; };
Bounds WindowBounds(size_t size, size_t window, size_t lag) {
    if (size <= lag) return {0, 0};
    const size_t end = size - lag;
    return {end > window ? end - window : 0, end};
}
double Recovery(const std::deque<double>& history, size_t window, size_t lag) {
    const auto bounds = WindowBounds(history.size(), window, lag);
    if (bounds.begin == bounds.end) return 0.0;
    double peak = 0.0;
    for (size_t i = bounds.begin; i < bounds.end; ++i) peak = std::max(peak, history[i]);
    const double value = peak > 0.0 ? history[bounds.end - 1] / peak : 0.0;
    return std::isfinite(value) ? std::max(0.0, std::min(1.0, value)) : 0.0;
}
double RecoverySpeed(const std::deque<double>& history, size_t window, size_t lag) {
    const auto bounds = WindowBounds(history.size(), window, lag);
    if (bounds.end - bounds.begin < 2) return 0.0;
    size_t minimum_index = bounds.begin;
    double minimum = history[minimum_index];
    double peak = minimum;
    for (size_t i = bounds.begin + 1; i < bounds.end; ++i) {
        peak = std::max(peak, history[i]);
        if (history[i] < minimum) { minimum = history[i]; minimum_index = i; }
    }
    if (minimum_index == bounds.end - 1 || peak <= 0.0) return 0.0;
    const double recovery = std::max(0.0, history[bounds.end - 1] - minimum) / peak;
    const double age = static_cast<double>(bounds.end - 1 - minimum_index);
    const double value = recovery / age;
    return std::isfinite(value) ? value : 0.0;
}
}  // namespace

FactorEntry::FactorEntry(const std::string& asset, const comm::FactorMetadata& metadata, const comm::FactorEntryConfig& config)
    : comm::FactorEntryBase(asset, metadata, config) {}

void FactorEntry::DoOnAddQuote(const Stock_Internal_Book& quote) {
    const double l1_liquidity = QuoteLiquidity(quote, 1);
    const double l5_liquidity = QuoteLiquidity(quote, 5);
    current_valid_ = l1_liquidity > 0.0;
    if (!current_valid_) return;
    l1_liquidity_history_.push_back(l1_liquidity);
    l5_liquidity_history_.push_back(l5_liquidity > 0.0 ? l5_liquidity : l1_liquidity);
    if (l1_liquidity_history_.size() > kMaxHistoryEvents) l1_liquidity_history_.pop_front();
    if (l5_liquidity_history_.size() > kMaxHistoryEvents) l5_liquidity_history_.pop_front();
}

void FactorEntry::DoOnAddTrans(const Stock_Transaction_Internal_Book_New& trade) { (void)trade; }
void FactorEntry::DoOnAddOrder(const Stock_Order_Internal_Book_New& order) { (void)order; }

void FactorEntry::DoOnUpdateFactors(int64_t timestamp) {
    (void)timestamp;
    if (!current_valid_) { std::fill(fvals_.begin(), fvals_.end(), 0.0); return; }
    fvals_[0] = Recovery(l1_liquidity_history_, 16, 0);
    fvals_[1] = Recovery(l1_liquidity_history_, 32, 0);
    fvals_[2] = Recovery(l1_liquidity_history_, 64, 1);
    fvals_[3] = Recovery(l1_liquidity_history_, 128, 2);
    fvals_[4] = Recovery(l5_liquidity_history_, 16, 0);
    fvals_[5] = Recovery(l5_liquidity_history_, 32, 0);
    fvals_[6] = Recovery(l5_liquidity_history_, 64, 1);
    fvals_[7] = Recovery(l5_liquidity_history_, 128, 2);
    fvals_[8] = RecoverySpeed(l1_liquidity_history_, 16, 0);
    fvals_[9] = RecoverySpeed(l1_liquidity_history_, 32, 0);
    fvals_[10] = RecoverySpeed(l1_liquidity_history_, 64, 1);
    fvals_[11] = RecoverySpeed(l1_liquidity_history_, 128, 2);
}

}  // namespace liquidity_resilience
}  // namespace factors
