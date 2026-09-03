#include "factors/demofw00/tools/market/order_synthesizer.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace factors {
namespace demofw00 {
namespace tools {
namespace market {

namespace {

int64_t AveragePriceX1e4(int64_t amount_yuan, int64_t volume) {
    if (volume <= 0) {
        return 0;
    }
    const __int128 scaled =
        static_cast<__int128>(amount_yuan) * static_cast<__int128>(10000);
    if (scaled >= 0) {
        return static_cast<int64_t>(
            (scaled + static_cast<__int128>(volume / 2)) / volume);
    }
    return -static_cast<int64_t>(
        ((-scaled) + static_cast<__int128>(volume / 2)) / volume);
}

}  // namespace

double OrderSynthesizer::SafeDiv(double a, double b, double def) {
    return (std::abs(b) < 1e-12) ? def : (a / b);
}

OrderSynthesizer::OrderSynthesizer(int ttl_ms) : ttl_ms_(ttl_ms) {}

// 判断是否为沪市股票；market=48 为深市，market=49 为沪市。
static bool IsShanghai(uint8_t market) {
    return (market == 49);
}

const BuilderOutput& OrderSynthesizer::OnOrder(
    const Stock_Order_Internal_Book_New& quote) {
    output_buffer_.Clear();
    const int64_t canonical_id = CanonicalOrderId(quote);
    if (canonical_id == 0) {
        return output_buffer_;
    }
    auto& state = states_[canonical_id];
    state.market = quote.market;
    state.channel = quote.channel;
    state.raw_event_id = quote.order_index;
    state.raw_order_type = quote.order_type;

    if (quote.market == 48) {
        if (!state.has_order) {
            ++seen_order_count_;
        }
        state.order_id = canonical_id;
        state.side = (quote.bsflag == 'B' || quote.bsflag == 'b') ? 0 : 1;
        state.first_time = state.has_init ? state.first_time : quote.int_time;
        state.last_time = quote.int_time;
        state.last_time_ms = ToMillis(quote.int_time);
        state.has_init = true;
        state.has_order = true;
        state.volume_locked = true;
        state.initial_confirmed = true;
        state.original_qty = std::max<int64_t>(0, quote.order_volume);
        state.rest_qty_exact = state.original_qty;
        state.original_order_volume = static_cast<double>(state.original_qty);
        state.rest_qty = static_cast<double>(state.rest_qty_exact);
        AppendEvent(BuilderEventKind::MotherOrderConfirmed,
                    CloseReason::None, state, 0, 0, quote.order_price,
                    AggressorSide::Unknown);
        return output_buffer_;
    }
    if (quote.market != 49) {
        return output_buffer_;
    }

    if (!state.has_init) {
        state.order_id = canonical_id;
        state.side = (quote.bsflag == 'B' || quote.bsflag == 'b') ? 0 : 1;
        state.order_type = 2;
        state.first_time = quote.int_time;
        state.has_init = true;
    } else {
        state.first_time = std::min(state.first_time, quote.int_time);
    }
    state.last_time = std::max(state.last_time, quote.int_time);
    state.last_time_ms = std::max(state.last_time_ms, ToMillis(quote.int_time));
    state.max_event_time_ms =
        std::max(state.max_event_time_ms, ToMillis(quote.int_time));

    if (quote.order_type == 'A') {
        if (!state.has_order) {
            ++seen_order_count_;
        }
        state.has_order = true;
        state.has_a = true;
        state.a_biz_index = static_cast<int64_t>(quote.biz_index);
        state.a_rest_qty = std::max<int64_t>(0, quote.order_volume);
        if (!state.initial_confirmed &&
            state.scheduled_event_time_ms == 0) {
            pending_deadlines_.push(
                PendingDeadline{state.max_event_time_ms, canonical_id});
            state.scheduled_event_time_ms = state.max_event_time_ms;
        }
    } else if (quote.order_type == 'D') {
        const int64_t cancel_qty = std::max<int64_t>(0, quote.order_volume);
        if (!state.initial_confirmed) {
            state.cancelled_qty += cancel_qty;
            if (state.scheduled_event_time_ms == 0) {
                pending_deadlines_.push(
                    PendingDeadline{state.max_event_time_ms, canonical_id});
                state.scheduled_event_time_ms = state.max_event_time_ms;
            }
        } else {
            const int64_t applied =
                std::min<int64_t>(state.rest_qty_exact, cancel_qty);
            state.cancelled_qty += applied;
            state.rest_qty_exact -= applied;
            state.rest_qty = static_cast<double>(state.rest_qty_exact);
            const bool closed = state.rest_qty_exact == 0;
            AppendEvent(closed ? BuilderEventKind::MotherOrderClosed
                               : BuilderEventKind::RestQtyChanged,
                        closed ? CloseReason::Cancelled : CloseReason::None,
                        state, 0, 0, quote.order_price,
                        AggressorSide::Unknown);
            if (closed) {
                states_.erase(canonical_id);
            }
        }
    }
    return output_buffer_;
}

void OrderSynthesizer::AppendEvent(BuilderEventKind kind,
                                   CloseReason close_reason,
                                   const OrderState& state,
                                   int64_t delta_qty,
                                   int64_t delta_amount_yuan,
                                   int64_t price_x1e4,
                                   AggressorSide aggressor_side) {
    OrderEvent event;
    event.kind = kind;
    event.close_reason = close_reason;
    event.order_id = state.order_id;
    event.raw_event_id = state.raw_event_id;
    event.market = state.market;
    event.channel = state.channel;
    event.side = state.side;
    event.order_type = state.order_type;
    event.raw_order_type = state.raw_order_type;
    event.first_time = state.first_time;
    event.last_time = state.last_time;
    event.original_qty = state.original_qty;
    event.immediate_fill_qty = state.immediate_fill_qty;
    event.passive_fill_qty = state.passive_fill_qty;
    event.filled_qty = state.immediate_fill_qty + state.passive_fill_qty;
    event.cancelled_qty = state.cancelled_qty;
    event.rest_qty = static_cast<double>(state.rest_qty_exact);
    event.filled_amount_yuan = state.filled_amount_yuan;
    event.price_x1e4 = price_x1e4;
    if (event.filled_qty > 0) {
        event.avg_price_x1e4 = AveragePriceX1e4(
            event.filled_amount_yuan, event.filled_qty);
    }
    event.original_order_volume = static_cast<double>(event.original_qty);
    event.filled_volume = static_cast<double>(event.filled_qty);
    event.filled_amount = static_cast<double>(event.filled_amount_yuan);
    event.delta_volume = static_cast<double>(delta_qty);
    event.delta_amount = static_cast<double>(delta_amount_yuan);
    event.price = static_cast<double>(event.price_x1e4);
    event.avg_price = static_cast<double>(event.avg_price_x1e4);
    event.has_order = state.has_order;
    event.estimated = state.estimated;
    event.aggressor_side = aggressor_side;
    output_buffer_.events.push_back(event);
    if (kind == BuilderEventKind::MotherOrderConfirmed) {
        ResolveWaitingPairs(state.order_id, MakeSnapshot(state));
    }
    if (kind == BuilderEventKind::MotherOrderClosed) {
        ++completed_order_count_;
    }
}

OrderSnapshot OrderSynthesizer::MakeSnapshot(const OrderState& state) {
    OrderSnapshot snapshot;
    snapshot.order_id = state.order_id;
    snapshot.original_qty = state.original_qty;
    snapshot.immediate_fill_qty = state.immediate_fill_qty;
    snapshot.passive_fill_qty = state.passive_fill_qty;
    snapshot.cancelled_qty = state.cancelled_qty;
    snapshot.rest_qty = state.rest_qty_exact;
    snapshot.filled_amount_yuan = state.filled_amount_yuan;
    snapshot.first_time = state.first_time;
    snapshot.last_time = state.last_time;
    snapshot.channel = state.channel;
    snapshot.market = state.market;
    snapshot.side = static_cast<int8_t>(state.side);
    return snapshot;
}

OrderSnapshot OrderSynthesizer::MakeSnapshot(const OrderEvent& event) {
    OrderSnapshot snapshot;
    snapshot.order_id = event.order_id;
    snapshot.original_qty = event.original_qty;
    snapshot.immediate_fill_qty = event.immediate_fill_qty;
    snapshot.passive_fill_qty = event.passive_fill_qty;
    snapshot.cancelled_qty = event.cancelled_qty;
    snapshot.rest_qty = static_cast<int64_t>(event.rest_qty);
    snapshot.filled_amount_yuan = event.filled_amount_yuan;
    snapshot.first_time = event.first_time;
    snapshot.last_time = event.last_time;
    snapshot.channel = event.channel;
    snapshot.market = event.market;
    snapshot.side = static_cast<int8_t>(event.side);
    return snapshot;
}

bool OrderSynthesizer::FindSnapshot(int64_t order_id,
                                    OrderSnapshot& snapshot) const {
    auto state_it = states_.find(order_id);
    if (state_it != states_.end() && state_it->second.initial_confirmed) {
        snapshot = MakeSnapshot(state_it->second);
        return true;
    }
    for (auto event_it = output_buffer_.events.rbegin();
         event_it != output_buffer_.events.rend(); ++event_it) {
        if (event_it->order_id == order_id && event_it->original_qty > 0) {
            snapshot = MakeSnapshot(*event_it);
            return true;
        }
    }
    return false;
}

void OrderSynthesizer::ResolveWaitingPairs(
    int64_t order_id,
    const OrderSnapshot& snapshot) {
    auto waiter_it = pair_waiters_.find(order_id);
    if (waiter_it == pair_waiters_.end()) {
        return;
    }
    PairWaiterList waiters = std::move(waiter_it->second);
    pair_waiters_.erase(waiter_it);
    const auto resolve_token = [&](size_t token) {
        if (token >= pending_pairs_.size() ||
            !pending_pairs_[token].active) {
            return;
        }
        PendingTradePair& pending = pending_pairs_[token];
        if (pending.buy_id == order_id) {
            pending.event.buy_order = snapshot;
            pending.has_buy = true;
        }
        if (pending.sell_id == order_id) {
            pending.event.sell_order = snapshot;
            pending.has_sell = true;
        }
        if (pending.has_buy && pending.has_sell) {
            output_buffer_.trade_pairs.push_back(pending.event);
            pending.active = false;
            free_pair_slots_.push_back(token);
        }
    };
    if (waiters.has_first) {
        resolve_token(waiters.first);
    }
    for (size_t token : waiters.additional) {
        resolve_token(token);
    }
}

void OrderSynthesizer::QueueOrEmitTradePair(
    const Stock_Transaction_Internal_Book_New& quote,
    AggressorSide aggressor_side,
    AggressorSource aggressor_source) {
    if (quote.buy_id == 0 || quote.sell_id == 0 || quote.trade_volume <= 0) {
        return;
    }
    PendingTradePair pending;
    pending.buy_id = quote.buy_id;
    pending.sell_id = quote.sell_id;
    pending.event.trade_index = quote.trade_index;
    pending.event.biz_index = quote.biz_index;
    pending.event.trade_volume = quote.trade_volume;
    pending.event.trade_amount_yuan = quote.trade_amount;
    pending.event.price_x1e4 = quote.trade_price;
    pending.event.int_time = quote.int_time;
    pending.event.aggressor_side = aggressor_side;
    pending.event.aggressor_source = aggressor_source;
    if (quote.trade_volume > 0) {
        pending.event.avg_price_x1e4 = AveragePriceX1e4(
            quote.trade_amount, quote.trade_volume);
    }
    pending.has_buy = FindSnapshot(pending.buy_id, pending.event.buy_order);
    pending.has_sell = FindSnapshot(pending.sell_id, pending.event.sell_order);
    if (pending.has_buy && pending.has_sell) {
        output_buffer_.trade_pairs.push_back(pending.event);
        return;
    }

    size_t token = 0;
    pending.active = true;
    if (free_pair_slots_.empty()) {
        token = pending_pairs_.size();
        pending_pairs_.push_back(std::move(pending));
    } else {
        token = free_pair_slots_.back();
        free_pair_slots_.pop_back();
        pending_pairs_[token] = std::move(pending);
    }
    if (!pending.has_buy) {
        pair_waiters_[pending_pairs_[token].buy_id].Push(token);
    }
    if (!pending.has_sell) {
        pair_waiters_[pending_pairs_[token].sell_id].Push(token);
    }
}

void OrderSynthesizer::UpdateShenzhenTradeSide(
    int64_t order_id,
    int side,
    const Stock_Transaction_Internal_Book_New& quote,
    AggressorSide aggressor_side) {
    auto it = states_.find(order_id);
    if (it == states_.end() || quote.trade_volume <= 0) {
        return;
    }
    OrderState& state = it->second;
    const int64_t volume = quote.trade_volume;
    const bool incoming =
        (side == 0 && aggressor_side == AggressorSide::Buy) ||
        (side == 1 && aggressor_side == AggressorSide::Sell);
    if (incoming) {
        state.immediate_fill_qty += volume;
    } else {
        state.passive_fill_qty += volume;
    }
    state.filled_amount_yuan += quote.trade_amount;
    state.rest_qty_exact = std::max<int64_t>(0, state.rest_qty_exact - volume);
    state.last_time = quote.int_time;
    state.last_time_ms = ToMillis(quote.int_time);
    state.rest_qty = static_cast<double>(state.rest_qty_exact);
    state.filled_volume = static_cast<double>(state.immediate_fill_qty +
                                              state.passive_fill_qty);
    state.filled_amount = static_cast<double>(state.filled_amount_yuan);

    const bool closed = state.rest_qty_exact == 0;
    AppendEvent(closed ? BuilderEventKind::MotherOrderClosed
                       : BuilderEventKind::RestQtyChanged,
                closed ? CloseReason::FullyFilledAfterResting
                       : CloseReason::None,
                state, volume, quote.trade_amount, quote.trade_price,
                aggressor_side);
    if (closed) {
        states_.erase(it);
    }
}

void OrderSynthesizer::AddShanghaiTradeSide(
    int64_t order_id,
    int side,
    const Stock_Transaction_Internal_Book_New& quote,
    AggressorSide aggressor_side) {
    if (order_id == 0 || quote.trade_volume <= 0) {
        return;
    }
    auto& state = states_[order_id];
    if (!state.has_init) {
        state.order_id = order_id;
        state.raw_event_id = quote.trade_index;
        state.market = quote.market;
        state.channel = quote.channel;
        state.side = side;
        state.order_type = 2;
        state.first_time = quote.int_time;
        state.has_init = true;
    } else {
        state.first_time = std::min(state.first_time, quote.int_time);
    }
    state.last_time = std::max(state.last_time, quote.int_time);
    state.last_time_ms = std::max(state.last_time_ms, ToMillis(quote.int_time));
    state.max_event_time_ms =
        std::max(state.max_event_time_ms, ToMillis(quote.int_time));

    const bool incoming =
        (side == 0 && aggressor_side == AggressorSide::Buy) ||
        (side == 1 && aggressor_side == AggressorSide::Sell);
    if (state.initial_confirmed) {
        const int64_t applied =
            std::min<int64_t>(state.rest_qty_exact, quote.trade_volume);
        state.passive_fill_qty += applied;
        state.filled_amount_yuan += quote.trade_amount;
        state.rest_qty_exact -= applied;
        state.rest_qty = static_cast<double>(state.rest_qty_exact);
        const bool closed = state.rest_qty_exact == 0;
        AppendEvent(closed ? BuilderEventKind::MotherOrderClosed
                           : BuilderEventKind::RestQtyChanged,
                    closed ? CloseReason::FullyFilledAfterResting
                           : CloseReason::None,
                    state, applied, quote.trade_amount, quote.trade_price,
                    aggressor_side);
        if (closed) {
            states_.erase(order_id);
        }
        return;
    }

    TradeFragment fragment;
    fragment.biz_index = quote.biz_index;
    fragment.volume = quote.trade_volume;
    fragment.amount_yuan = quote.trade_amount;
    fragment.price_x1e4 = quote.trade_price;
    fragment.int_time = quote.int_time;
    fragment.incoming = incoming;
    state.pending_trades.push_back(fragment);
    state.has_incoming_fragment = state.has_incoming_fragment || incoming;
    if (state.scheduled_event_time_ms == 0) {
        pending_deadlines_.push(
            PendingDeadline{state.max_event_time_ms, order_id});
        state.scheduled_event_time_ms = state.max_event_time_ms;
    }
}

void OrderSynthesizer::FinalizeShanghaiPending(int safe_event_time_ms) {
    std::vector<int64_t> ready_ids;
    while (!pending_deadlines_.empty() &&
           pending_deadlines_.top().first <= safe_event_time_ms) {
        const PendingDeadline deadline = pending_deadlines_.top();
        pending_deadlines_.pop();
        const int64_t order_id = deadline.second;
        const auto state_it = states_.find(order_id);
        if (state_it == states_.end()) {
            continue;
        }
        OrderState& state = state_it->second;
        if (state.scheduled_event_time_ms != deadline.first) {
            continue;
        }
        if (state.max_event_time_ms != deadline.first) {
            pending_deadlines_.push(
                PendingDeadline{state.max_event_time_ms, order_id});
            state.scheduled_event_time_ms = state.max_event_time_ms;
            continue;
        }
        state.scheduled_event_time_ms = 0;
        if (!state.initial_confirmed &&
            state.max_event_time_ms == deadline.first &&
            (state.has_a || state.has_incoming_fragment) &&
            !state.finalization_queued) {
            state.finalization_queued = true;
            ready_ids.push_back(order_id);
        }
    }

    for (int64_t order_id : ready_ids) {
        auto it = states_.find(order_id);
        if (it == states_.end()) {
            continue;
        }
        OrderState& state = it->second;
        int64_t immediate_qty = 0;
        int64_t passive_qty = 0;
        int64_t amount_yuan = 0;
        int64_t last_price_x1e4 = 0;
        for (const TradeFragment& fragment : state.pending_trades) {
            amount_yuan += fragment.amount_yuan;
            last_price_x1e4 = fragment.price_x1e4;
            if (state.has_a) {
                if (fragment.biz_index < state.a_biz_index) {
                    immediate_qty += fragment.volume;
                } else if (fragment.biz_index > state.a_biz_index) {
                    passive_qty += fragment.volume;
                }
            } else if (fragment.incoming) {
                immediate_qty += fragment.volume;
            }
        }

        state.immediate_fill_qty = immediate_qty;
        state.passive_fill_qty = passive_qty;
        state.filled_amount_yuan = amount_yuan;
        state.initial_confirmed = true;
        state.volume_locked = true;

        if (state.has_a) {
            state.original_qty = state.a_rest_qty + immediate_qty;
            const int64_t consumed = passive_qty + state.cancelled_qty;
            state.rest_qty_exact =
                std::max<int64_t>(0, state.a_rest_qty - consumed);
            state.original_order_volume =
                static_cast<double>(state.original_qty);
            state.filled_volume =
                static_cast<double>(immediate_qty + passive_qty);
            state.filled_amount = static_cast<double>(amount_yuan);
            state.rest_qty = static_cast<double>(state.rest_qty_exact);
            AppendEvent(BuilderEventKind::MotherOrderConfirmed,
                        CloseReason::None, state, 0, 0,
                        last_price_x1e4, AggressorSide::Unknown);
            if (state.rest_qty_exact == 0) {
                AppendEvent(BuilderEventKind::MotherOrderClosed,
                            state.cancelled_qty > 0
                                ? CloseReason::Cancelled
                                : CloseReason::FullyFilledAfterResting,
                            state, 0, 0, last_price_x1e4,
                            AggressorSide::Unknown);
                states_.erase(it);
            } else {
                state.pending_trades.clear();
                state.pending_trades.shrink_to_fit();
            }
        } else {
            state.original_qty = immediate_qty;
            state.rest_qty_exact = 0;
            state.original_order_volume =
                static_cast<double>(state.original_qty);
            state.filled_volume = static_cast<double>(immediate_qty);
            state.filled_amount = static_cast<double>(amount_yuan);
            state.rest_qty = 0.0;
            AppendEvent(BuilderEventKind::MotherOrderConfirmed,
                        CloseReason::None, state, 0, 0,
                        last_price_x1e4, AggressorSide::Unknown);
            AppendEvent(BuilderEventKind::MotherOrderClosed,
                        CloseReason::FullyFilledOnArrival, state, 0, 0,
                        last_price_x1e4, AggressorSide::Unknown);
            states_.erase(it);
        }
    }
}

const BuilderOutput& OrderSynthesizer::AdvanceWatermark(int int_time) {
    output_buffer_.Clear();
    const int safe_event_time_ms = ToMillis(int_time) - ttl_ms_;
    FinalizeShanghaiPending(safe_event_time_ms);
    synth_events_ += static_cast<int64_t>(output_buffer_.events.size());
    return output_buffer_;
}

const BuilderOutput& OrderSynthesizer::FlushAtClose(int int_time) {
    output_buffer_.Clear();
    FinalizeShanghaiPending(std::numeric_limits<int>::max());

    std::vector<int64_t> active_ids;
    active_ids.reserve(states_.size());
    for (const auto& item : states_) {
        if (item.second.initial_confirmed) {
            active_ids.push_back(item.first);
        }
    }
    for (int64_t order_id : active_ids) {
        auto it = states_.find(order_id);
        if (it == states_.end()) {
            continue;
        }
        OrderState& state = it->second;
        if (state.rest_qty_exact > 0) {
            state.cancelled_qty += state.rest_qty_exact;
            state.rest_qty_exact = 0;
            state.rest_qty = 0.0;
        }
        state.last_time = std::max(state.last_time, int_time);
        state.last_time_ms = std::max(state.last_time_ms, ToMillis(int_time));
        AppendEvent(BuilderEventKind::MotherOrderClosed,
                    CloseReason::SessionClose, state, 0, 0, 0,
                    AggressorSide::Unknown);
        states_.erase(it);
    }

    // Incomplete orphan fragments cannot cross the session boundary. They do
    // not emit a false mother order; replay audit reports unresolved pairs.
    states_.clear();
    pending_deadlines_ = decltype(pending_deadlines_){};
    pending_pairs_.clear();
    free_pair_slots_.clear();
    pair_waiters_.clear();
    synth_events_ += static_cast<int64_t>(output_buffer_.events.size());
    return output_buffer_;
}

int OrderSynthesizer::ToMillis(int int_time) {
    if (int_time <= 0) {
        return 0;
    }
    const int hh = (int_time / 10000000) % 100;
    const int mm = (int_time / 100000) % 100;
    const int ss = (int_time / 1000) % 100;
    const int ms = int_time % 1000;
    return (((hh * 60) + mm) * 60 + ss) * 1000 + ms;
}

void OrderSynthesizer::UpdateOne(int64_t order_id,
                                 int side,
                                 const Stock_Transaction_Internal_Book_New& quote,
                                 AggressorSide aggressor_side,
                                 std::vector<OrderEvent>& events) {
    if (order_id == 0) {
        return;
    }
    auto& state = states_[order_id];
    if (!state.has_init) {
        state.order_id = order_id;
        state.side = side;
        state.order_type = 2;
        state.first_time = quote.int_time;
        state.has_init = true;
    }

    state.last_time = quote.int_time;
    state.filled_volume += static_cast<double>(quote.trade_volume);
    state.filled_amount += static_cast<double>(quote.trade_amount);
    state.last_time_ms = ToMillis(quote.int_time);

    // 计算增量
    double delta_vol = state.filled_volume - state.last_emitted_volume;
    double delta_amt = state.filled_amount - state.last_emitted_amount;
    bool is_first = (state.last_emitted_volume == 0.0 && state.last_emitted_amount == 0.0);

    OrderEvent ev;
    ev.order_id = order_id;
    ev.side = side;
    ev.order_type = state.order_type;
    ev.first_time = state.first_time;
    ev.last_time = state.last_time;
    ev.original_order_volume = state.original_order_volume;
    ev.rest_qty = state.rest_qty;
    ev.filled_volume = state.filled_volume;
    ev.filled_amount = state.filled_amount;
    ev.delta_volume = delta_vol;
    ev.delta_amount = delta_amt;
    ev.price = static_cast<double>(quote.trade_price);
    ev.avg_price = SafeDiv(state.filled_amount, state.filled_volume, 0.0);
    ev.has_order = state.has_order;
    ev.is_first_fill = is_first;
    ev.estimated = state.estimated;
    ev.aggressor_side = aggressor_side;

    state.last_emitted_volume = state.filled_volume;
    state.last_emitted_amount = state.filled_amount;

    events.push_back(ev);
}

int64_t OrderSynthesizer::CanonicalOrderId(
    const Stock_Order_Internal_Book_New& quote) {
    if (quote.market == 49) {
        return static_cast<int64_t>(quote.orderorino);
    }
    if (quote.market == 48) {
        return quote.order_index;
    }
    return 0;
}

AggressorSide OrderSynthesizer::ResolveAggressorSide(
    const Stock_Transaction_Internal_Book_New& quote,
    AggressorSource& source) {
    if (quote.bsflag == 'B' || quote.bsflag == 'b') {
        source = AggressorSource::BsFlag;
        return AggressorSide::Buy;
    }
    if (quote.bsflag == 'S' || quote.bsflag == 's') {
        source = AggressorSource::BsFlag;
        return AggressorSide::Sell;
    }
    if (quote.buy_id > 0 && quote.sell_id > 0 &&
        quote.buy_id != quote.sell_id) {
        source = AggressorSource::OrderIdFallback;
        return quote.buy_id > quote.sell_id ? AggressorSide::Buy
                                            : AggressorSide::Sell;
    }
    source = AggressorSource::Unknown;
    return AggressorSide::Unknown;
}

const BuilderOutput& OrderSynthesizer::OnTrans(
    const Stock_Transaction_Internal_Book_New& quote) {
    output_buffer_.Clear();

    if (quote.trade_type == 'C') {
        if (quote.market == 48 && quote.trade_volume > 0) {
            const int64_t target_id =
                quote.buy_id != 0 ? quote.buy_id : quote.sell_id;
            auto it = states_.find(target_id);
            if (it != states_.end()) {
                OrderState& state = it->second;
                const int64_t cancel_qty =
                    std::min<int64_t>(state.rest_qty_exact,
                                      quote.trade_volume);
                state.cancelled_qty += cancel_qty;
                state.rest_qty_exact -= cancel_qty;
                state.rest_qty = static_cast<double>(state.rest_qty_exact);
                state.last_time = quote.int_time;
                state.last_time_ms = ToMillis(quote.int_time);
                const bool closed = state.rest_qty_exact == 0;
                AppendEvent(closed ? BuilderEventKind::MotherOrderClosed
                                   : BuilderEventKind::RestQtyChanged,
                            closed ? CloseReason::Cancelled
                                   : CloseReason::None,
                            state, 0, 0, 0, AggressorSide::Unknown);
                if (closed) {
                    states_.erase(it);
                }
            }
        }
        return output_buffer_;
    }

    // 推断主动方
    AggressorSource aggressor_source = AggressorSource::Unknown;
    AggressorSide aggressor = ResolveAggressorSide(quote, aggressor_source);
    output_buffer_.aggressor_side = aggressor;
    output_buffer_.aggressor_source = aggressor_source;
    output_buffer_.price_x1e4 = quote.trade_price;
    if (quote.trade_volume > 0) {
        output_buffer_.avg_price_x1e4 = AveragePriceX1e4(
            quote.trade_amount, quote.trade_volume);
    }

    if (quote.market == 48) {
        if (quote.buy_id != 0) {
            UpdateShenzhenTradeSide(quote.buy_id, 0, quote, aggressor);
        }
        if (quote.sell_id != 0) {
            UpdateShenzhenTradeSide(quote.sell_id, 1, quote, aggressor);
        }
        QueueOrEmitTradePair(quote, aggressor, aggressor_source);
        synth_events_ += static_cast<int64_t>(output_buffer_.events.size());
        return output_buffer_;
    }

    if (quote.market == 49) {
        AddShanghaiTradeSide(quote.buy_id, 0, quote, aggressor);
        AddShanghaiTradeSide(quote.sell_id, 1, quote, aggressor);
        QueueOrEmitTradePair(quote, aggressor, aggressor_source);
    }
    return output_buffer_;
}

bool OrderSynthesizer::HasOrder(uint8_t market,
                                uint16_t channel,
                                int64_t order_id) const {
    (void)market;
    (void)channel;
    return states_.find(order_id) != states_.end();
}

void OrderSynthesizer::Cleanup(int int_time) {
    // Kept as a source-compatible alias while FactorEntry migrates to
    // AdvanceWatermark(). The watermark finalizes only pending Shanghai mother
    // orders; active rest quantities are never expired by elapsed time.
    AdvanceWatermark(int_time);
}

void OrderSynthesizer::SnapshotStats(int64_t& seen_orders,
                                     int64_t& synth_events,
                                     int64_t& filtered_events) const {
    seen_orders = seen_order_count_;
    synth_events = synth_events_;
    filtered_events = filtered_events_;
}

void OrderSynthesizer::AddFilteredEvent() { filtered_events_ += 1; }

}  // namespace market
}  // namespace tools
}  // namespace demofw00
}  // namespace factors
