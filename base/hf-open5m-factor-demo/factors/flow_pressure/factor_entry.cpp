#include "factors/flow_pressure/factor_entry.h"

#include <algorithm>
#include <cmath>

namespace {

struct Bounds {
    size_t begin;
    size_t end;
};

Bounds WindowBounds(size_t history_size, size_t window, size_t lag) {
    if (history_size <= lag) return {0, 0};
    const size_t end = history_size - lag;
    return {end > window ? end - window : 0, end};
}

double SignedRatio(
    const std::deque<double>& history, size_t window, size_t lag) {
    const auto bounds = WindowBounds(history.size(), window, lag);
    double signed_sum = 0.0;
    double absolute_sum = 0.0;
    for (size_t index = bounds.begin; index < bounds.end; ++index) {
        signed_sum += history[index];
        absolute_sum += std::abs(history[index]);
    }
    return absolute_sum > 0.0 ? signed_sum / absolute_sum : 0.0;
}

double ZScoreFlow(
    const std::deque<double>& history, size_t window, size_t lag) {
    const auto bounds = WindowBounds(history.size(), window, lag);
    const size_t count = bounds.end - bounds.begin;
    if (count == 0) return 0.0;
    double sum = 0.0;
    double squared_sum = 0.0;
    for (size_t index = bounds.begin; index < bounds.end; ++index) {
        sum += history[index];
        squared_sum += history[index] * history[index];
    }
    const double mean = sum / static_cast<double>(count);
    const double variance = std::max(
        0.0, squared_sum / static_cast<double>(count) - mean * mean);
    const double denominator = std::sqrt(variance) * std::sqrt(static_cast<double>(window));
    return denominator > 1e-12 ? sum / denominator : 0.0;
}

double DecayedFlow(
    const std::deque<double>& history, size_t window, size_t lag,
    double half_life) {
    const auto bounds = WindowBounds(history.size(), window, lag);
    double signed_sum = 0.0;
    double absolute_sum = 0.0;
    const double decay = std::log(2.0) / half_life;
    for (size_t index = bounds.begin; index < bounds.end; ++index) {
        const size_t age = bounds.end - index - 1;
        const double weight = std::exp(-static_cast<double>(age) * decay);
        signed_sum += weight * history[index];
        absolute_sum += weight * std::abs(history[index]);
    }
    return absolute_sum > 0.0 ? signed_sum / absolute_sum : 0.0;
}

double FiniteOrZero(double value) {
    return std::isfinite(value) ? value : 0.0;
}

}  // namespace

namespace factors {
namespace flow_pressure {

FactorEntry::FactorEntry(
    const std::string& asset, const comm::FactorMetadata& metadata,
    const comm::FactorEntryConfig& config)
    : comm::FactorEntryBase(asset, metadata, config) {}

void FactorEntry::DoOnAddQuote(const Stock_Internal_Book& quote) { (void)quote; }

void FactorEntry::DoOnAddTrans(
    const Stock_Transaction_Internal_Book_New& trade) {
    if (trade.trade_type == 'C' || trade.trade_volume <= 0) return;
    double signed_volume = 0.0;
    if (trade.bsflag == 'B') {
        signed_volume = static_cast<double>(trade.trade_volume);
    } else if (trade.bsflag == 'S') {
        signed_volume = -static_cast<double>(trade.trade_volume);
    } else {
        return;
    }
    signed_volumes_.push_back(signed_volume);
    if (signed_volumes_.size() > kMaxHistoryEvents) {
        signed_volumes_.pop_front();
    }
}

void FactorEntry::DoOnAddOrder(const Stock_Order_Internal_Book_New& order) {
    (void)order;
}

void FactorEntry::DoOnUpdateFactors(int64_t timestamp) {
    (void)timestamp;
    fvals_[0] = FiniteOrZero(SignedRatio(signed_volumes_, 16, 0));
    fvals_[1] = FiniteOrZero(SignedRatio(signed_volumes_, 32, 0));
    fvals_[2] = FiniteOrZero(SignedRatio(signed_volumes_, 64, 1));
    fvals_[3] = FiniteOrZero(SignedRatio(signed_volumes_, 128, 2));
    fvals_[4] = FiniteOrZero(ZScoreFlow(signed_volumes_, 16, 0));
    fvals_[5] = FiniteOrZero(ZScoreFlow(signed_volumes_, 32, 0));
    fvals_[6] = FiniteOrZero(ZScoreFlow(signed_volumes_, 64, 1));
    fvals_[7] = FiniteOrZero(ZScoreFlow(signed_volumes_, 128, 2));
    fvals_[8] = FiniteOrZero(DecayedFlow(signed_volumes_, 16, 0, 4.0));
    fvals_[9] = FiniteOrZero(DecayedFlow(signed_volumes_, 32, 0, 8.0));
    fvals_[10] = FiniteOrZero(DecayedFlow(signed_volumes_, 64, 1, 16.0));
    fvals_[11] = FiniteOrZero(DecayedFlow(signed_volumes_, 128, 2, 32.0));
}

}  // namespace flow_pressure
}  // namespace factors
