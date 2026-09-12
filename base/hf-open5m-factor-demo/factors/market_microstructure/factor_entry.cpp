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
    std::vector<bool> ready(kFactorSize, false);
    if (!has_computed_) return ready;
    for (size_t i = 0; i < kFactorSize; ++i) ready[i] = std::isfinite(fvals_[i]);
    if (timestamp == 92600000 || timestamp == 92700000) {
        std::fill(ready.begin() + 2, ready.end(), false);
    }
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
    for (size_t i = 2; i < 8; ++i) {
        reasons[i] = std::isfinite(fvals_[i])
            ? static_cast<unsigned char>(ReadinessReason::Ready)
            : static_cast<unsigned char>(ReadinessReason::InsufficientHistory);
    }
    for (size_t i = 8; i < reasons.size(); ++i) {
        reasons[i] = std::isfinite(fvals_[i])
            ? static_cast<unsigned char>(ReadinessReason::Ready)
            : static_cast<unsigned char>(ReadinessReason::InsufficientHistory);
    }
    if (timestamp == 92600000 || timestamp == 92700000) {
        for (size_t i = 2; i < reasons.size(); ++i) {
            reasons[i] = static_cast<unsigned char>(ReadinessReason::UnsupportedEvent);
        }
    }
    return reasons;
}

void FactorEntry::DoOnAddQuote(const Stock_Internal_Book& quote) {
    const double previous_bid_depth = last_bid_depth_;
    const double previous_ask_depth = last_ask_depth_;
    if (pending_shock_ && pending_shock_depth_ > 0.0) {
        const double current = pending_shock_side_ > 0 ?
            previous_ask_depth : previous_bid_depth;
        const double response = std::max(0.0, current - pending_shock_depth_);
        shock_value_ = pending_shock_side_ * response /
                       (pending_shock_volume_ + 1e-9);
        shock_ready_ = true;
        ++matured_shocks_;
        pending_shock_ = false;
    }
    last_quote_ = quote;
    has_quote_ = true;
    last_quote_time_ms_ = static_cast<int>(quote.exch_time);
    last_bid_depth_ = 0.0;
    last_ask_depth_ = 0.0;
    for (int i = 0; i < 10; ++i) {
        if (ValidLevel(quote.bp_array[i], quote.bv_array[i]))
            last_bid_depth_ += quote.bv_array[i];
        if (ValidLevel(quote.ap_array[i], quote.av_array[i]))
            last_ask_depth_ += quote.av_array[i];
    }
}

void FactorEntry::Consume(const demofw00::tools::market::BuilderOutput& output) {
    using namespace demofw00::tools::market;
    for (const auto& event : output.events) {
        if (event.original_qty <= 0 || event.estimated) continue;
        ++lifecycle_events_;
        if (event.side == 0) ++exact_orders_bid_;
        else ++exact_orders_ask_;
        const double survived = std::max(0.0, static_cast<double>(event.rest_qty) +
            static_cast<double>(event.passive_fill_qty));
        const double cancelled = std::max(0.0, static_cast<double>(event.cancelled_qty));
        life_.push_back({event.side, survived, cancelled});
        if (event.side == 0) cancel_bid_ += cancelled;
        else cancel_ask_ += cancelled;
    }
    for (const auto& pair : output.trade_pairs) {
        if (pair.aggressor_source != AggressorSource::BsFlag ||
            pair.buy_order.estimated || pair.sell_order.estimated ||
            pair.buy_order.original_qty <= 0 || pair.sell_order.original_qty <= 0) continue;
        const double mismatch = std::log1p(static_cast<double>(pair.buy_order.original_qty)) -
            std::log1p(static_cast<double>(pair.sell_order.original_qty));
        const int side = pair.aggressor_side == AggressorSide::Buy ? 1 : -1;
        pairs_.push_back({side * mismatch, side, pair.buy_order.order_id,
                          pair.sell_order.order_id, pair.trade_volume});
        pair_sides_.push_back(side);
        ++exact_pairs_;
        parent_ids_.insert(pair.buy_order.order_id);
        parent_ids_.insert(pair.sell_order.order_id);
        if (side == last_pair_side_) ++current_run_;
        else { last_pair_side_ = side; current_run_ = 1; }
        if (pair_sides_.size() > 4096) pair_sides_.pop_front();
    }
    while (pairs_.size() > 4096) pairs_.pop_front();
    while (life_.size() > 4096) life_.pop_front();
}

void FactorEntry::DoOnAddOrder(const Stock_Order_Internal_Book_New& order) {
    const auto& output = synthesizer_.OnOrder(order);
    Consume(output);
}

void FactorEntry::DoOnAddTrans(const Stock_Transaction_Internal_Book_New& trade) {
    Consume(synthesizer_.OnTrans(trade));
    if (trade.trade_type == 'C' || trade.trade_volume <= 0) return;
    if (trade.bsflag == 'B') exec_buy_ += trade.trade_volume;
    if (trade.bsflag == 'S') exec_sell_ += trade.trade_volume;
    if (has_quote_ && last_bid_depth_ > 0.0 && last_ask_depth_ > 0.0 &&
        static_cast<double>(trade.trade_volume) >=
            0.25 * std::min(last_bid_depth_, last_ask_depth_)) {
        pending_shock_ = true;
        pending_shock_side_ = trade.bsflag == 'B' ? 1 : -1;
        pending_shock_volume_ = static_cast<double>(trade.trade_volume);
        pending_shock_depth_ = pending_shock_side_ > 0 ? last_ask_depth_ : last_bid_depth_;
    }
}

void FactorEntry::DoOnGlobalTime(int exch_time) {
    Consume(exch_time >= 150000000 ? synthesizer_.FlushAtClose(exch_time)
                                   : synthesizer_.AdvanceWatermark(exch_time));
}

void FactorEntry::UpdateDerivedFactors() {
    if (exact_pairs_ >= 1) {
        double weighted = 0.0, weight = 0.0, equal = 0.0;
        for (const auto& p : pairs_) {
            weighted += p.mismatch * std::max<int64_t>(1, p.volume);
            weight += std::max<int64_t>(1, p.volume);
            equal += p.mismatch;
        }
        fvals_[2] = weighted / weight;
        fvals_[3] = equal / static_cast<double>(pairs_.size());
    }
    if (exact_orders_bid_ >= 2 && exact_orders_ask_ >= 2 && !life_.empty()) {
        double bid = 0.0, ask = 0.0;
        for (const auto& s : life_) {
            const double commitment = s.commitment;
            if (s.side == 0) bid += commitment;
            else ask += commitment;
        }
        const double total = bid + ask;
        if (total > 0.0) {
            fvals_[4] = (bid - ask) / total;
            fvals_[5] = fvals_[4];
        }
    }
    const double activity = std::abs(cancel_ask_) + std::abs(cancel_bid_) +
                            std::abs(exec_buy_) + std::abs(exec_sell_);
    if (lifecycle_events_ >= 2 && has_quote_ &&
        last_bid_depth_ + last_ask_depth_ > 0.0 && activity > 0.0) {
        const double cancel_delta = cancel_ask_ - cancel_bid_;
        const double exec_delta = exec_buy_ - exec_sell_;
        fvals_[6] = cancel_delta / (std::abs(cancel_ask_) + std::abs(cancel_bid_) + 1e-9) -
                    exec_delta / (std::abs(exec_buy_) + std::abs(exec_sell_) + 1e-9);
        fvals_[7] = (cancel_delta - exec_delta) /
                    (std::abs(cancel_ask_) + std::abs(cancel_bid_) +
                    std::abs(exec_buy_) + std::abs(exec_sell_) + 1e-9);
    }
    if (shock_ready_) {
        fvals_[8] = shock_value_;
        fvals_[9] = shock_value_;
    }
    if (parent_ids_.size() >= 20 && current_run_ >= 3) {
        fvals_[10] = static_cast<double>(last_pair_side_) *
                     std::log1p(static_cast<double>(current_run_));
        fvals_[11] = fvals_[10] /
                     std::sqrt(static_cast<double>(current_run_));
    }
}

void FactorEntry::DoOnUpdateFactors(int64_t timestamp) {
    (void)timestamp;
    std::fill(fvals_.begin(), fvals_.end(), kNaN);
    has_computed_ = true;
    if (!has_quote_) return;
    fvals_[0] = Counterfactual(last_quote_, 0.10);
    fvals_[1] = Counterfactual(last_quote_, 0.25);
    UpdateDerivedFactors();
}

}  // namespace market_microstructure
}  // namespace factors
