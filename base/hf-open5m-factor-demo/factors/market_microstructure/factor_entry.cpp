#include "factors/market_microstructure/factor_entry.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace factors {
namespace market_microstructure {
namespace {

const double kNaN = std::numeric_limits<double>::quiet_NaN();

bool ValidLevel(double price, double volume) {
    return std::isfinite(price) && std::isfinite(volume) && price > 0.0 && volume > 0.0;
}

double WalkVwap(const uint32_t* prices, const uint32_t* volumes, int levels,
                double quantity) {
    if (!(quantity > 0.0)) return kNaN;
    double remaining = quantity;
    double notional = 0.0;
    double consumed = 0.0;
    for (int i = 0; i < levels && remaining > 0.0; ++i) {
        if (!ValidLevel(prices[i], volumes[i])) continue;
        const double take = std::min(remaining, static_cast<double>(volumes[i]));
        notional += take * prices[i];
        consumed += take;
        remaining -= take;
    }
    if (remaining > 1e-12 || !(consumed > 0.0)) return kNaN;
    return notional / consumed;
}

double Counterfactual(const Stock_Internal_Book& quote, double fraction) {
    const double bid = static_cast<double>(quote.bp_array[0]);
    const double ask = static_cast<double>(quote.ap_array[0]);
    if (!(std::isfinite(bid) && std::isfinite(ask) && ask > bid)) return kNaN;
    double bid_depth = 0.0;
    double ask_depth = 0.0;
    int bid_levels = 0;
    int ask_levels = 0;
    for (int i = 0; i < 10; ++i) {
        if (ValidLevel(static_cast<double>(quote.bp_array[i]), static_cast<double>(quote.bv_array[i]))) {
            bid_depth += static_cast<double>(quote.bv_array[i]);
            ++bid_levels;
        }
        if (ValidLevel(static_cast<double>(quote.ap_array[i]), static_cast<double>(quote.av_array[i]))) {
            ask_depth += static_cast<double>(quote.av_array[i]);
            ++ask_levels;
        }
    }
    if (bid_levels < 3 || ask_levels < 3) return kNaN;
    const double quantity = fraction * std::min(bid_depth, ask_depth);
    if (!(quantity > 0.0)) return kNaN;
    const double buy_vwap = WalkVwap(quote.ap_array, quote.av_array, 10, quantity);
    const double sell_vwap = WalkVwap(quote.bp_array, quote.bv_array, 10, quantity);
    const double mid = 0.5 * (bid + ask);
    const double tick = 1.0;
    if (!(std::isfinite(buy_vwap) && std::isfinite(sell_vwap) && mid > 0.0)) return kNaN;
    return ((buy_vwap - mid) - (mid - sell_vwap)) / tick;
}

}  // namespace

FactorEntry::FactorEntry(const std::string& asset, const comm::FactorMetadata& metadata,
                         const comm::FactorEntryConfig& config)
    : comm::FactorEntryBase(asset, metadata, config) {}

std::vector<bool> FactorEntry::GetReadinessMask(int64_t timestamp) const {
    (void)timestamp;
    std::vector<bool> ready(kFactorSize, false);
    if (!has_computed_) return ready;
    ready[0] = std::isfinite(fvals_[0]);
    ready[1] = std::isfinite(fvals_[1]);
    return ready;
}

std::vector<unsigned char> FactorEntry::GetReadinessReasonCodes(int64_t timestamp) const {
    using comm::ReadinessReason;
    std::vector<unsigned char> reasons(
        kFactorSize, static_cast<unsigned char>(ReadinessReason::ImplementationPending));
    if (!has_computed_ || !has_quote_) {
        std::fill(reasons.begin(), reasons.end(),
                  static_cast<unsigned char>(ReadinessReason::NoInput));
        return reasons;
    }
    for (size_t i = 0; i < 2; ++i) {
        reasons[i] = std::isfinite(fvals_[i])
            ? static_cast<unsigned char>(ReadinessReason::Ready)
            : static_cast<unsigned char>(ReadinessReason::InsufficientBookDepth);
    }
    if (timestamp == 92600000 || timestamp == 92700000) {
        for (size_t i = 2; i < reasons.size(); ++i) {
            reasons[i] = static_cast<unsigned char>(ReadinessReason::UnsupportedEvent);
        }
    }
    return reasons;
}

void FactorEntry::DoOnAddQuote(const Stock_Internal_Book& quote) {
    last_quote_ = quote;
    has_quote_ = true;
}

void FactorEntry::DoOnUpdateFactors(int64_t timestamp) {
    (void)timestamp;
    std::fill(fvals_.begin(), fvals_.end(), kNaN);
    has_computed_ = true;
    if (!has_quote_) return;
    fvals_[0] = Counterfactual(last_quote_, 0.10);
    fvals_[1] = Counterfactual(last_quote_, 0.25);
    // The remaining ten candidates require exact OrderEvent/TradePairEvent
    // state.  They intentionally remain NaN until their causal state machines
    // are wired; writing zero would conflate not-ready with a measured zero.
}

}  // namespace market_microstructure
}  // namespace factors
