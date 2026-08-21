#pragma once

#include <iostream>
#include <vector>
#include <cmath>      // 需要包含 <cmath> 用于 std::isnan
#include <limits>     // 需要包含 <limits> 用于 std::numeric_limits
#include <iomanip>
#include <string>

namespace factors {
namespace demofw00 {
namespace tools {
namespace base {

// 结构体成员现在都是 plain double
struct TrendIndicators {
    double velocity;
    double smoothed_velocity;
    double acceleration;
    double jerk;
    double snap;
    double roc;
    double ema_fast;
    double ema_slow;
    double macd_line;
    double macd_signal;
    double macd_hist;
    double variance;
    double std_dev;
};

class StreamingTrend {
public:
    TrendIndicators indicators;

public:
    explicit StreamingTrend(
        int fast_period = 12,
        int slow_period = 26,
        int signal_period = 9,
        double velocity_smooth_factor = 0.2
    ) :
        alpha_fast_(2.0 / (fast_period + 1)),
        alpha_slow_(2.0 / (slow_period + 1)),
        alpha_signal_(2.0 / (signal_period + 1)),
        alpha_velocity_ema_(velocity_smooth_factor),
        count_(0),
        mean_(0.0),
        m2_(0.0)
    {
        reset();
    }

    void update(double value) {
        count_++;

        indicators = {
            std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::quiet_NaN()
        };

        // --- 计算统计指标 ---
        const double delta = value - mean_;
        mean_ += delta / count_;
        const double delta2 = value - mean_;
        m2_ += delta * delta2;
        if (count_ > 1) {
            indicators.variance = m2_ / (count_ - 1);
            indicators.std_dev = std::sqrt(indicators.variance);
        }

        // --- 更新EMA和计算MACD ---
        update_ema(ema_fast_prev_, value, alpha_fast_);
        update_ema(ema_slow_prev_, value, alpha_slow_);
        indicators.ema_fast = ema_fast_prev_;
        indicators.ema_slow = ema_slow_prev_;
        if (!std::isnan(ema_fast_prev_) && !std::isnan(ema_slow_prev_)) {
            indicators.macd_line = ema_fast_prev_ - ema_slow_prev_;
            update_ema(ema_signal_prev_, indicators.macd_line, alpha_signal_);
            if (!std::isnan(ema_signal_prev_)) {
                indicators.macd_signal = ema_signal_prev_;
                indicators.macd_hist = indicators.macd_line - indicators.macd_signal;
            }
        }

        // --- 计算运动学指标 ---
        if (!std::isnan(x_prev_)) {
            indicators.velocity = value - x_prev_;

            if (std::abs(x_prev_) > std::numeric_limits<double>::epsilon()) {
                indicators.roc = indicators.velocity / std::abs(x_prev_);
            } else {
                indicators.roc = 0.0;
            }
            update_ema(velocity_ema_prev_, indicators.velocity, alpha_velocity_ema_);
            indicators.smoothed_velocity = velocity_ema_prev_;
        }

        if (!std::isnan(x_prev_prev_)) {
            indicators.acceleration = value - 2 * x_prev_ + x_prev_prev_;
        }
        if (!std::isnan(x_prev_3_)) {
            indicators.jerk = value - 3 * x_prev_ + 3 * x_prev_prev_ - x_prev_3_;
        }
        if (!std::isnan(x_prev_4_)) {
            indicators.snap = value - 4 * x_prev_ + 6 * x_prev_prev_ - 4 * x_prev_3_ + x_prev_4_;
        }

        // --- 更新历史状态 ---
        x_prev_4_ = x_prev_3_;
        x_prev_3_ = x_prev_prev_;
        x_prev_prev_ = x_prev_;
        x_prev_ = value;
    }

    void reset() {
        count_ = 0;
        mean_ = 0.0;
        m2_ = 0.0;

        double nan_val = std::numeric_limits<double>::quiet_NaN();
        indicators = {nan_val, nan_val, nan_val, nan_val, nan_val, nan_val, nan_val,
                      nan_val, nan_val, nan_val, nan_val, nan_val, nan_val};
        ema_fast_prev_ = nan_val;
        ema_slow_prev_ = nan_val;
        ema_signal_prev_ = nan_val;
        velocity_ema_prev_ = nan_val;
        x_prev_ = nan_val;
        x_prev_prev_ = nan_val;
        x_prev_3_ = nan_val;
        x_prev_4_ = nan_val;
    }

private:
    void update_ema(double& ema_prev, double value, double alpha) {
        if (std::isnan(ema_prev)) {
            ema_prev = value;
        } else {
            ema_prev = alpha * value + (1.0 - alpha) * ema_prev;
        }
    }

    double alpha_fast_, alpha_slow_, alpha_signal_, alpha_velocity_ema_;
    long long count_;
    double mean_, m2_;
    double ema_fast_prev_, ema_slow_prev_, ema_signal_prev_, velocity_ema_prev_;
    double x_prev_, x_prev_prev_, x_prev_3_, x_prev_4_;
};

// ================== 新增：窗口滚动版本 ==================
#include <deque>

class WindowedStreamingTrend {
public:
    TrendIndicators indicators;

    explicit WindowedStreamingTrend(
        int window_size = 60,
        int fast_period = 12,
        int slow_period = 26,
        int signal_period = 9,
        double velocity_smooth_factor = 0.2
    ) :
        window_size_(window_size),
        alpha_fast_(2.0 / (fast_period + 1)),
        alpha_slow_(2.0 / (slow_period + 1)),
        alpha_signal_(2.0 / (signal_period + 1)),
        alpha_velocity_ema_(velocity_smooth_factor)
    {
        reset();
    }

    void update(double value) {
        // 滚动窗口
        if (window_.size() == window_size_) {
            double old = window_.front();
            window_.pop_front();
            // 移除旧值对均值和方差的影响
            if (count_ > 1) {
                double old_mean = mean_;
                mean_ = (mean_ * count_ - old) / (count_ - 1);
                m2_ -= (old - old_mean) * (old - mean_);
            }
            count_--;
        }
        window_.push_back(value);
        count_++;

        // 初始化所有指标为 NaN
        indicators = {
            std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::quiet_NaN()
        };

        // --- 计算窗口内均值和方差 ---
        const double delta = value - mean_;
        mean_ += delta / count_;
        const double delta2 = value - mean_;
        m2_ += delta * delta2;
        if (count_ > 1) {
            indicators.variance = m2_ / (count_ - 1);
            indicators.std_dev = std::sqrt(indicators.variance);
        }

        // --- EMA和MACD ---
        update_ema(ema_fast_prev_, value, alpha_fast_);
        update_ema(ema_slow_prev_, value, alpha_slow_);
        indicators.ema_fast = ema_fast_prev_;
        indicators.ema_slow = ema_slow_prev_;
        if (!std::isnan(ema_fast_prev_) && !std::isnan(ema_slow_prev_)) {
            indicators.macd_line = ema_fast_prev_ - ema_slow_prev_;
            update_ema(ema_signal_prev_, indicators.macd_line, alpha_signal_);
            if (!std::isnan(ema_signal_prev_)) {
                indicators.macd_signal = ema_signal_prev_;
                indicators.macd_hist = indicators.macd_line - indicators.macd_signal;
            }
        }

        // --- 运动学指标 ---
        if (!std::isnan(x_prev_)) {
            indicators.velocity = value - x_prev_;
            if (std::abs(x_prev_) > std::numeric_limits<double>::epsilon()) {
                indicators.roc = indicators.velocity / std::abs(x_prev_);
            } else {
                indicators.roc = 0.0;
            }
            update_ema(velocity_ema_prev_, indicators.velocity, alpha_velocity_ema_);
            indicators.smoothed_velocity = velocity_ema_prev_;
        }
        if (!std::isnan(x_prev_prev_)) {
            indicators.acceleration = value - 2 * x_prev_ + x_prev_prev_;
        }
        if (!std::isnan(x_prev_3_)) {
            indicators.jerk = value - 3 * x_prev_ + 3 * x_prev_prev_ - x_prev_3_;
        }
        if (!std::isnan(x_prev_4_)) {
            indicators.snap = value - 4 * x_prev_ + 6 * x_prev_prev_ - 4 * x_prev_3_ + x_prev_4_;
        }

        // --- 更新历史状态 ---
        x_prev_4_ = x_prev_3_;
        x_prev_3_ = x_prev_prev_;
        x_prev_prev_ = x_prev_;
        x_prev_ = value;
    }

    void reset() {
        window_.clear();
        count_ = 0;
        mean_ = 0.0;
        m2_ = 0.0;
        double nan_val = std::numeric_limits<double>::quiet_NaN();
        indicators = {nan_val, nan_val, nan_val, nan_val, nan_val, nan_val, nan_val,
                      nan_val, nan_val, nan_val, nan_val, nan_val, nan_val};
        ema_fast_prev_ = nan_val;
        ema_slow_prev_ = nan_val;
        ema_signal_prev_ = nan_val;
        velocity_ema_prev_ = nan_val;
        x_prev_ = nan_val;
        x_prev_prev_ = nan_val;
        x_prev_3_ = nan_val;
        x_prev_4_ = nan_val;
    }

private:
    void update_ema(double& ema_prev, double value, double alpha) {
        if (std::isnan(ema_prev)) {
            ema_prev = value;
        } else {
            ema_prev = alpha * value + (1.0 - alpha) * ema_prev;
        }
    }

    int window_size_;
    std::deque<double> window_;
    int count_;
    double mean_, m2_;
    double alpha_fast_, alpha_slow_, alpha_signal_, alpha_velocity_ema_;
    double ema_fast_prev_, ema_slow_prev_, ema_signal_prev_, velocity_ema_prev_;
    double x_prev_, x_prev_prev_, x_prev_3_, x_prev_4_;
};

}  // namespace base
}  // namespace tools
}  // namespace demofw00
}  // namespace factors
