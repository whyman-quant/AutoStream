#include <cmath>
#include <cstdlib>
#include <cstdint>

#include "factors/market_microstructure/factor_entry.h"
#include "sdp_handler/quote_format_define.h"

namespace {
Stock_Order_Internal_Book_New Order(int64_t id, char side, int64_t volume) {
    Stock_Order_Internal_Book_New order{};
    order.market = 48;
    order.channel = 1;
    order.order_type = '0';
    order.bsflag = side;
    order.order_index = id;
    order.order_volume = volume;
    order.order_price = side == 'B' ? 999900 : 1000100;
    order.int_time = 93000000;
    return order;
}

Stock_Transaction_Internal_Book_New Trade(int64_t buy_id, int64_t sell_id,
                                          int64_t volume, int64_t index) {
    Stock_Transaction_Internal_Book_New trade{};
    trade.market = 48;
    trade.channel = 1;
    trade.trade_type = '0';
    trade.bsflag = 'B';
    trade.buy_id = buy_id;
    trade.sell_id = sell_id;
    trade.trade_volume = volume;
    trade.trade_price = 1000000;
    trade.trade_amount = volume * 100;
    trade.trade_index = index;
    trade.biz_index = index;
    trade.int_time = 93000000 + static_cast<int>(index);
    return trade;
}
}  // namespace

int main() {
    using factors::market_microstructure::FactorEntry;
    using factors::market_microstructure::GetMetadata;
    factors::comm::FactorEntryConfig config;
    FactorEntry entry("600000", GetMetadata(), config);
    for (bool value : entry.GetReadinessMask(92600000)) {
        if (value) return 3;
    }
    const auto cold_reasons = entry.GetReadinessReasonCodes(92600000);
    if (cold_reasons.size() != 12 || cold_reasons[0] != 1) return 5;
    FactorEntry shallow("600001", GetMetadata(), config);
    Stock_Internal_Book shallow_quote{};
    shallow_quote.bp_array[0] = 999900;
    shallow_quote.ap_array[0] = 1000100;
    shallow_quote.bv_array[0] = 1000;
    shallow_quote.av_array[0] = 1000;
    shallow.AddQuote(shallow_quote);
    shallow.UpdateFactors(92600000);
    if (shallow.GetReadinessMask(92600000)[0] ||
        !std::isnan(shallow.GetFactorValues()[0])) return 4;
    if (shallow.GetReadinessReasonCodes(92600000)[0] != 2) return 6;
    Stock_Internal_Book quote{};
    quote.bp_array[0] = 999900;
    quote.ap_array[0] = 1000100;
    quote.bv_array[0] = 1000;
    quote.av_array[0] = 1000;
    quote.bp_array[1] = 999800;
    quote.ap_array[1] = 1000200;
    quote.bv_array[1] = 1000;
    quote.av_array[1] = 1000;
    quote.bp_array[2] = 999700;
    quote.ap_array[2] = 1000300;
    quote.bv_array[2] = 1000;
    quote.av_array[2] = 1000;
    entry.AddQuote(quote);
    entry.UpdateFactors(92600000);
    const auto& values = entry.GetFactorValues();
    const auto ready = entry.GetReadinessMask(92600000);
    const auto reasons = entry.GetReadinessReasonCodes(92600000);
    if (values.size() != 12 || !ready[0] || !ready[1] ||
        !std::isfinite(values[0]) || !std::isfinite(values[1])) return 1;
    if (reasons[0] != 0 || reasons[1] != 0) return 7;
    for (size_t i = 2; i < values.size(); ++i) {
        if (ready[i] || !std::isnan(values[i]) || reasons[i] != 4) return 2;
    }

    factors::market_microstructure::FactorEntry lifecycle(
        "600002", GetMetadata(), config);
    lifecycle.AddQuote(quote);
    for (int64_t i = 0; i < 20; ++i) {
        lifecycle.AddOrder(Order(10000 + i, 'B', 1000 + i));
        lifecycle.AddOrder(Order(20000 + i, 'S', 1200 + i));
        lifecycle.AddTrans(Trade(10000 + i, 20000 + i, 1000, i + 1));
    }
    Stock_Internal_Book replenished = quote;
    replenished.bv_array[0] = 1500;
    replenished.av_array[0] = 1500;
    lifecycle.AddQuote(replenished);
    const auto lifecycle_values = lifecycle.UpdateFactors(100000000);
    const auto lifecycle_ready = lifecycle.GetReadinessMask(100000000);
    if (!lifecycle_ready[2] || !lifecycle_ready[3] ||
        !std::isfinite(lifecycle_values[2]) ||
        !std::isfinite(lifecycle_values[3])) return 8;
    if (!lifecycle_ready[4] || !lifecycle_ready[5]) return 9;
    for (size_t i = 6; i < 12; ++i) {
        if (!lifecycle_ready[i] || !std::isfinite(lifecycle_values[i])) return 10;
    }
    return 0;
}
