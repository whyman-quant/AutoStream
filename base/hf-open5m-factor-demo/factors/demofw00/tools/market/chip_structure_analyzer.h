#pragma once

#include "sdp_handler/quote_format_define.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "factors/demofw00/tools/base/ring_buffer.hpp"

namespace factors {
namespace demofw00 {
namespace tools {
namespace market {

// 筹码结构变化分析器（ChipStructureAnalyzer）
// - 维护价格分箱的成交权重直方图（窗口内）
// - 支持指数衰减（半衰期）与窗口弹出
// - 提供集中度/形态/结构与迁移类指标
class ChipStructureAnalyzer {
public:
    struct Config {
        int window_seconds;           // 时间窗口（秒）
        int bin_tick;                 // 价格分箱粒度（整数价格单位的 tick 数）
        int events_cap;               // 窗口内事件上限（ring buffer 容量）
        double half_life_seconds;     // 半衰期（秒）；<=0 表示不衰减
        double peak_share_threshold;  // 支撑/压力检出最小份额阈值（占总权重的比例）

        constexpr Config()
            : window_seconds(120)      // 默认 2 分钟，便于快速观测
            , bin_tick(1)
            , events_cap(200000)
            , half_life_seconds(0.0)
            , peak_share_threshold(0.02) {}
    };

    struct Metrics {
        // 形态/集中度
        double chip_vwap = 0.0;
        double chip_std = 0.0;       // 按 tick 空间
        double chip_entropy = 0.0;   // -sum p log p
        double chip_hhi = 0.0;       // sum p^2
        double chip_top1_share = 0.0;
        double chip_top3_share = 0.0;
        double chip_top5_share = 0.0;
        int    chip_peak_price = 0;  // 价格（整数单位），为分箱中心（近似为 bin*bin_tick）
        double chip_peak_share = 0.0;

        // 结构/支撑压力
        double chip_above_share = 0.0;
        double chip_below_share = 0.0;
        double chip_profit_ratio = 0.0; // 简化：below_share
        int    nearest_resistance_ticks = -1; // 不可得为 -1
        int    nearest_support_ticks = -1;    // 不可得为 -1

        // 动态迁移
        int    chip_peak_shift_ticks = 0; // 相对上次 compute 的主峰位移（tick）
        double chip_wasserstein_approx = 0.0; // 近似 W1（以 tick 计）
        double chip_migration_speed = 0.0;    // W1 / Δt（tick/s）
    };

    explicit ChipStructureAnalyzer(const Config& config = Config())
        : config_(config),
          events_(config.events_cap) {}

    void reset() {
        events_.clear();
        bin_to_weight_.clear();
        has_base_time_ = false;
        base_time_seconds_ = 0.0;
        last_update_seconds_ = 0.0;
        prev_peak_bin_ = std::nullopt;
        prev_norm_cdf_.clear();
        prev_compute_seconds_ = 0.0;
        last_metrics_ = Metrics{};
    }

    void update(const Stock_Transaction_Internal_Book_New& trade) {
        if (trade.trade_price <= 0 || trade.trade_volume <= 0) return;
        if (trade.trade_type == 'C') return; // 过滤撤销类

        const double now_seconds = convert_time_to_seconds(trade.int_time);
        if (!has_base_time_) {
            has_base_time_ = true;
            base_time_seconds_ = now_seconds;
        }

        // 过窗弹出
        prune_expired(static_cast<int>(now_seconds));

        // 分箱
        const int price_bin = price_to_bin(trade.trade_price);

        // 衰减：按统一基准时间记录加权值（避免反复缩放）
        const double stored_weight = compute_stored_weight(trade.trade_volume, now_seconds);

        // 入窗：ring buffer + 直方图累加
        events_.push_back(TradeEntry{trade.int_time, price_bin, trade.trade_volume, stored_weight});
        bin_to_weight_[price_bin] += stored_weight;

        last_update_seconds_ = now_seconds;

        // 控制容量：如超过 events_cap，进一步按时间弹出
        while (static_cast<int>(events_.size()) > config_.events_cap) {
            pop_one_front();
        }
    }

    // 计算指标（需传入当前价格；若无需支撑/压力计算，可传 0）
    void compute(int current_price) {
        // 收集当前分布（无需应用全局缩放，归一化会抵消）
        if (bin_to_weight_.empty()) {
            last_metrics_ = Metrics{};
            // 保持 prev_* 不变
            return;
        }

        // 总权重与一阶/二阶矩
        double sum_w = 0.0;
        double sum_wp = 0.0;
        double sum_wp2 = 0.0;
        int peak_bin = 0;
        double peak_w = -1.0;

        // 复制到向量以便排序与 TopK
        std::vector<std::pair<int, double>> bins(bin_to_weight_.begin(), bin_to_weight_.end());
        std::sort(bins.begin(), bins.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

        for (const auto& kv : bins) {
            const int bin = kv.first;
            const double w = kv.second;
            if (w <= 0.0) continue;
            const double price_value = static_cast<double>(bin) * static_cast<double>(config_.bin_tick);
            sum_w += w;
            sum_wp += w * price_value;
            sum_wp2 += w * price_value * price_value;
            if (w > peak_w) {
                peak_w = w;
                peak_bin = bin;
            }
        }
        if (sum_w <= 0.0) {
            last_metrics_ = Metrics{};
            return;
        }

        Metrics m;
        const double mu = sum_wp / sum_w;
        const double var = std::max(0.0, sum_wp2 / sum_w - mu * mu);
        m.chip_vwap = mu;
        m.chip_std = std::sqrt(var); // 单位为整数价格单位（tick 大小的倍数）

        // 归一化份额
        std::vector<double> shares;
        shares.reserve(bins.size());
        for (const auto& kv : bins) {
            const double w = kv.second;
            if (w <= 0.0) continue;
            shares.push_back(w / sum_w);
        }
        // 熵/HHI/TopK
        double entropy = 0.0;
        double hhi = 0.0;
        std::sort(shares.begin(), shares.end(), std::greater<double>());
        double top1 = 0.0, top3 = 0.0, top5 = 0.0;
        for (size_t i = 0; i < shares.size(); ++i) {
            const double p = shares[i];
            if (p <= 0.0) continue;
            entropy -= p * std::log(p);
            hhi += p * p;
            if (i == 0) top1 = p;
            if (i < 3) top3 += p;
            if (i < 5) top5 += p;
        }
        m.chip_entropy = entropy;
        m.chip_hhi = hhi;
        m.chip_top1_share = top1;
        m.chip_top3_share = top3;
        m.chip_top5_share = top5;
        m.chip_peak_price = peak_bin * config_.bin_tick;
        m.chip_peak_share = peak_w / sum_w;

        // 上/下方占比 + 盈利占比（简化为下方）
        if (current_price > 0) {
            const int current_bin = price_to_bin(current_price);
            double below_sum = 0.0;
            double above_sum = 0.0;
            for (const auto& kv : bins) {
                if (kv.second <= 0.0) continue;
                if (kv.first < current_bin) below_sum += kv.second;
                else if (kv.first > current_bin) above_sum += kv.second;
            }
            m.chip_below_share = below_sum / sum_w;
            m.chip_above_share = above_sum / sum_w;
            m.chip_profit_ratio = m.chip_below_share;

            // 支撑/压力（最近超过阈值的峰）
            const double threshold_weight = config_.peak_share_threshold * sum_w;
            // 上方最近
            int nearest_up = -1;
            for (const auto& kv : bins) {
                if (kv.first <= current_bin) continue;
                if (kv.second >= threshold_weight) { nearest_up = kv.first; break; }
            }
            // 下方最近
            int nearest_dn = -1;
            for (auto it = bins.rbegin(); it != bins.rend(); ++it) {
                if (it->first >= current_bin) continue;
                if (it->second >= threshold_weight) { nearest_dn = it->first; break; }
            }
            if (nearest_up > 0) m.nearest_resistance_ticks = (nearest_up - current_bin) * config_.bin_tick;
            if (nearest_dn > 0) m.nearest_support_ticks = (current_bin - nearest_dn) * config_.bin_tick;
        }

        // 迁移：峰位移（tick）
        if (prev_peak_bin_.has_value()) {
            m.chip_peak_shift_ticks = (peak_bin - prev_peak_bin_.value()) * config_.bin_tick;
        } else {
            m.chip_peak_shift_ticks = 0;
        }

        // 迁移：Wasserstein-1 近似（按 tick）。比较当前与上一次分布
        // 构造当前归一化分布（按升序 bins）
        std::vector<std::pair<int, double>> curr_p;
        curr_p.reserve(bins.size());
        for (const auto& kv : bins) {
            if (kv.second <= 0.0) continue;
            curr_p.emplace_back(kv.first, kv.second / sum_w);
        }
        std::sort(curr_p.begin(), curr_p.end());

        if (!prev_norm_cdf_.empty()) {
            m.chip_wasserstein_approx = wasserstein_l1_on_bins(prev_norm_cdf_, curr_p) * static_cast<double>(config_.bin_tick);
            double dt = std::max(1e-9, last_update_seconds_ - prev_compute_seconds_);
            m.chip_migration_speed = m.chip_wasserstein_approx / dt;
        } else {
            m.chip_wasserstein_approx = 0.0;
            m.chip_migration_speed = 0.0;
        }

        // 写回状态
        last_metrics_ = m;
        prev_peak_bin_ = peak_bin;
        prev_norm_cdf_ = to_cdf(curr_p);
        prev_compute_seconds_ = last_update_seconds_;
    }

    const Metrics& get_metrics() const { return last_metrics_; }

private:
    struct TradeEntry {
        int int_time;         // HHMMSSmmm
        int price_bin;        // bin index
        int64_t volume;       // 原始量（可用于诊断）
        double stored_weight; // 以 base_time 为时间基的加权值
    };

    Config config_;
    base::RingBuffer<TradeEntry> events_;
    std::unordered_map<int, double> bin_to_weight_;

    bool has_base_time_ = false;
    double base_time_seconds_ = 0.0;   // 衰减的参考时间
    double last_update_seconds_ = 0.0; // 最近一次 update 的时间（秒）

    // 迁移状态
    std::optional<int> prev_peak_bin_;
    std::vector<std::pair<int, double>> prev_norm_cdf_;
    double prev_compute_seconds_ = 0.0;

    Metrics last_metrics_;

private:
    static double convert_time_to_seconds(int int_time) {
        int hour = int_time / 10000000;
        int minute = (int_time / 100000) % 100;
        int second = (int_time / 1000) % 100;
        int millisecond = int_time % 1000;
        return hour * 3600.0 + minute * 60.0 + second + millisecond / 1000.0;
    }

    int price_to_bin(int price) const {
        const int tick = std::max(1, config_.bin_tick);
        if (price >= 0) return price / tick;
        // 负价不应出现，兜底
        return -((-price) / tick);
    }

    double compute_stored_weight(int64_t volume, double now_seconds) const {
        if (config_.half_life_seconds <= 0.0) return static_cast<double>(volume);
        const double tau = config_.half_life_seconds / std::log(2.0);
        const double dt = std::max(0.0, now_seconds - base_time_seconds_);
        const double decay = std::exp(-dt / tau);
        return static_cast<double>(volume) * decay;
    }

    void prune_expired(int now_seconds_int) {
        // 弹出超窗的事件（按 int_time 比较）
        const int threshold = now_seconds_int - config_.window_seconds;
        while (!events_.empty()) {
            const TradeEntry& front = events_.front();
            const double front_seconds = convert_time_to_seconds(front.int_time);
            if (front_seconds <= static_cast<double>(threshold)) {
                // 从直方图扣减
                auto it = bin_to_weight_.find(front.price_bin);
                if (it != bin_to_weight_.end()) {
                    it->second -= front.stored_weight;
                    if (it->second <= 1e-12) bin_to_weight_.erase(it);
                }
                events_.pop_front();
            } else {
                break;
            }
        }
    }

    void pop_one_front() {
        if (events_.empty()) return;
        const TradeEntry& front = events_.front();
        auto it = bin_to_weight_.find(front.price_bin);
        if (it != bin_to_weight_.end()) {
            it->second -= front.stored_weight;
            if (it->second <= 1e-12) bin_to_weight_.erase(it);
        }
        events_.pop_front();
    }

    // 将离散分布转为累积分布（升序 bins）
    static std::vector<std::pair<int, double>> to_cdf(const std::vector<std::pair<int, double>>& pmf) {
        std::vector<std::pair<int, double>> cdf;
        cdf.reserve(pmf.size());
        double acc = 0.0;
        for (const auto& kv : pmf) {
            acc += kv.second;
            cdf.emplace_back(kv.first, acc);
        }
        // 归一化到 1（数值稳健）
        if (!cdf.empty() && std::abs(cdf.back().second - 1.0) > 1e-9) {
            const double s = cdf.back().second;
            for (auto& kv : cdf) kv.second = (s > 0.0) ? (kv.second / s) : 0.0;
        }
        return cdf;
    }

    // 计算两个离散分布在价格轴上的 L1 累积差积分（W1 近似，未乘 bin 宽）
    static double wasserstein_l1_on_bins(const std::vector<std::pair<int, double>>& prev_cdf,
                                         const std::vector<std::pair<int, double>>& curr_pmf) {
        // 将 curr pmf 转为 cdf，并在联合键上累计
        auto curr_cdf = to_cdf(curr_pmf);
        // 合并两者的键
        std::vector<int> keys;
        keys.reserve(prev_cdf.size() + curr_cdf.size());
        for (const auto& kv : prev_cdf) keys.push_back(kv.first);
        for (const auto& kv : curr_cdf) keys.push_back(kv.first);
        std::sort(keys.begin(), keys.end());
        keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
        if (keys.empty()) return 0.0;

        // 双指针遍历 cdf
        double dist = 0.0;
        size_t i = 0, j = 0;
        double prev_f = 0.0, curr_f = 0.0;
        for (size_t k = 0; k < keys.size(); ++k) {
            const int key = keys[k];
            while (i < prev_cdf.size() && prev_cdf[i].first <= key) { prev_f = prev_cdf[i].second; ++i; }
            while (j < curr_cdf.size() && curr_cdf[j].first <= key) { curr_f = curr_cdf[j].second; ++j; }
            double prev_key = (k == 0) ? static_cast<double>(key) : static_cast<double>(keys[k - 1]);
            double width = static_cast<double>(key) - prev_key;
            dist += std::abs(curr_f - prev_f) * std::max(0.0, width);
        }
        return dist;
    }
};

}  // namespace market
}  // namespace tools
}  // namespace demofw00
}  // namespace factors
