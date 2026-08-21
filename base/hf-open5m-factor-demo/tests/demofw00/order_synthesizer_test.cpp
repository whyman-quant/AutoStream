#include <cstdlib>
#include <iostream>

#include "factors/demofw00/tools/market/order_synthesizer.h"

using factors::demofw00::tools::market::BuilderEventKind;
using factors::demofw00::tools::market::AggressorSide;
using factors::demofw00::tools::market::AggressorSource;
using factors::demofw00::tools::market::CloseReason;
using factors::demofw00::tools::market::OrderEvent;
using factors::demofw00::tools::market::OrderSynthesizer;

#define CHECK(expr)                                                           \
    do {                                                                      \
        if (!(expr)) {                                                        \
            std::cerr << __FILE__ << ':' << __LINE__                         \
                      << " CHECK failed: " #expr << '\n';                   \
            std::exit(1);                                                     \
        }                                                                     \
    } while (0)

const OrderEvent& FindEvent(
    const factors::demofw00::tools::market::BuilderOutput& output,
    BuilderEventKind kind,
    int64_t order_id) {
    for (const auto& event : output.events) {
        if (event.kind == kind && event.order_id == order_id) {
            return event;
        }
    }
    std::cerr << "missing event kind=" << static_cast<int>(kind)
              << " order_id=" << order_id << '\n';
    std::exit(1);
}

Stock_Order_Internal_Book_New MakeSzOrder(int64_t order_id,
                                          char side,
                                          int64_t volume) {
    Stock_Order_Internal_Book_New order{};
    order.market = 48;
    order.channel = 2012;
    order.order_type = '0';
    order.bsflag = side;
    order.order_index = order_id;
    order.orderorino = static_cast<uint64_t>(order_id);
    order.order_volume = volume;
    order.int_time = 93000000;
    return order;
}

Stock_Transaction_Internal_Book_New MakeShTrade(int64_t buy_id,
                                                int64_t sell_id,
                                                char aggressor,
                                                int64_t volume,
                                                int64_t biz_index,
                                                int int_time) {
    Stock_Transaction_Internal_Book_New trade{};
    trade.market = 49;
    trade.channel = 1;
    trade.trade_type = '0';
    trade.bsflag = aggressor;
    trade.buy_id = buy_id;
    trade.sell_id = sell_id;
    trade.trade_volume = volume;
    trade.trade_price = 100000;
    trade.trade_amount = volume * 10;
    trade.biz_index = biz_index;
    trade.int_time = int_time;
    return trade;
}

Stock_Order_Internal_Book_New MakeShOrder(int64_t order_id,
                                          int64_t raw_event_id,
                                          char action,
                                          char side,
                                          int64_t volume,
                                          int64_t biz_index,
                                          int int_time) {
    Stock_Order_Internal_Book_New order{};
    order.market = 49;
    order.channel = 1;
    order.order_type = action;
    order.bsflag = side;
    order.order_index = raw_event_id;
    order.orderorino = static_cast<uint64_t>(order_id);
    order.order_volume = volume;
    order.order_price = 100000;
    order.biz_index = biz_index;
    order.int_time = int_time;
    return order;
}

int main() {
    OrderSynthesizer builder(10000);
    Stock_Order_Internal_Book_New order{};
    order.market = 49;
    order.order_type = 'A';
    order.bsflag = 'B';
    order.order_index = 900001;
    order.orderorino = 10001;
    order.biz_index = 102;
    order.order_volume = 500;
    order.int_time = 93000000;

    const auto& output = builder.OnOrder(order);
    CHECK(output.events.empty());
    CHECK(builder.HasOrder(49, order.channel, 10001));
    CHECK(!builder.HasOrder(49, order.channel, 900001));
    CHECK(static_cast<int>(BuilderEventKind::MotherOrderConfirmed) >= 0);

    CHECK(OrderSynthesizer::CanonicalOrderId(order) == 10001);
    order.market = 48;
    CHECK(OrderSynthesizer::CanonicalOrderId(order) == 900001);

    OrderSynthesizer trade_builder(10000);
    Stock_Transaction_Internal_Book_New trade{};
    trade.market = 49;
    trade.bsflag = 'B';
    trade.buy_id = 20001;
    trade.sell_id = 19001;
    trade.trade_price = 85700;
    trade.trade_volume = 100;
    trade.trade_amount = 857;
    trade.biz_index = 100;
    trade.int_time = 93000000;
    const auto& trade_output = trade_builder.OnTrans(trade);
    CHECK(trade_output.aggressor_side == AggressorSide::Buy);
    CHECK(trade_output.aggressor_source == AggressorSource::BsFlag);
    CHECK(trade_output.price_x1e4 == 85700);
    CHECK(trade_output.avg_price_x1e4 == 85700);

    OrderSynthesizer sz_builder(10000);
    const auto& buy_confirm_output =
        sz_builder.OnOrder(MakeSzOrder(7001, 'B', 1000));
    const OrderEvent& buy_confirmed = FindEvent(
        buy_confirm_output, BuilderEventKind::MotherOrderConfirmed, 7001);
    CHECK(buy_confirmed.original_qty == 1000);
    CHECK(buy_confirmed.rest_qty == 1000);
    sz_builder.OnOrder(MakeSzOrder(8001, 'S', 1000));

    Stock_Transaction_Internal_Book_New sz_trade{};
    sz_trade.market = 48;
    sz_trade.channel = 2012;
    sz_trade.trade_type = '0';
    sz_trade.bsflag = 'B';
    sz_trade.buy_id = 7001;
    sz_trade.sell_id = 8001;
    sz_trade.trade_volume = 300;
    sz_trade.trade_price = 100000;
    sz_trade.trade_amount = 3000;
    sz_trade.int_time = 93001000;
    const auto& sz_trade_output = sz_builder.OnTrans(sz_trade);
    const OrderEvent& after_trade = FindEvent(
        sz_trade_output, BuilderEventKind::RestQtyChanged, 7001);
    CHECK(after_trade.filled_qty == 300);
    CHECK(after_trade.rest_qty == 700);

    Stock_Transaction_Internal_Book_New sz_cancel{};
    sz_cancel.market = 48;
    sz_cancel.channel = 2012;
    sz_cancel.trade_type = 'C';
    sz_cancel.buy_id = 7001;
    sz_cancel.trade_volume = 700;
    sz_cancel.int_time = 93002000;
    const auto& sz_cancel_output = sz_builder.OnTrans(sz_cancel);
    const OrderEvent& sz_closed = FindEvent(
        sz_cancel_output, BuilderEventKind::MotherOrderClosed, 7001);
    CHECK(sz_closed.cancelled_qty == 700);
    CHECK(sz_closed.rest_qty == 0);
    CHECK(sz_closed.close_reason == CloseReason::Cancelled);
    CHECK(sz_closed.original_qty == sz_closed.filled_qty +
                                        sz_closed.cancelled_qty);
    CHECK(!sz_builder.HasOrder(48, 2012, 7001));
    CHECK(sz_cancel_output.aggressor_side == AggressorSide::Unknown);

    // Shanghai: trades before A are the immediate part of one mother order.
    OrderSynthesizer sh_forward(10000);
    CHECK(sh_forward.OnTrans(
        MakeShTrade(10001, 9001, 'B', 300, 100, 93000000)).events.empty());
    CHECK(sh_forward.OnTrans(
        MakeShTrade(10001, 9002, 'B', 200, 101, 93001000)).events.empty());
    CHECK(sh_forward.OnOrder(
        MakeShOrder(10001, 900001, 'A', 'B', 500, 102, 93002000))
              .events.empty());
    const auto& sh_forward_watermark = sh_forward.AdvanceWatermark(93013000);
    const OrderEvent& sh_forward_confirmed = FindEvent(
        sh_forward_watermark, BuilderEventKind::MotherOrderConfirmed, 10001);
    CHECK(sh_forward_confirmed.original_qty == 1000);
    CHECK(sh_forward_confirmed.immediate_fill_qty == 500);
    CHECK(sh_forward_confirmed.passive_fill_qty == 0);
    CHECK(sh_forward_confirmed.rest_qty == 500);
    CHECK(sh_forward_confirmed.raw_event_id == 900001);

    // The local callbacks can arrive in the opposite order. biz_index, not
    // callback order, determines whether a fill precedes the A event.
    OrderSynthesizer sh_reversed(10000);
    CHECK(sh_reversed.OnOrder(
        MakeShOrder(10001, 900001, 'A', 'B', 500, 102, 93002000))
              .events.empty());
    CHECK(sh_reversed.OnTrans(
        MakeShTrade(10001, 9001, 'B', 300, 100, 93000000)).events.empty());
    CHECK(sh_reversed.OnTrans(
        MakeShTrade(10001, 9002, 'B', 200, 101, 93001000)).events.empty());
    const auto& sh_reversed_watermark =
        sh_reversed.AdvanceWatermark(93013000);
    const OrderEvent& sh_reversed_confirmed = FindEvent(
        sh_reversed_watermark, BuilderEventKind::MotherOrderConfirmed, 10001);
    CHECK(sh_reversed_confirmed.original_qty ==
          sh_forward_confirmed.original_qty);
    CHECK(sh_reversed_confirmed.immediate_fill_qty ==
          sh_forward_confirmed.immediate_fill_qty);
    CHECK(sh_reversed_confirmed.rest_qty == sh_forward_confirmed.rest_qty);

    // No A after the watermark means the incoming mother order was fully
    // executed before it could rest on the book.
    OrderSynthesizer sh_full_fill(10000);
    CHECK(sh_full_fill.OnTrans(
        MakeShTrade(20001, 9101, 'B', 300, 200, 93100000)).events.empty());
    CHECK(sh_full_fill.OnTrans(
        MakeShTrade(20001, 9102, 'B', 700, 201, 93101000)).events.empty());
    const auto& sh_full_fill_watermark =
        sh_full_fill.AdvanceWatermark(93112000);
    const OrderEvent& sh_full_fill_closed = FindEvent(
        sh_full_fill_watermark, BuilderEventKind::MotherOrderClosed, 20001);
    CHECK(sh_full_fill_closed.original_qty == 1000);
    CHECK(sh_full_fill_closed.immediate_fill_qty == 1000);
    CHECK(sh_full_fill_closed.rest_qty == 0);
    CHECK(sh_full_fill_closed.close_reason ==
          CloseReason::FullyFilledOnArrival);

    // Once confirmed, a Shanghai A order remains active regardless of how far
    // the pending-order watermark advances. A later passive fill and D event
    // consume only the live rest quantity.
    sh_forward.OnOrder(
        MakeShOrder(30001, 930001, 'A', 'S', 1000, 90, 93013000));
    sh_forward.AdvanceWatermark(93024000);
    const auto& sh_passive_output = sh_forward.OnTrans(
        MakeShTrade(10001, 30001, 'S', 200, 103, 93014000));
    const OrderEvent& sh_after_passive = FindEvent(
        sh_passive_output, BuilderEventKind::RestQtyChanged, 10001);
    CHECK(sh_after_passive.original_qty == 1000);
    CHECK(sh_after_passive.immediate_fill_qty == 500);
    CHECK(sh_after_passive.passive_fill_qty == 200);
    CHECK(sh_after_passive.rest_qty == 300);

    sh_forward.AdvanceWatermark(94000000);
    CHECK(sh_forward.HasOrder(49, 1, 10001));

    const auto& sh_delete_output = sh_forward.OnOrder(
        MakeShOrder(10001, 900002, 'D', 'B', 300, 104, 94001000));
    const OrderEvent& sh_cancelled = FindEvent(
        sh_delete_output, BuilderEventKind::MotherOrderClosed, 10001);
    CHECK(sh_cancelled.original_qty == 1000);
    CHECK(sh_cancelled.immediate_fill_qty == 500);
    CHECK(sh_cancelled.passive_fill_qty == 200);
    CHECK(sh_cancelled.cancelled_qty == 300);
    CHECK(sh_cancelled.rest_qty == 0);
    CHECK(sh_cancelled.close_reason == CloseReason::Cancelled);
    CHECK(!sh_forward.HasOrder(49, 1, 10001));
    CHECK(sh_forward.CompletedOrderCount() == 1);

    // A trade is one research event with two mother orders. The caller must
    // not infer the pair by looping over buy then sell and retaining the last.
    OrderSynthesizer pair_builder(10000);
    pair_builder.OnOrder(MakeSzOrder(41001, 'B', 1000));
    pair_builder.OnOrder(MakeSzOrder(42001, 'S', 3000));
    Stock_Transaction_Internal_Book_New pair_trade{};
    pair_trade.market = 48;
    pair_trade.channel = 2012;
    pair_trade.trade_type = '0';
    pair_trade.bsflag = 'B';
    pair_trade.buy_id = 41001;
    pair_trade.sell_id = 42001;
    pair_trade.trade_volume = 500;
    pair_trade.trade_price = 101000;
    pair_trade.trade_amount = 5050;
    pair_trade.trade_index = 88001;
    pair_trade.biz_index = 88001;
    pair_trade.int_time = 94500000;
    const auto& pair_output = pair_builder.OnTrans(pair_trade);
    CHECK(pair_output.trade_pairs.size() == 1);
    const auto& pair = pair_output.trade_pairs.front();
    CHECK(pair.buy_order.order_id == 41001);
    CHECK(pair.sell_order.order_id == 42001);
    CHECK(pair.buy_order.original_qty == 1000);
    CHECK(pair.sell_order.original_qty == 3000);
    CHECK(pair.aggressor_side == AggressorSide::Buy);
    CHECK(pair.trade_volume == 500);

    // Shanghai pairs are held until the incoming mother's A/no-A outcome is
    // known; the passive mother's size is retained rather than overwritten.
    OrderSynthesizer sh_pair_builder(10000);
    sh_pair_builder.OnOrder(
        MakeShOrder(51001, 950001, 'A', 'S', 3000, 50, 95000000));
    sh_pair_builder.AdvanceWatermark(95011000);
    const auto& sh_pair_trade_output = sh_pair_builder.OnTrans(
        MakeShTrade(52001, 51001, 'B', 500, 100, 95012000));
    CHECK(sh_pair_trade_output.trade_pairs.empty());
    sh_pair_builder.OnOrder(
        MakeShOrder(52001, 950002, 'A', 'B', 500, 101, 95013000));
    const auto& sh_pair_watermark =
        sh_pair_builder.AdvanceWatermark(95024000);
    CHECK(sh_pair_watermark.trade_pairs.size() == 1);
    const auto& sh_pair = sh_pair_watermark.trade_pairs.front();
    CHECK(sh_pair.buy_order.order_id == 52001);
    CHECK(sh_pair.sell_order.order_id == 51001);
    CHECK(sh_pair.buy_order.original_qty == 1000);
    CHECK(sh_pair.sell_order.original_qty == 3000);
    CHECK(sh_pair.aggressor_side == AggressorSide::Buy);

    // A real 2022 Shanghai sample has the A event arrive 11.741 seconds
    // before its biz_index-1 immediate fill. The production default must keep
    // the mother pending beyond 10 seconds and reconstruct the full quantity.
    OrderSynthesizer delayed_predecessor_builder;
    delayed_predecessor_builder.OnOrder(
        MakeShOrder(608613, 571718, 'A', 'B', 600, 800920, 93115420));
    CHECK(delayed_predecessor_builder.AdvanceWatermark(93127000)
              .events.empty());
    delayed_predecessor_builder.OnTrans(
        MakeShTrade(608613, 605368, 'B', 900, 800919, 93115420));
    const auto& delayed_confirm_output =
        delayed_predecessor_builder.AdvanceWatermark(93136000);
    const OrderEvent& delayed_confirm = FindEvent(
        delayed_confirm_output, BuilderEventKind::MotherOrderConfirmed,
        608613);
    CHECK(delayed_confirm.original_qty == 1500);
    CHECK(delayed_confirm.immediate_fill_qty == 900);
    CHECK(delayed_confirm.rest_qty == 600);

    // A second real mother was prematurely finalized with a 15-second
    // exchange-time watermark because the global clock ran ahead of the
    // locally delayed biz_index-1 fills. The 20-second default is the minimum
    // tested safe tier for this sequence.
    OrderSynthesizer global_clock_lead_builder;
    global_clock_lead_builder.OnOrder(
        MakeShOrder(631430, 592135, 'A', 'B', 9400, 835564, 93124430));
    CHECK(global_clock_lead_builder.AdvanceWatermark(93140000)
              .events.empty());
    global_clock_lead_builder.OnTrans(
        MakeShTrade(631430, 622572, 'B', 300, 835561, 93124430));
    global_clock_lead_builder.OnTrans(
        MakeShTrade(631430, 624397, 'B', 100, 835562, 93124430));
    global_clock_lead_builder.OnTrans(
        MakeShTrade(631430, 627393, 'B', 1400, 835563, 93124430));
    const auto& clock_lead_confirm_output =
        global_clock_lead_builder.AdvanceWatermark(93145000);
    const OrderEvent& clock_lead_confirm = FindEvent(
        clock_lead_confirm_output, BuilderEventKind::MotherOrderConfirmed,
        631430);
    CHECK(clock_lead_confirm.original_qty == 11200);
    CHECK(clock_lead_confirm.immediate_fill_qty == 1800);
    CHECK(clock_lead_confirm.rest_qty == 9400);

    // Active rest quantities never expire intraday, but the explicit session
    // close must conserve quantity and remove them before the next day.
    OrderSynthesizer session_close_builder;
    session_close_builder.OnOrder(MakeSzOrder(99001, 'B', 1000));
    const auto& session_close_output =
        session_close_builder.FlushAtClose(150000000);
    const OrderEvent& session_closed = FindEvent(
        session_close_output, BuilderEventKind::MotherOrderClosed, 99001);
    CHECK(session_closed.close_reason == CloseReason::SessionClose);
    CHECK(session_closed.cancelled_qty == 1000);
    CHECK(session_closed.rest_qty == 0);
    CHECK(!session_close_builder.HasOrder(48, 2012, 99001));
    return 0;
}
