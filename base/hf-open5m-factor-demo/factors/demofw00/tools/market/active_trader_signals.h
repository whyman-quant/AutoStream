#pragma once

#include "sdp_handler/quote_format_define.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <string>

#include "factors/demofw00/tools/base/ring_buffer.hpp"
#include "factors/demofw00/tools/base/basic_stats.h"
#include "factors/demofw00/tools/base/trend_fit.h"
#include "factors/demofw00/tools/market/chip_structure_analyzer.h"

namespace factors {
namespace demofw00 {
namespace tools {
namespace market {

// ActiveTraderSignals: 游资视角指标集合（动能/流量/量能/阵地/时机 → 组合得分）
class ActiveTraderSignals {
public:
    struct Config {
        // 趋势窗口（秒）
        int trend_short_window_seconds;
        int trend_mid_window_seconds;
        // 量能窗口（秒）
        int volume_window_seconds;
        int ring_capacity;
        // 筹码
        ChipStructureAnalyzer::Config chip_config;
        // 关键时段权重
        double weight_open_15m;
        double weight_after_1430;
        // 分项权重
        double weight_momentum;
        double weight_flow_bias;
        double weight_volume_spike;
        double weight_terrain_alignment;
        double weight_session_time;
        // 方向：+1 做多，-1 做空
        int direction_sign;

        constexpr Config()
            : trend_short_window_seconds(60)
            , trend_mid_window_seconds(180)
            , volume_window_seconds(120)
            , ring_capacity(200000)
            , chip_config()
            , weight_open_15m(1.2)
            , weight_after_1430(1.2)
            , weight_momentum(0.35)
            , weight_flow_bias(0.20)
            , weight_volume_spike(0.15)
            , weight_terrain_alignment(0.25)
            , weight_session_time(0.05)
            , direction_sign(+1) {}
    };

    struct Metrics {
        // 动能（趋势/直线度）
        double trend_short_slope = 0.0, trend_short_r2 = 0.0;
        double trend_mid_slope   = 0.0, trend_mid_r2   = 0.0;
        double momentum_score = 0.0;           // 0~100
        // 流量（买卖占比）
        double buy_volume_share = 0.0, sell_volume_share = 0.0;
        double flow_bias_score = 0.0;          // 0~100
        // 量能突变
        double last_volume = 0.0, mean_volume = 0.0, std_volume = 0.0, volume_zscore = 0.0;
        double volume_spike_score = 0.0;       // 0~100
        // 阵地（筹码/VWAP/支撑压力）
        double vwap_price = 0.0, vwap_deviation = 0.0;
        double chip_above_share = 0.0, chip_below_share = 0.0, chip_peak_share = 0.0;
        int nearest_resistance_ticks = -1, nearest_support_ticks = -1;
        double terrain_alignment_score = 0.0;  // 0~100
        // 时段
        double session_time_weight = 1.0;
        // 组合
        double composite_score = 0.0;          // 0~100
        std::string explanation;
    };

    explicit ActiveTraderSignals(const Config& cfg = Config())
        : cfg_(cfg)
        , trend_short_(cfg.trend_short_window_seconds)
        , trend_mid_(cfg.trend_mid_window_seconds)
        , chip_(cfg.chip_config)
        , vol_queue_(cfg.ring_capacity) {}

    void reset() {
        trend_short_ = base::TrendFit(cfg_.trend_short_window_seconds);
        trend_mid_   = base::TrendFit(cfg_.trend_mid_window_seconds);
        chip_.reset();
        vol_queue_.clear();
        vol_stats_.reset();
        sum_pv_day_ = 0.0;
        sum_v_day_  = 0.0;
        buy_acc_ = sell_acc_ = 0;
        last_int_time_ = 0;
    }

    // 输入：逐笔成交
    void update(const Stock_Transaction_Internal_Book_New& trd) {
        if (trd.trade_type == 'C' || trd.trade_price <= 0 || trd.trade_volume <= 0) return;
        last_int_time_ = trd.int_time;
        // 趋势
        trend_short_.update(trd.int_time, static_cast<double>(trd.trade_price));
        trend_mid_.update(trd.int_time, static_cast<double>(trd.trade_price));
        // 筹码
        chip_.update(trd);
        // VWAP（日内）
        sum_pv_day_ += static_cast<double>(trd.trade_price) * static_cast<double>(trd.trade_volume);
        sum_v_day_  += static_cast<double>(trd.trade_volume);
        // 量能窗口
        vol_queue_.push_back(VolEntry{trd.int_time, static_cast<double>(trd.trade_volume)});
        vol_stats_.update(static_cast<double>(trd.trade_volume));
        prune_volume(trd.int_time);
        // 流量
        if (trd.bsflag == 'B') buy_acc_ += trd.trade_volume; else if (trd.bsflag == 'S') sell_acc_ += trd.trade_volume;
    }

    Metrics compute(int current_price) {
        Metrics m;
        // 动能
        auto ss = trend_short_.get();
        auto sm = trend_mid_.get();
        m.trend_short_slope = ss.slope; m.trend_short_r2 = ss.r2;
        m.trend_mid_slope   = sm.slope; m.trend_mid_r2   = sm.r2;
        double dir = static_cast<double>(cfg_.direction_sign);
        double s_short = dir * ss.slope, s_mid = dir * sm.slope;
        double mom_raw = std::max(0.0, 0.5 * s_short * ss.r2 + 0.5 * s_mid * sm.r2);
        m.momentum_score = clamp01(std::tanh(mom_raw * 10.0)) * 100.0;
        // 流量
        double tot = static_cast<double>(buy_acc_ + sell_acc_);
        m.buy_volume_share  = (tot > 0.0 ? static_cast<double>(buy_acc_) / tot : 0.0);
        m.sell_volume_share = 1.0 - m.buy_volume_share;
        double flow_raw = (cfg_.direction_sign > 0) ? (m.buy_volume_share - 0.5) : (m.sell_volume_share - 0.5);
        m.flow_bias_score = clamp01(flow_raw * 2.5) * 100.0;
        // 量能
        m.last_volume = (!vol_queue_.empty() ? vol_queue_.back().volume : 0.0);
        m.mean_volume = vol_stats_.mean();
        m.std_volume  = vol_stats_.std_dev();
        if (m.std_volume > 1e-12) m.volume_zscore = (m.last_volume - m.mean_volume) / m.std_volume;
        m.volume_spike_score = clamp01((m.volume_zscore - 0.0) / 3.0) * 100.0;
        // 阵地（筹码/VWAP）
        chip_.compute(current_price);
        const auto& c = chip_.get_metrics();
        m.chip_above_share = c.chip_above_share;
        m.chip_below_share = c.chip_below_share;
        m.chip_peak_share  = c.chip_peak_share;
        m.nearest_resistance_ticks = c.nearest_resistance_ticks;
        m.nearest_support_ticks    = c.nearest_support_ticks;
        m.vwap_price     = (sum_v_day_ > 0.0 ? (sum_pv_day_ / sum_v_day_) : 0.0);
        m.vwap_deviation = static_cast<double>(current_price) - m.vwap_price;
        double terr_raw = 0.0;
        if (cfg_.direction_sign > 0) {
            terr_raw += (m.chip_below_share - 0.5);
            terr_raw += (m.nearest_resistance_ticks <= 0 ? 0.2 : 0.0);
            terr_raw += (m.vwap_deviation >= 0 ? 0.2 : -0.2);
        } else {
            terr_raw += (m.chip_above_share - 0.5);
            terr_raw += (m.nearest_support_ticks <= 0 ? 0.2 : 0.0);
            terr_raw += (m.vwap_deviation <= 0 ? 0.2 : -0.2);
        }
        m.terrain_alignment_score = clamp01(terr_raw + 0.5) * 100.0;
        // 时段权重
        m.session_time_weight = session_time_weight(last_int_time_);
        // 组合分
        double score = 0.0;
        score += cfg_.weight_momentum          * (m.momentum_score          / 100.0);
        score += cfg_.weight_flow_bias         * (m.flow_bias_score         / 100.0);
        score += cfg_.weight_volume_spike      * (m.volume_spike_score      / 100.0);
        score += cfg_.weight_terrain_alignment * (m.terrain_alignment_score / 100.0);
        score *= (1.0 + cfg_.weight_session_time * (m.session_time_weight - 1.0));
        m.composite_score = std::max(0.0, std::min(100.0, score * 100.0));
        // 解释
        m.explanation = build_explanation(m);
        return m;
    }

private:
    struct VolEntry { int mark; double volume; };

    Config cfg_;
    base::TrendFit trend_short_;
    base::TrendFit trend_mid_;
    ChipStructureAnalyzer chip_;

    base::RingBuffer<VolEntry> vol_queue_;
    base::BasicStats vol_stats_{2, 0};

    double sum_pv_day_ = 0.0;
    double sum_v_day_  = 0.0;
    long long buy_acc_ = 0, sell_acc_ = 0;
    int last_int_time_ = 0;

private:
    static double clamp01(double x) { return std::max(0.0, std::min(1.0, x)); }

    void prune_volume(int int_time_now) {
        const int threshold = int_time_now - cfg_.volume_window_seconds;
        while (!vol_queue_.empty()) {
            const VolEntry& f = vol_queue_.front();
            if (f.mark <= threshold) { vol_stats_.pop(f.volume); vol_queue_.pop_front(); }
            else break;
        }
    }

    double session_time_weight(int int_time) const {
        if (int_time <= 0) return 1.0;
        int hh = int_time / 10000000;
        int mm = (int_time / 100000) % 100;
        // 开盘首15分钟
        if ((hh == 9 && mm >= 30 && (hh * 60 + mm) < 9 * 60 + 45) || (hh == 9 && mm < 45)) return cfg_.weight_open_15m;
        // 14:30 以后
        if (hh > 14 || (hh == 14 && mm >= 30)) return cfg_.weight_after_1430;
        return 1.0;
    }

    std::string build_explanation(const Metrics& m) const {
        char buf[512];
        snprintf(buf, sizeof(buf),
                 "mom(r2_s=%.2f,r2_m=%.2f) flow(buy=%.0f%%) vol(z=%.2f) terr(below=%.0f%%,res=%d,sup=%d,vwap_dev=%.0f)",
                 m.trend_short_r2, m.trend_mid_r2,
                 m.buy_volume_share * 100.0,
                 m.volume_zscore,
                 m.chip_below_share * 100.0,
                 m.nearest_resistance_ticks, m.nearest_support_ticks,
                 m.vwap_deviation);
        return std::string(buf);
    }
};

}  // namespace market
}  // namespace tools
}  // namespace demofw00
}  // namespace factors
