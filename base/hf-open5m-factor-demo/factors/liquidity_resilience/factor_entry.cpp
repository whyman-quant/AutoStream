#include "factors/liquidity_resilience/factor_entry.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace factors {
namespace liquidity_resilience {
namespace {

const double kNaN = std::numeric_limits<double>::quiet_NaN();

double Liquidity(const Stock_Internal_Book& quote, size_t levels) {
    const double bid = static_cast<double>(quote.bp_array[0]);
    const double ask = static_cast<double>(quote.ap_array[0]);
    if (!(bid > 0.0 && ask > bid)) return 0.0;

    double volume = 0.0;
    for (size_t level = 0; level < levels; ++level) {
        volume += static_cast<double>(quote.bv_array[level]);
        volume += static_cast<double>(quote.av_array[level]);
    }
    const double mid = 0.5 * (bid + ask);
    const double spread = ask - bid;
    const double value = volume / (1.0 + spread / std::max(1.0, mid * 0.001));
    return std::isfinite(value) && value > 0.0 ? value : 0.0;
}

struct Bounds {
    size_t begin;
    size_t end;
};

Bounds Window(const std::deque<double>& history, size_t window, size_t lag) {
    if (history.size() <= lag) return {0, 0};
    const size_t end = history.size() - lag;
    return {end > window ? end - window : 0, end};
}

double Recovery(const std::deque<double>& history, size_t window, size_t lag) {
    const Bounds bounds = Window(history, window, lag);
    if (bounds.end - bounds.begin < window) return kNaN;

    double peak = 0.0;
    for (size_t i = bounds.begin; i < bounds.end; ++i) peak = std::max(peak, history[i]);
    if (!(peak > 0.0)) return kNaN;

    const double value = history[bounds.end - 1] / peak;
    return std::isfinite(value) ? std::max(0.0, std::min(1.0, value)) : kNaN;
}

double RecoverySpeed(const std::deque<double>& history, size_t window, size_t lag) {
    const Bounds bounds = Window(history, window, lag);
    if (bounds.end - bounds.begin < window) return kNaN;

    // Detect a drawdown against a peak seen before the trough. This prevents
    // a rising-only series from being misclassified using a future peak.
    double running_peak = history[bounds.begin];
    double shock_peak = 0.0;
    double minimum = 0.0;
    size_t minimum_index = bounds.end;
    for (size_t i = bounds.begin + 1; i + 1 < bounds.end; ++i) {
        if (running_peak > 0.0 && history[i] < running_peak * kShockThreshold &&
            (minimum_index == bounds.end || history[i] < minimum)) {
            shock_peak = running_peak;
            minimum = history[i];
            minimum_index = i;
        }
        running_peak = std::max(running_peak, history[i]);
    }
    if (minimum_index == bounds.end || !(shock_peak > 0.0)) return kNaN;

    const double rebound = history[bounds.end - 1] - minimum;
    const double denominator = shock_peak - minimum;
    if (!(rebound > 0.0) || !(denominator > 0.0)) return kNaN;
    const double age = static_cast<double>(bounds.end - 1 - minimum_index);
    if (!(age > 0.0)) return kNaN;

    const double value = (rebound / denominator) / age;
    return std::isfinite(value) ? value : kNaN;
}

}  // namespace

FactorEntry::FactorEntry(const std::string& asset, const comm::FactorMetadata& metadata,
                         const comm::FactorEntryConfig& config)
    : comm::FactorEntryBase(asset, metadata, config) {}

std::vector<bool> FactorEntry::GetReadinessMask(int64_t timestamp) const {
    (void)timestamp;
    std::vector<bool> ready(fvals_.size(), false);
    for (size_t i = 0; i < fvals_.size(); ++i) ready[i] = std::isfinite(fvals_[i]);
    return ready;
}

void FactorEntry::DoOnAddQuote(const Stock_Internal_Book& quote) {
    const double l1 = Liquidity(quote, 1);
    if (!(l1 > 0.0)) {
        current_valid_ = false;
        return;
    }
    const double l5 = Liquidity(quote, 5);
    current_valid_ = true;
    l1_.push_back(l1);
    l5_.push_back(l5 > 0.0 ? l5 : l1);
    if (l1_.size() > kMaxHistoryEvents) l1_.pop_front();
    if (l5_.size() > kMaxHistoryEvents) l5_.pop_front();
}

void FactorEntry::DoOnUpdateFactors(int64_t) {
    if (!current_valid_) {
        std::fill(fvals_.begin(), fvals_.end(), kNaN);
        return;
    }
    fvals_[0] = Recovery(l1_, 16, 0);
    fvals_[1] = Recovery(l1_, 32, 0);
    fvals_[2] = Recovery(l1_, 64, 1);
    fvals_[3] = Recovery(l1_, 128, 2);
    fvals_[4] = Recovery(l5_, 16, 0);
    fvals_[5] = Recovery(l5_, 32, 0);
    fvals_[6] = Recovery(l5_, 64, 1);
    fvals_[7] = Recovery(l5_, 128, 2);
    fvals_[8] = RecoverySpeed(l1_, 16, 0);
    fvals_[9] = RecoverySpeed(l1_, 32, 0);
    fvals_[10] = RecoverySpeed(l1_, 64, 1);
    fvals_[11] = RecoverySpeed(l1_, 128, 2);
    // L4G1 candidates are explicitly post-shock variants.  A calm/rising
    // window is unavailable (NaN + readiness=false), never a numeric zero.
    fvals_[12] = RecoverySpeed(l1_, 96, 1);
    fvals_[13] = RecoverySpeed(l5_, 96, 1);
    fvals_[14] = RecoverySpeed(l1_, 96, 1);
    fvals_[15] = RecoverySpeed(l5_, 96, 1);
}

}  // namespace liquidity_resilience
}  // namespace factors
