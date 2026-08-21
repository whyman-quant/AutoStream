#pragma once

#include "sdp_handler/quote_format_define.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "factors/demofw00/tools/base/basic_stats.h"

namespace factors {
namespace demofw00 {
namespace tools {
namespace market {

// 基于委托流(Stock_Order_Internal_Book_New)的订单ID分析器
// 参考机器版实现，做轻量化适配：
// - 使用 base::BasicStats 维护间隔统计
// - 提供增量更新与指标获取接口
class OrderIDAnalyzerForOrders {
public:
    struct Config {
        int history_window_size;              // 历史窗口（预留，不存大历史）
        int price_clustering_threshold;       // 价格聚类阈值(以最小报价单位计)
        double time_clustering_threshold;     // 时间聚集阈值(秒)
        int64_t large_order_volume_threshold; // 大单阈值(委托量)

        constexpr Config()
            : history_window_size(100)
            , price_clustering_threshold(2)
            , time_clustering_threshold(1.0)
            , large_order_volume_threshold(50000) {}
    };

    struct OrderMetrics {
        // 订单ID序列
        double order_id_velocity;  // ID增长速度(条/秒)
        double order_id_gap_mean;  // ID间隔均值
        double order_id_gap_std;   // ID间隔标准差
        int order_id_jump_count;   // 异常跳跃次数

        // 价格聚类(按price_clustering_threshold分簇) - 轻量增量统计
        int price_clusters;            // 价格聚类数量(游程聚类)
        double avg_price_cluster_size; // 平均聚类大小
        int max_price_cluster_size;    // 最大聚类大小

        // 大单统计
        int potential_large_orders; // 大单数量
        double large_order_ratio;   // 大单占比

        // 随机性/熵(近似)
        double order_id_entropy; // ID熵近似(0~1)

        // 市场活跃度/方向压力
        double trading_intensity;        // 交易强度(条/秒)
        double order_flow_pressure_sell; // 卖方压力[0,1]
        double order_flow_pressure_buy;  // 买方压力[0,1]

        // 统计信息
        int total_orders_analyzed;
        double analysis_time_span;

        OrderMetrics()
            : order_id_velocity(0.0)
            , order_id_gap_mean(0.0)
            , order_id_gap_std(0.0)
            , order_id_jump_count(0)
            , price_clusters(0)
            , avg_price_cluster_size(0.0)
            , max_price_cluster_size(0)
            , potential_large_orders(0)
            , large_order_ratio(0.0)
            , order_id_entropy(0.0)
            , trading_intensity(0.0)
            , order_flow_pressure_sell(0.0)
            , order_flow_pressure_buy(0.0)
            , total_orders_analyzed(0)
            , analysis_time_span(0.0) {}
    };

private:
    Config config_;

    // 最小必要状态（无大历史缓存）
    bool has_last_order_id_ = false;
    int64_t last_order_id_ = 0;
    bool has_last_price_ = false;
    int last_price_ = 0;
    bool has_last_timestamp_ = false;
    double last_timestamp_seen_ = 0.0;

    // 统计量
    base::BasicStats order_id_gap_stats_{2, 0};
    base::BasicStats time_gap_stats_{2, 0};
    base::BasicStats price_gap_stats_{2, 0};
    base::BasicStats volume_stats_{2, 0};

    // 价格聚类游程（无需存整簇）
    int last_cluster_price_ = 0;
    int current_cluster_size_ = 0;

    // 状态
    double first_timestamp_ = 0.0;
    double last_timestamp_ = 0.0;
    int total_orders_ = 0;

    // 增量：ID跳跃
    mutable int id_jump_count_ = 0;

    // 增量：买卖压力
    mutable int sell_orders_count_ = 0;
    mutable int buy_orders_count_ = 0;
    mutable double sell_pressure_ = 0.0;
    mutable double buy_pressure_ = 0.0;

    // 增量：大单
    mutable int large_orders_count_ = 0;
    mutable double large_order_ratio_ = 0.0;

    // 增量：熵(近似)
    mutable double current_entropy_ = 0.0;
    mutable int total_id_count_ = 0;

public:
    explicit OrderIDAnalyzerForOrders(const Config& config = Config())
        : config_(config) {}

    void update(const Stock_Order_Internal_Book_New& order) {
        double timestamp = convert_time_to_seconds(order.int_time);

        update_basic_stats(order, timestamp);
        update_price_patterns(order);
        update_price_clustering(order.order_price);

        if (total_orders_ == 0) {
            first_timestamp_ = timestamp;
        }
        last_timestamp_ = timestamp;
        total_orders_++;
    }

    OrderMetrics get_metrics() const {
        OrderMetrics m = {};
        m.total_orders_analyzed = total_orders_;
        m.analysis_time_span = last_timestamp_ - first_timestamp_;
        if (total_orders_ < 2) {
            return m;
        }

        // ID间隔
        if (order_id_gap_stats_.num() > 0) {
            m.order_id_gap_mean = order_id_gap_stats_.mean();
            m.order_id_gap_std = order_id_gap_stats_.std_dev();
        }
        if (time_gap_stats_.num() > 0 && time_gap_stats_.mean() > 0) {
            m.order_id_velocity = 1.0 / time_gap_stats_.mean();
        }
        m.order_id_jump_count = id_jump_count_;

        // 价格聚类统计
        m.price_clusters = price_clusters_;
        m.avg_price_cluster_size = avg_price_cluster_size_;
        m.max_price_cluster_size = max_price_cluster_size_;

        // 大单
        m.potential_large_orders = large_orders_count_;
        m.large_order_ratio = large_order_ratio_;

        // 熵/随机性
        m.order_id_entropy = current_entropy_;

        // 活跃度与方向压力
        if (time_gap_stats_.num() > 0) {
            m.trading_intensity = 1.0 / time_gap_stats_.mean();
        }
        m.order_flow_pressure_sell = sell_pressure_;
        m.order_flow_pressure_buy = buy_pressure_;

        return m;
    }

    void reset() {
        has_last_order_id_ = false;
        last_order_id_ = 0;
        has_last_price_ = false;
        last_price_ = 0;
        has_last_timestamp_ = false;
        last_timestamp_seen_ = 0.0;

        order_id_gap_stats_.reset();
        time_gap_stats_.reset();
        price_gap_stats_.reset();
        volume_stats_.reset();

        last_cluster_price_ = 0;
        current_cluster_size_ = 0;

        first_timestamp_ = 0.0;
        last_timestamp_ = 0.0;
        total_orders_ = 0;

        id_jump_count_ = 0;

        sell_orders_count_ = 0;
        buy_orders_count_ = 0;
        sell_pressure_ = 0.0;
        buy_pressure_ = 0.0;

        large_orders_count_ = 0;
        large_order_ratio_ = 0.0;

        current_entropy_ = 0.0;
        total_id_count_ = 0;

        price_clusters_ = 0;
        total_price_cluster_size_ = 0;
        avg_price_cluster_size_ = 0.0;
        max_price_cluster_size_ = 0;
    }

    void update_config(const Config& cfg) { config_ = cfg; }

private:
    static double convert_time_to_seconds(int int_time) {
        int hour = int_time / 10000000;
        int minute = (int_time / 100000) % 100;
        int second = (int_time / 1000) % 100;
        int millisecond = int_time % 1000;
        return hour * 3600.0 + minute * 60.0 + second + millisecond / 1000.0;
    }

    void update_basic_stats(const Stock_Order_Internal_Book_New& order, double timestamp) {
        // ID间隔与跳跃（基于上一个ID）
        if (has_last_order_id_) {
            int64_t gap = order.order_index - last_order_id_;
            order_id_gap_stats_.update(static_cast<double>(gap));

            double mean_gap = order_id_gap_stats_.mean();
            double std_gap = order_id_gap_stats_.std_dev();
            double jump_threshold = mean_gap + 3 * std::max(1e-9, std_gap);
            if (gap > jump_threshold) {
                id_jump_count_++;
            }
        }
        last_order_id_ = order.order_index;
        has_last_order_id_ = true;

        // 时间间隔（基于上一个时间）
        if (has_last_timestamp_) {
            double time_gap = timestamp - last_timestamp_seen_;
            time_gap_stats_.update(time_gap);
        }
        last_timestamp_seen_ = timestamp;
        has_last_timestamp_ = true;

        // 方向压力
        if (order.bsflag == 'S') {
            sell_orders_count_++;
        } else if (order.bsflag == 'B') {
            buy_orders_count_++;
        }
        int total = sell_orders_count_ + buy_orders_count_;
        if (total > 0) {
            sell_pressure_ = static_cast<double>(sell_orders_count_) / total;
            buy_pressure_ = static_cast<double>(buy_orders_count_) / total;
        }

        // 体量
        volume_stats_.update(static_cast<double>(order.order_volume));

        // 大单(阈值)
        if (order.order_volume >= config_.large_order_volume_threshold) {
            large_orders_count_++;
        }
        if (total_orders_ >= 0) {
            large_order_ratio_ = static_cast<double>(large_orders_count_) / (total_orders_ + 1);
        }

        // 熵近似: 基于ID相邻差分幅度归一
        update_entropy_incrementally(order.order_index);
    }

    void update_price_patterns(const Stock_Order_Internal_Book_New& order) {
        if (has_last_price_) {
            int gap = std::abs(order.order_price - last_price_);
            price_gap_stats_.update(static_cast<double>(gap));
        }
        last_price_ = order.order_price;
        has_last_price_ = true;
    }

    // 基于游程的近邻聚类（轻量增量）
    void update_price_clustering(int latest_price) {
        if (latest_price <= 0) return;
        if (current_cluster_size_ == 0) {
            last_cluster_price_ = latest_price;
            current_cluster_size_ = 1;
        } else if (std::abs(latest_price - last_cluster_price_) <= config_.price_clustering_threshold) {
            current_cluster_size_ += 1;
            last_cluster_price_ = latest_price;
            if (current_cluster_size_ == 2) {
                price_clusters_ += 1;
            }
            total_price_cluster_size_ += 1; // 仅统计簇中额外元素
            if (price_clusters_ > 0) {
                avg_price_cluster_size_ = static_cast<double>(total_price_cluster_size_) / price_clusters_;
            }
            if (current_cluster_size_ > max_price_cluster_size_) {
                max_price_cluster_size_ = current_cluster_size_;
            }
        } else {
            // 断开，开启新簇
            last_cluster_price_ = latest_price;
            current_cluster_size_ = 1;
        }
    }

    void update_entropy_incrementally(int64_t order_id) {
        const double alpha = 0.01;
        double score = 0.0;
        if (has_last_order_id_) {
            int64_t diff = std::llabs(order_id - last_order_id_);
            score = std::min(1.0, static_cast<double>(diff) / 1e6);
        }
        current_entropy_ = alpha * score + (1 - alpha) * current_entropy_;
        total_id_count_++;
    }

private:
    // 聚类增量统计
    int price_clusters_ = 0;
    int total_price_cluster_size_ = 0;
    double avg_price_cluster_size_ = 0.0;
    int max_price_cluster_size_ = 0;
};

}  // namespace market
}  // namespace tools
}  // namespace demofw00
}  // namespace factors
