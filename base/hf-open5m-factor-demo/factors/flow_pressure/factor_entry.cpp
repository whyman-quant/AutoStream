#include "factors/flow_pressure/factor_entry.h"

#include <cmath>

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
    signed_sum_ += signed_volume;
    absolute_sum_ += std::abs(signed_volume);
    if (signed_volumes_.size() > kWindowEvents) {
        const double oldest = signed_volumes_.front();
        signed_volumes_.pop_front();
        signed_sum_ -= oldest;
        absolute_sum_ -= std::abs(oldest);
    }
}

void FactorEntry::DoOnAddOrder(const Stock_Order_Internal_Book_New& order) {
    (void)order;
}

void FactorEntry::DoOnUpdateFactors(int64_t timestamp) {
    (void)timestamp;
    const double value = absolute_sum_ > 0.0 ? signed_sum_ / absolute_sum_ : 0.0;
    fvals_[0] = std::isfinite(value) ? value : 0.0;
}

}  // namespace flow_pressure
}  // namespace factors
