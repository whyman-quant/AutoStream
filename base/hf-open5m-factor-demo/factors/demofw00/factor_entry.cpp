#include "factors/demofw00/factor_entry.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include "factors/demofw00/tools/market/active_trader_signals.h"
#include "factors/demofw00/tools/market/big_event_detector.h"
#include "factors/demofw00/tools/market/ofi_status_analyzer.h"
#include "factors/demofw00/tools/market/order_id_analyzer_order.h"
#include "factors/demofw00/tools/market/order_id_analyzer_trans.h"
#include "factors/demofw00/tools/market/order_synthesizer.h"

namespace factors {
namespace demofw00 {

namespace {

enum FactorIndex : size_t {
    kTimestamp = 0,
    kLastQuoteTime,
    kLastTradeTime,
    kLastOrderTime,
    kQuoteCount,
    kTradeCount,
    kOrderCount,
    kLastPrice,
    kBestBid,
    kBestAsk,
    kSpread,
    kMidPrice,
    kOfiBasic,
    kOfiAgg,
    kOfiPca,
    kOfiOrder,
    kOfiTrade,
    kOfiZscore,
    kOfiTrendSlope,
    kOfiState,
    kOfiPurityFinal,
    kBigTradeFlag,
    kBigTradeZscore,
    kBigTradeThreshold,
    kBigOrderFlag,
    kBigOrderZscore,
    kBigOrderThreshold,
    kSynthSeenOrders,
    kSynthEvents,
    kSynthFilteredEvents,
    kSynthLastOriginalVolume,
    kSynthLastDeltaVolume,
    kSynthLastExecutionRate,
    kSynthLastEstimated,
    kSynthLastAggressorSide,
    kOrderIdVelocity,
    kOrderIdGapMean,
    kOrderPressureBuy,
    kOrderPressureSell,
    kTransRecordIdVelocity,
    kTransRecordIdGapMean,
    kTransWashProbability,
    kTransPressureBuy,
    kTransPressureSell,
    kActiveCompositeScore,
    kActiveMomentumScore,
    kActiveFlowBiasScore,
    kActiveVolumeSpikeScore,
    kActiveTerrainScore,
    kChipBelowShare,
    kChipAboveShare,
    kChipPeakShare,
    kChipSupportTicks,
    kChipResistanceTicks,
};

static_assert(kChipResistanceTicks + 1 == kFactorSize,
              "demofw00 factor index count must match metadata");

double SafeDiv(double a, double b, double def = 0.0) {
    return (std::abs(b) < 1e-12) ? def : (a / b);
}

int64_t ToMillis(int int_time) {
    if (int_time <= 0) {
        return 0;
    }
    const int hh = (int_time / 10000000) % 100;
    const int mm = (int_time / 100000) % 100;
    const int ss = (int_time / 1000) % 100;
    const int ms = int_time % 1000;
    return static_cast<int64_t>((((hh * 60) + mm) * 60 + ss) * 1000 + ms);
}

double BoolToDouble(bool value) { return value ? 1.0 : 0.0; }

double AggressorToDouble(tools::market::AggressorSide side) {
    return static_cast<double>(static_cast<int8_t>(side));
}

double StateToDouble(tools::market::OfiMarketState state) {
    return static_cast<double>(static_cast<int>(state));
}

tools::market::ActiveTraderSignals::Config MakeActiveTraderConfig() {
    tools::market::ActiveTraderSignals::Config cfg;
    cfg.ring_capacity = 4096;
    cfg.chip_config.events_cap = 4096;
    return cfg;
}

bool IsBuyOrder(const Stock_Order_Internal_Book_New& quote) {
    return quote.bsflag == 'B' || quote.bsflag == 'b';
}

bool IsSellOrder(const Stock_Order_Internal_Book_New& quote) {
    return quote.bsflag == 'S' || quote.bsflag == 's';
}

}  // namespace

struct FactorEntry::Impl {
    tools::market::OfiCalculator ofi_calc;
    tools::market::OfiStatusAnalyzer ofi_status;
    tools::market::OrderIDAnalyzerForOrders order_id_orders;
    tools::market::OrderIDAnalyzerForTrans order_id_trans;
    tools::market::ActiveTraderSignals active_trader;
    tools::base::WindowBasicStats price_vol_stats;
    tools::base::WindowBasicStats big_trade_ref;
    tools::base::WindowBasicStats big_order_ref;
    tools::market::BigEventDetector big_trade_detector;
    tools::market::BigEventDetector big_order_detector;
    tools::market::OrderSynthesizer order_synth;

    tools::market::BigEventDetector::Decision last_big_trade;
    tools::market::BigEventDetector::Decision last_big_order;

    int64_t quote_count{0};
    int64_t trade_count{0};
    int64_t order_count{0};
    int64_t trade_event_idx{0};
    int64_t order_event_idx{0};
    int64_t total_synth_events{0};
    int64_t last_quote_time{0};
    int64_t last_trade_time{0};
    int64_t last_order_time{0};
    double last_price{0.0};
    double best_bid{0.0};
    double best_ask{0.0};
    double mid_price{0.0};
    double last_mid_price{0.0};
    double last_synth_original_volume{0.0};
    double last_synth_delta_volume{0.0};
    double last_synth_execution_rate{0.0};
    double last_synth_estimated{0.0};
    tools::market::AggressorSide last_aggressor{
        tools::market::AggressorSide::Unknown};

    Impl()
        : active_trader(MakeActiveTraderConfig()),
          price_vol_stats(5000, 2000, 2),
          big_trade_ref(200, 2000, 30),
          big_order_ref(200, 2000, 30),
          big_trade_detector(big_trade_ref, 2.0, 30, 1e-12, false, false,
                             0.0, 8.0),
          big_order_detector(big_order_ref, 2.0, 30, 1e-12, false, false,
                             0.0, 8.0),
          order_synth(20000) {}

    void ConsumeBuilderOutput(
        const tools::market::BuilderOutput& output) {
        total_synth_events += static_cast<int64_t>(
            output.events.size() + output.trade_pairs.size());
        for (const auto& event : output.events) {
            if (event.original_qty <= 0) {
                continue;
            }
            last_synth_original_volume =
                static_cast<double>(event.original_qty);
            last_synth_delta_volume = event.delta_volume;
            last_synth_execution_rate = SafeDiv(
                static_cast<double>(event.filled_qty),
                static_cast<double>(event.original_qty), 0.0);
            last_synth_estimated = BoolToDouble(event.estimated);
            if (event.aggressor_side !=
                tools::market::AggressorSide::Unknown) {
                last_aggressor = event.aggressor_side;
            }
        }
        for (const auto& pair : output.trade_pairs) {
            const tools::market::OrderSnapshot* incoming = nullptr;
            if (pair.aggressor_side == tools::market::AggressorSide::Buy) {
                incoming = &pair.buy_order;
            } else if (pair.aggressor_side ==
                       tools::market::AggressorSide::Sell) {
                incoming = &pair.sell_order;
            }
            if (incoming == nullptr || incoming->original_qty <= 0) {
                continue;
            }
            const int64_t filled_qty = incoming->immediate_fill_qty +
                                       incoming->passive_fill_qty;
            last_synth_original_volume =
                static_cast<double>(incoming->original_qty);
            last_synth_delta_volume =
                static_cast<double>(pair.trade_volume);
            last_synth_execution_rate = SafeDiv(
                static_cast<double>(filled_qty),
                static_cast<double>(incoming->original_qty), 0.0);
            last_synth_estimated = 0.0;
            last_aggressor = pair.aggressor_side;
        }
    }
};

FactorEntry::FactorEntry(const std::string& asset,
                         const comm::FactorMetadata& metadata,
                         const comm::FactorEntryConfig& config)
    : comm::FactorEntryBase(asset, metadata, config),
      impl_(factors::make_unique<Impl>()) {}

FactorEntry::~FactorEntry() = default;

void FactorEntry::DoOnAddQuote(const Stock_Internal_Book& quote) {
    ++impl_->quote_count;
    impl_->last_quote_time = quote.exch_time;
    impl_->best_bid = static_cast<double>(quote.bp_array[0]);
    impl_->best_ask = static_cast<double>(quote.ap_array[0]);
    if (impl_->best_bid > 0.0 && impl_->best_ask > 0.0) {
        impl_->mid_price = 0.5 * (impl_->best_bid + impl_->best_ask);
    }
    if (quote.last_px > 0) {
        impl_->last_price = static_cast<double>(quote.last_px);
    } else if (impl_->mid_price > 0.0) {
        impl_->last_price = impl_->mid_price;
    }

    double abs_mid_ret = 0.0;
    if (impl_->last_mid_price > 0.0 && impl_->mid_price > 0.0) {
        abs_mid_ret = std::abs((impl_->mid_price - impl_->last_mid_price) /
                               impl_->last_mid_price);
    }
    impl_->last_mid_price = impl_->mid_price;
    impl_->price_vol_stats.update(static_cast<int>(ToMillis(quote.exch_time)),
                                  abs_mid_ret);

    impl_->ofi_calc.update(quote);
    impl_->ofi_status.update(impl_->ofi_calc.ofi_agg_,
                             impl_->price_vol_stats.std_dev(), quote);
}

void FactorEntry::DoOnAddTrans(
    const Stock_Transaction_Internal_Book_New& quote) {
    impl_->last_trade_time = quote.int_time;
    const auto& builder_output = impl_->order_synth.OnTrans(quote);
    impl_->ConsumeBuilderOutput(builder_output);
    if (quote.trade_type == 'C') {
        return;
    }
    if (quote.trade_volume <= 0) {
        return;
    }

    ++impl_->trade_count;
    ++impl_->trade_event_idx;
    if (quote.trade_price > 0) {
        impl_->last_price = static_cast<double>(quote.trade_price);
    }

    impl_->last_big_trade =
        impl_->big_trade_detector.decide_and_update_count(
            static_cast<int>(impl_->trade_event_idx),
            static_cast<double>(quote.trade_volume));

    impl_->ofi_calc.update(quote);
    impl_->order_id_trans.update(quote);
    impl_->active_trader.update(quote);

}

void FactorEntry::DoOnAddOrder(const Stock_Order_Internal_Book_New& quote) {
    impl_->last_order_time = quote.int_time;
    const auto& builder_output = impl_->order_synth.OnOrder(quote);
    impl_->ConsumeBuilderOutput(builder_output);
    if (quote.market == 49 && quote.order_type == 'D') {
        return;
    }
    if (quote.order_volume <= 0) {
        return;
    }

    ++impl_->order_count;
    ++impl_->order_event_idx;
    impl_->ofi_calc.update(quote);
    impl_->order_id_orders.update(quote);

    bool marketable = false;
    int pricedist_ticks = 0;
    if (IsBuyOrder(quote) && impl_->best_ask > 0.0) {
        marketable = quote.order_price >= impl_->best_ask;
        pricedist_ticks = static_cast<int>(std::max(0.0, impl_->best_ask -
                                                          quote.order_price));
    } else if (IsSellOrder(quote) && impl_->best_bid > 0.0) {
        marketable = quote.order_price <= impl_->best_bid;
        pricedist_ticks = static_cast<int>(std::max(0.0, quote.order_price -
                                                          impl_->best_bid));
    }

    impl_->last_big_order =
        impl_->big_order_detector.decide_order_and_update_count(
            static_cast<int>(impl_->order_event_idx),
            static_cast<double>(quote.order_volume), marketable,
            pricedist_ticks);
}

void FactorEntry::DoOnGlobalTime(int exch_time) {
    const auto& builder_output = exch_time >= 150000000
        ? impl_->order_synth.FlushAtClose(exch_time)
        : impl_->order_synth.AdvanceWatermark(exch_time);
    impl_->ConsumeBuilderOutput(builder_output);
}

void FactorEntry::DoOnUpdateFactors(int64_t timestamp) {
    auto order_metrics = impl_->order_id_orders.get_metrics();
    auto trans_metrics = impl_->order_id_trans.get_metrics();
    auto active_metrics =
        impl_->active_trader.compute(static_cast<int>(impl_->last_price));

    int64_t synth_seen = 0;
    int64_t synth_events = 0;
    int64_t synth_filtered = 0;
    impl_->order_synth.SnapshotStats(synth_seen, synth_events, synth_filtered);

    fvals_[kTimestamp] = static_cast<double>(timestamp);
    fvals_[kLastQuoteTime] = static_cast<double>(impl_->last_quote_time);
    fvals_[kLastTradeTime] = static_cast<double>(impl_->last_trade_time);
    fvals_[kLastOrderTime] = static_cast<double>(impl_->last_order_time);
    fvals_[kQuoteCount] = static_cast<double>(impl_->quote_count);
    fvals_[kTradeCount] = static_cast<double>(impl_->trade_count);
    fvals_[kOrderCount] = static_cast<double>(impl_->order_count);
    fvals_[kLastPrice] = impl_->last_price;
    fvals_[kBestBid] = impl_->best_bid;
    fvals_[kBestAsk] = impl_->best_ask;
    fvals_[kSpread] = impl_->best_ask - impl_->best_bid;
    fvals_[kMidPrice] = impl_->mid_price;
    fvals_[kOfiBasic] = impl_->ofi_calc.ofi_basic_;
    fvals_[kOfiAgg] = impl_->ofi_calc.ofi_agg_;
    fvals_[kOfiPca] = impl_->ofi_calc.ofi_pca_;
    fvals_[kOfiOrder] = impl_->ofi_calc.ofi_order_;
    fvals_[kOfiTrade] = impl_->ofi_calc.ofi_trade_;
    fvals_[kOfiZscore] = impl_->ofi_status.slope_filtered_z();
    fvals_[kOfiTrendSlope] =
        impl_->ofi_status.cond_mean_ofi_slope(impl_->ofi_status.current_state());
    fvals_[kOfiState] = StateToDouble(impl_->ofi_status.current_state());
    fvals_[kOfiPurityFinal] = impl_->ofi_status.purity_final();
    fvals_[kBigTradeFlag] = BoolToDouble(impl_->last_big_trade.is_big);
    fvals_[kBigTradeZscore] = impl_->last_big_trade.zscore;
    fvals_[kBigTradeThreshold] = impl_->last_big_trade.threshold;
    fvals_[kBigOrderFlag] = BoolToDouble(impl_->last_big_order.is_big);
    fvals_[kBigOrderZscore] = impl_->last_big_order.zscore;
    fvals_[kBigOrderThreshold] = impl_->last_big_order.threshold;
    fvals_[kSynthSeenOrders] = static_cast<double>(synth_seen);
    fvals_[kSynthEvents] = static_cast<double>(synth_events);
    fvals_[kSynthFilteredEvents] = static_cast<double>(synth_filtered);
    fvals_[kSynthLastOriginalVolume] = impl_->last_synth_original_volume;
    fvals_[kSynthLastDeltaVolume] = impl_->last_synth_delta_volume;
    fvals_[kSynthLastExecutionRate] = impl_->last_synth_execution_rate;
    fvals_[kSynthLastEstimated] = impl_->last_synth_estimated;
    fvals_[kSynthLastAggressorSide] = AggressorToDouble(impl_->last_aggressor);
    fvals_[kOrderIdVelocity] = order_metrics.order_id_velocity;
    fvals_[kOrderIdGapMean] = order_metrics.order_id_gap_mean;
    fvals_[kOrderPressureBuy] = order_metrics.order_flow_pressure_buy;
    fvals_[kOrderPressureSell] = order_metrics.order_flow_pressure_sell;
    fvals_[kTransRecordIdVelocity] = trans_metrics.record_id_velocity;
    fvals_[kTransRecordIdGapMean] = trans_metrics.record_id_gap_mean;
    fvals_[kTransWashProbability] = trans_metrics.wash_trading_probability;
    fvals_[kTransPressureBuy] = trans_metrics.order_book_pressure_buy;
    fvals_[kTransPressureSell] = trans_metrics.order_book_pressure_sell;
    fvals_[kActiveCompositeScore] = active_metrics.composite_score;
    fvals_[kActiveMomentumScore] = active_metrics.momentum_score;
    fvals_[kActiveFlowBiasScore] = active_metrics.flow_bias_score;
    fvals_[kActiveVolumeSpikeScore] = active_metrics.volume_spike_score;
    fvals_[kActiveTerrainScore] = active_metrics.terrain_alignment_score;
    fvals_[kChipBelowShare] = active_metrics.chip_below_share;
    fvals_[kChipAboveShare] = active_metrics.chip_above_share;
    fvals_[kChipPeakShare] = active_metrics.chip_peak_share;
    fvals_[kChipSupportTicks] =
        static_cast<double>(active_metrics.nearest_support_ticks);
    fvals_[kChipResistanceTicks] =
        static_cast<double>(active_metrics.nearest_resistance_ticks);
}

}  // namespace demofw00
}  // namespace factors
