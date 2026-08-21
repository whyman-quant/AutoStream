#pragma once

#include "sdp_handler/quote_format_define.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "factors/demofw00/tools/base/basic_stats.h"
#include "factors/demofw00/tools/base/ring_buffer.hpp"

namespace factors {
namespace demofw00 {
namespace tools {
namespace market {

// 成交ID与委托序号分析器（轻量版）
class OrderIDAnalyzerForTrans {
public:
    struct Config {
        int history_window_size;          // 历史窗口大小（用于 base::RingBuffer 容量）
        int clustering_threshold;         // ID聚类阈值
        double time_clustering_threshold; // 时间聚类阈值（秒）
        double wash_trading_threshold;    // 对敲检测阈值（占比）

        constexpr Config()
            : history_window_size(100)
            , clustering_threshold(10)
            , time_clustering_threshold(1.0)
            , wash_trading_threshold(0.1) {}
    };

    struct Metrics {
        // 成交编号分析
        double record_id_velocity;   // 成交编号增长速度（个/秒）
        double record_id_gap_mean;   // 成交编号间隔均值
        double record_id_gap_std;    // 成交编号间隔标准差
        int record_id_jump_count;    // 成交编号跳跃次数

        // 委托序号聚类（增量统计）
        int sell_order_clusters;     // 卖方聚类数量
        int buy_order_clusters;      // 买方聚类数量
        double avg_sell_cluster_size;
        double avg_buy_cluster_size;
        int max_sell_cluster_size;
        int max_buy_cluster_size;

        // 大单拆分近似
        int potential_large_sell_orders;
        int potential_large_buy_orders;
        double large_order_fragment_ratio;

        // 异常模式
        double wash_trading_probability;  // 对敲交易概率（近似）
        int suspicious_matching_count;    // 可疑匹配次数（sell_id==buy_id）
        double order_id_entropy;          // 委托ID熵值（近似）

        // 活跃度
        double trading_intensity;         // 交易强度
        double order_book_pressure_sell;  // 卖方压力
        double order_book_pressure_buy;   // 买方压力

        // 统计信息
        int total_trades_analyzed;
        double analysis_time_span;

        Metrics()
            : record_id_velocity(0.0)
            , record_id_gap_mean(0.0)
            , record_id_gap_std(0.0)
            , record_id_jump_count(0)
            , sell_order_clusters(0)
            , buy_order_clusters(0)
            , avg_sell_cluster_size(0.0)
            , avg_buy_cluster_size(0.0)
            , max_sell_cluster_size(0)
            , max_buy_cluster_size(0)
            , potential_large_sell_orders(0)
            , potential_large_buy_orders(0)
            , large_order_fragment_ratio(0.0)
            , wash_trading_probability(0.0)
            , suspicious_matching_count(0)
            , order_id_entropy(0.0)
            , trading_intensity(0.0)
            , order_book_pressure_sell(0.0)
            , order_book_pressure_buy(0.0)
            , total_trades_analyzed(0)
            , analysis_time_span(0.0) {}
    };

private:
    Config config_;

    // 历史缓存
    base::RingBuffer<int64_t> record_ids_;   // 成交编号历史
    base::RingBuffer<int> sell_ids_;         // 卖方委托序号历史
    base::RingBuffer<int> buy_ids_;          // 买方委托序号历史
    base::RingBuffer<double> timestamps_;    // 时间戳历史
    base::RingBuffer<int64_t> volumes_;      // 成交量历史
    base::RingBuffer<char> sides_;           // 方向历史

    // 统计量
    base::BasicStats record_id_gap_stats_{2, 0};
    base::BasicStats sell_id_gap_stats_{2, 0};
    base::BasicStats buy_id_gap_stats_{2, 0};
    base::BasicStats time_gap_stats_{2, 0};
    base::BasicStats volume_stats_{2, 0};

    // 模式/频率
    std::unordered_map<int, int> sell_id_frequency_;
    std::unordered_map<int, int> buy_id_frequency_;

    // 分析状态
    double first_timestamp_ = 0.0;
    double last_timestamp_ = 0.0;
    int total_trades_ = 0;

    // 增量缓存
    mutable int jump_count_ = 0;
    mutable int64_t last_record_id_ = 0;

    mutable int sell_cluster_count_ = 0;
    mutable int buy_cluster_count_ = 0;
    mutable double avg_sell_cluster_size_ = 0.0;
    mutable double avg_buy_cluster_size_ = 0.0;
    mutable int max_sell_cluster_size_ = 0;
    mutable int max_buy_cluster_size_ = 0;
    mutable int total_sell_cluster_size_ = 0;
    mutable int total_buy_cluster_size_ = 0;

    mutable int large_sell_orders_ = 0;
    mutable int large_buy_orders_ = 0;
    mutable double large_order_ratio_ = 0.0;

    mutable int sell_trades_count_ = 0;
    mutable int buy_trades_count_ = 0;
    mutable double sell_pressure_ = 0.0;
    mutable double buy_pressure_ = 0.0;

    mutable double current_entropy_ = 0.0; // 近似熵

public:
    explicit OrderIDAnalyzerForTrans(const Config& config = Config())
        : config_(config)
        , record_ids_(config.history_window_size)
        , sell_ids_(config.history_window_size)
        , buy_ids_(config.history_window_size)
        , timestamps_(config.history_window_size)
        , volumes_(config.history_window_size)
        , sides_(config.history_window_size) {}

    void update(const Stock_Transaction_Internal_Book_New& trade) {
        double timestamp = convert_time_to_seconds(trade.int_time);

        update_basic_stats(trade, timestamp);
        update_id_patterns(trade);
        update_clusters_incrementally(trade);
        update_anomaly_incrementally(trade);

        // 入历史
        record_ids_.push_back(trade.trade_index);
        sell_ids_.push_back(static_cast<int>(trade.sell_id));
        buy_ids_.push_back(static_cast<int>(trade.buy_id));
        timestamps_.push_back(timestamp);
        volumes_.push_back(trade.trade_volume);
        sides_.push_back(trade.bsflag);

        if (total_trades_ == 0) first_timestamp_ = timestamp;
        last_timestamp_ = timestamp;
        total_trades_++;
    }

    Metrics get_metrics() const {
        Metrics m = {};
        m.total_trades_analyzed = total_trades_;
        m.analysis_time_span = last_timestamp_ - first_timestamp_;
        if (record_ids_.size() < 2) return m;

        if (record_id_gap_stats_.num() > 0) {
            m.record_id_gap_mean = record_id_gap_stats_.mean();
            m.record_id_gap_std = record_id_gap_stats_.std_dev();
        }
        if (time_gap_stats_.num() > 0 && time_gap_stats_.mean() > 0) {
            m.record_id_velocity = 1.0 / time_gap_stats_.mean();
        }
        m.record_id_jump_count = jump_count_;

        // 聚类统计（增量缓存）
        m.sell_order_clusters = sell_cluster_count_;
        m.buy_order_clusters = buy_cluster_count_;
        m.avg_sell_cluster_size = avg_sell_cluster_size_;
        m.avg_buy_cluster_size = avg_buy_cluster_size_;
        m.max_sell_cluster_size = max_sell_cluster_size_;
        m.max_buy_cluster_size = max_buy_cluster_size_;

        // 大单/碎片化
        m.potential_large_sell_orders = large_sell_orders_;
        m.potential_large_buy_orders = large_buy_orders_;
        m.large_order_fragment_ratio = large_order_ratio_;

        // 异常/活跃度
        m.order_id_entropy = current_entropy_;
        if (time_gap_stats_.num() > 0) m.trading_intensity = 1.0 / time_gap_stats_.mean();
        m.order_book_pressure_sell = sell_pressure_;
        m.order_book_pressure_buy = buy_pressure_;
        // 对敲概率用 suspicious_matching_count/总对比数 的 EMA 近似，这里直接给出当前近似
        m.wash_trading_probability = wash_prob_ema_;
        m.suspicious_matching_count = suspicious_count_;

        return m;
    }

    void reset() {
        record_ids_.clear();
        sell_ids_.clear();
        buy_ids_.clear();
        timestamps_.clear();
        volumes_.clear();
        sides_.clear();

        record_id_gap_stats_.reset();
        sell_id_gap_stats_.reset();
        buy_id_gap_stats_.reset();
        time_gap_stats_.reset();
        volume_stats_.reset();

        sell_id_frequency_.clear();
        buy_id_frequency_.clear();

        jump_count_ = 0;
        last_record_id_ = 0;

        sell_cluster_count_ = buy_cluster_count_ = 0;
        avg_sell_cluster_size_ = avg_buy_cluster_size_ = 0.0;
        max_sell_cluster_size_ = max_buy_cluster_size_ = 0;
        total_sell_cluster_size_ = total_buy_cluster_size_ = 0;

        large_sell_orders_ = large_buy_orders_ = 0;
        large_order_ratio_ = 0.0;

        sell_trades_count_ = buy_trades_count_ = 0;
        sell_pressure_ = buy_pressure_ = 0.0;

        current_entropy_ = 0.0;
        suspicious_count_ = 0;
        total_pairs_ = 0;
        wash_prob_ema_ = 0.0;

        first_timestamp_ = last_timestamp_ = 0.0;
        total_trades_ = 0;
    }

    void update_config(const Config& c) {
        if (c.history_window_size != config_.history_window_size) {
            config_ = c;
            reset();
            record_ids_ = base::RingBuffer<int64_t>(config_.history_window_size);
            sell_ids_   = base::RingBuffer<int>(config_.history_window_size);
            buy_ids_    = base::RingBuffer<int>(config_.history_window_size);
            timestamps_ = base::RingBuffer<double>(config_.history_window_size);
            volumes_    = base::RingBuffer<int64_t>(config_.history_window_size);
            sides_      = base::RingBuffer<char>(config_.history_window_size);
        } else {
            config_ = c;
        }
    }

private:
    static double convert_time_to_seconds(int int_time) {
        int hour = int_time / 10000000;
        int minute = (int_time / 100000) % 100;
        int second = (int_time / 1000) % 100;
        int millisecond = int_time % 1000;
        return hour * 3600.0 + minute * 60.0 + second + millisecond / 1000.0;
    }

    void update_basic_stats(const Stock_Transaction_Internal_Book_New& trade, double timestamp) {
        if (!record_ids_.empty()) {
            int64_t gap = trade.trade_index - record_ids_.back();
            record_id_gap_stats_.update(static_cast<double>(gap));
            // 跳跃阈值：均值 + 3*std
            double mean_gap = record_id_gap_stats_.mean();
            double std_gap = record_id_gap_stats_.std_dev();
            double jump_threshold = mean_gap + 3 * std::max(1e-9, std_gap);
            if (gap > jump_threshold) jump_count_++;
        }

        if (!timestamps_.empty()) {
            double time_gap = timestamp - timestamps_.back();
            time_gap_stats_.update(time_gap);
        }

        // 买卖压力
        if (trade.bsflag == 'S') sell_trades_count_++; else if (trade.bsflag == 'B') buy_trades_count_++;
        int total = sell_trades_count_ + buy_trades_count_;
        if (total > 0) {
            sell_pressure_ = static_cast<double>(sell_trades_count_) / total;
            buy_pressure_ = static_cast<double>(buy_trades_count_) / total;
        }

        // 成交量
        volume_stats_.update(static_cast<double>(trade.trade_volume));

        last_record_id_ = trade.trade_index;
    }

    void update_id_patterns(const Stock_Transaction_Internal_Book_New& trade) {
        if (!sell_ids_.empty()) {
            int gap = std::abs(static_cast<int>(trade.sell_id) - sell_ids_.back());
            sell_id_gap_stats_.update(static_cast<double>(gap));
        }
        if (!buy_ids_.empty()) {
            int gap = std::abs(static_cast<int>(trade.buy_id) - buy_ids_.back());
            buy_id_gap_stats_.update(static_cast<double>(gap));
        }
        sell_id_frequency_[static_cast<int>(trade.sell_id)]++;
        buy_id_frequency_[static_cast<int>(trade.buy_id)]++;

        // 近似熵：基于买卖ID差异的EMA
        const double alpha = 0.01;
        double randomness_score = 0.0;
        if (trade.sell_id != 0 && trade.buy_id != 0) {
            randomness_score = std::min(1.0, std::abs(static_cast<double>(trade.sell_id - trade.buy_id)) / 1000.0);
        }
        current_entropy_ = alpha * randomness_score + (1 - alpha) * current_entropy_;
    }

    void update_clusters_incrementally(const Stock_Transaction_Internal_Book_New& trade) {
        update_cluster_one(static_cast<int>(trade.sell_id), true);
        update_cluster_one(static_cast<int>(trade.buy_id), false);
    }

    void update_cluster_one(int new_id, bool is_sell) {
        if (new_id == 0) return;
        // 简化：若与上一个ID差在阈值内，认为延续，否则开新簇
        int old_size = 0;
        int new_size = 1;
        if (is_sell) {
            if (!last_sell_cluster_.empty() && std::abs(new_id - last_sell_cluster_.back()) <= config_.clustering_threshold) {
                old_size = static_cast<int>(last_sell_cluster_.size());
                last_sell_cluster_.push_back(new_id);
                new_size = static_cast<int>(last_sell_cluster_.size());
            } else {
                last_sell_cluster_.clear();
                last_sell_cluster_.push_back(new_id);
                new_size = 1;
                if (new_size == 1) {
                    // 新簇出现，先记为大小1，只有扩展后才计入聚类数
                }
            }
            update_cluster_statistics(true, old_size, new_size);
        } else {
            if (!last_buy_cluster_.empty() && std::abs(new_id - last_buy_cluster_.back()) <= config_.clustering_threshold) {
                old_size = static_cast<int>(last_buy_cluster_.size());
                last_buy_cluster_.push_back(new_id);
                new_size = static_cast<int>(last_buy_cluster_.size());
            } else {
                last_buy_cluster_.clear();
                last_buy_cluster_.push_back(new_id);
                new_size = 1;
            }
            update_cluster_statistics(false, old_size, new_size);
        }
    }

    void update_cluster_statistics(bool is_sell, int old_size, int new_size) {
        const int LARGE_ORDER_THRESHOLD = 5;
        if (is_sell) {
            if (old_size > 0) {
                total_sell_cluster_size_ -= old_size;
                if (old_size >= LARGE_ORDER_THRESHOLD) large_sell_orders_--;
            }
            total_sell_cluster_size_ += new_size;
            if (new_size == 2) sell_cluster_count_++; // 从1到2视为形成聚类
            if (new_size > max_sell_cluster_size_) max_sell_cluster_size_ = new_size;
            if (sell_cluster_count_ > 0)
                avg_sell_cluster_size_ = static_cast<double>(total_sell_cluster_size_) / sell_cluster_count_;
            if (new_size >= LARGE_ORDER_THRESHOLD) large_sell_orders_++;
        } else {
            if (old_size > 0) {
                total_buy_cluster_size_ -= old_size;
                if (old_size >= LARGE_ORDER_THRESHOLD) large_buy_orders_--;
            }
            total_buy_cluster_size_ += new_size;
            if (new_size == 2) buy_cluster_count_++;
            if (new_size > max_buy_cluster_size_) max_buy_cluster_size_ = new_size;
            if (buy_cluster_count_ > 0)
                avg_buy_cluster_size_ = static_cast<double>(total_buy_cluster_size_) / buy_cluster_count_;
            if (new_size >= LARGE_ORDER_THRESHOLD) large_buy_orders_++;
        }
        if (total_trades_ > 0) {
            large_order_ratio_ = static_cast<double>(large_sell_orders_ + large_buy_orders_) / total_trades_;
        }
    }

    void update_anomaly_incrementally(const Stock_Transaction_Internal_Book_New& trade) {
        // 可疑匹配：sell_id == buy_id 且不为0
        if (trade.sell_id != 0 && trade.sell_id == trade.buy_id) {
            suspicious_count_++;
        }
        total_pairs_++;
        // 对敲概率 EMA
        const double alpha = 0.1;
        double curr_p = static_cast<double>(suspicious_count_) / std::max(1, total_pairs_);
        wash_prob_ema_ = alpha * curr_p + (1 - alpha) * wash_prob_ema_;
    }

private:
    // 简化增量聚类的最近游程
    std::vector<int> last_sell_cluster_;
    std::vector<int> last_buy_cluster_;

    // 对敲相关
    int suspicious_count_ = 0;
    int total_pairs_ = 0;
    double wash_prob_ema_ = 0.0;
};

}  // namespace market
}  // namespace tools
}  // namespace demofw00
}  // namespace factors
