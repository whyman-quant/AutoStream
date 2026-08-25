#include "factors/book_imbalance/factor_entry.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace factors {
namespace book_imbalance {
namespace {

enum FactorIndex : size_t {
    kWeightedW16V0Lag0 = 0, kMicropriceW32V0Lag1, kSpreadW64V0Lag2,
    kWeightedW128V1Lag0, kMicropriceW16V1Lag1, kSpreadW32V1Lag2,
    kWeightedW64V2Lag0, kMicropriceW128V2Lag1, kSpreadW16V2Lag2,
    kWeightedW32V3Lag0, kMicropriceW64V3Lag1, kSpreadW128V3Lag2,
};

double FiniteOrZero(double value) { return std::isfinite(value) ? value : 0.0; }

int DepthLevelsForVariant(int variant) { return std::min(10, 2 + variant * 2); }

double WeightedDepth(const Stock_Internal_Book& quote, bool bid_side, int levels, int variant) {
    double total = 0.0;
    for (int level = 0; level < levels; ++level) {
        const double size = static_cast<double>(bid_side ? quote.bv_array[level] : quote.av_array[level]);
        const double price = static_cast<double>(bid_side ? quote.bp_array[level] : quote.ap_array[level]);
        if (size <= 0.0 || price <= 0.0) continue;
        const double level_weight = 1.0 / static_cast<double>(level + 1);
        const double price_weight = variant % 2 == 0 ? 1.0 : price / 10000.0;
        total += size * level_weight * price_weight;
    }
    return total;
}

double DepthImbalance(const Stock_Internal_Book& quote, int levels, int variant) {
    const double bid = WeightedDepth(quote, true, levels, variant);
    const double ask = WeightedDepth(quote, false, levels, variant);
    return bid + ask <= 0.0 ? 0.0 : (bid - ask) / (bid + ask);
}

double Normalizer(int window_events, int64_t quote_count) {
    const double event_scale = std::min(1.0, static_cast<double>(std::max<int64_t>(quote_count, 1)) /
                                               static_cast<double>(std::max(window_events, 1)));
    return std::max(1e-6, event_scale / static_cast<double>(window_events));
}

double Prepare(double raw, int variant, int lag) {
    if (!std::isfinite(raw)) return 0.0;
    const double clipped = std::max(-8.0, std::min(8.0, raw));
    return clipped * (1.0 + 0.25 * variant) / (1.0 + lag);
}

double Combine(double signal, double normalizer, int operation) {
    switch (operation) {
    case 0: return signal + normalizer;
    case 1: return signal - normalizer;
    case 2: return signal * normalizer;
    case 3: return std::abs(normalizer) <= 1e-12 ? 0.0 : signal / normalizer;
    default: return 0.0;
    }
}

double Weighted(const Stock_Internal_Book& quote, int window, int variant, int lag, int operation, int64_t count) {
    const double raw = DepthImbalance(quote, DepthLevelsForVariant(variant), variant);
    return FiniteOrZero(Combine(Prepare(raw, variant, lag), Normalizer(window, count), operation));
}

double Microprice(const Stock_Internal_Book& quote, int window, int variant, int lag, int operation, int64_t count) {
    const double bid = static_cast<double>(quote.bp_array[0]);
    const double ask = static_cast<double>(quote.ap_array[0]);
    const double bid_size = static_cast<double>(quote.bv_array[0]);
    const double ask_size = static_cast<double>(quote.av_array[0]);
    const double total = bid_size + ask_size;
    double raw = 0.0;
    if (std::isfinite(bid) && std::isfinite(ask) && bid_size >= 0.0 && ask_size >= 0.0 && ask > bid && total > 0.0) {
        const double micro = (ask * bid_size + bid * ask_size) / total;
        raw = (micro - 0.5 * (bid + ask)) / (ask - bid);
    }
    return FiniteOrZero(Combine(Prepare(raw, variant, lag), Normalizer(window, count), operation));
}

double Spread(const Stock_Internal_Book& quote, int window, int variant, int lag, int operation, int64_t count) {
    const double bid = static_cast<double>(quote.bp_array[0]);
    const double ask = static_cast<double>(quote.ap_array[0]);
    const double spread = ask - bid;
    const double mid = 0.5 * (bid + ask);
    const double reference = std::max(1.0, mid * 0.001);
    const double depth = DepthImbalance(quote, DepthLevelsForVariant(variant), variant);
    double raw = 0.0;
    if (std::isfinite(depth) && spread >= 0.0 && reference > 1e-12) {
        const double penalty = 1.0 + std::max(0.0, spread / reference - 1.0);
        raw = depth / penalty;
    }
    return FiniteOrZero(Combine(Prepare(raw, variant, lag), Normalizer(window, count), operation));
}

}  // namespace

FactorEntry::FactorEntry(const std::string& asset, const comm::FactorMetadata& metadata, const comm::FactorEntryConfig& config)
    : comm::FactorEntryBase(asset, metadata, config) {}

void FactorEntry::DoOnAddQuote(const Stock_Internal_Book& quote) {
    last_quote_ = quote;
    ++quote_count_;
    has_quote_ = true;
}

void FactorEntry::DoOnAddTrans(const Stock_Transaction_Internal_Book_New& quote) { (void)quote; }
void FactorEntry::DoOnAddOrder(const Stock_Order_Internal_Book_New& quote) { (void)quote; }

void FactorEntry::DoOnUpdateFactors(int64_t timestamp) {
    (void)timestamp;
    if (!has_quote_) {
        std::fill(fvals_.begin(), fvals_.end(), 0.0);
        return;
    }
    fvals_[kWeightedW16V0Lag0] = Weighted(last_quote_, 16, 0, 0, 1, quote_count_);
    fvals_[kMicropriceW32V0Lag1] = Microprice(last_quote_, 32, 0, 1, 0, quote_count_);
    fvals_[kSpreadW64V0Lag2] = Spread(last_quote_, 64, 0, 2, 2, quote_count_);
    fvals_[kWeightedW128V1Lag0] = Weighted(last_quote_, 128, 1, 0, 3, quote_count_);
    fvals_[kMicropriceW16V1Lag1] = Microprice(last_quote_, 16, 1, 1, 1, quote_count_);
    fvals_[kSpreadW32V1Lag2] = Spread(last_quote_, 32, 1, 2, 0, quote_count_);
    fvals_[kWeightedW64V2Lag0] = Weighted(last_quote_, 64, 2, 0, 2, quote_count_);
    fvals_[kMicropriceW128V2Lag1] = Microprice(last_quote_, 128, 2, 1, 3, quote_count_);
    fvals_[kSpreadW16V2Lag2] = Spread(last_quote_, 16, 2, 2, 1, quote_count_);
    fvals_[kWeightedW32V3Lag0] = Weighted(last_quote_, 32, 3, 0, 0, quote_count_);
    fvals_[kMicropriceW64V3Lag1] = Microprice(last_quote_, 64, 3, 1, 2, quote_count_);
    fvals_[kSpreadW128V3Lag2] = Spread(last_quote_, 128, 3, 2, 3, quote_count_);
}

}  // namespace book_imbalance
}  // namespace factors
