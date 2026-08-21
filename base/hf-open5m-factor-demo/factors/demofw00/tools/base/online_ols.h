#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "factors/demofw00/tools/base/ring_buffer.hpp"

namespace factors {
namespace demofw00 {
namespace tools {
namespace base {

// 在线最小二乘（单变量 x->y）
// 支持：
// - O(1) 增量更新 update(x,y)
// - O(1) 回退 pop(x,y)（用于窗口弹出）
// - 计算 slope/intercept/R^2/残差标准差
class OnlineOLS {
public:
    OnlineOLS(int min_n = 2) : min_n_(min_n) { reset(); }

    void reset() {
        n_ = 0;
        sum_x_ = sum_y_ = 0.0;
        sum_xx_ = sum_xy_ = sum_yy_ = 0.0;
    }

    void update(double x, double y) {
        ++n_;
        sum_x_ += x;
        sum_y_ += y;
        sum_xx_ += x * x;
        sum_xy_ += x * y;
        sum_yy_ += y * y;
    }

    void pop(double x, double y) {
        if (n_ <= 0) return;
        --n_;
        sum_x_ -= x;
        sum_y_ -= y;
        sum_xx_ -= x * x;
        sum_xy_ -= x * y;
        sum_yy_ -= y * y;
    }

    long long num() const { return n_; }

    double mean_x() const { return (n_ >= min_n_ ? sum_x_ / static_cast<double>(n_) : 0.0); }
    double mean_y() const { return (n_ >= min_n_ ? sum_y_ / static_cast<double>(n_) : 0.0); }

    double slope() const {
        if (n_ < min_n_) return 0.0;
        double nx = static_cast<double>(n_);
        double cov_xy = sum_xy_ - (sum_x_ * sum_y_) / nx;
        double var_x = sum_xx_ - (sum_x_ * sum_x_) / nx;
        if (std::abs(var_x) < 1e-12) return 0.0;
        return cov_xy / var_x;
    }

    double intercept() const {
        if (n_ < min_n_) return 0.0;
        return mean_y() - slope() * mean_x();
    }

    double r2() const {
        if (n_ < min_n_) return 0.0;
        double nx = static_cast<double>(n_);
        double cov_xy = sum_xy_ - (sum_x_ * sum_y_) / nx;
        double var_x = sum_xx_ - (sum_x_ * sum_x_) / nx;
        double var_y = sum_yy_ - (sum_y_ * sum_y_) / nx;
        if (var_x <= 1e-12 || var_y <= 1e-12) return 0.0;
        double corr2 = (cov_xy * cov_xy) / (var_x * var_y);
        return std::max(0.0, std::min(1.0, corr2));
    }

    // 回归是否就绪（样本充足 且 X/Y 方差均有效）
    bool is_ready(double eps = 1e-12) const {
        if (n_ < min_n_) return false;
        double nx = static_cast<double>(n_);
        double var_x = sum_xx_ - (sum_x_ * sum_x_) / nx;
        double var_y = sum_yy_ - (sum_y_ * sum_y_) / nx;
        return (std::abs(var_x) > eps && std::abs(var_y) > eps);
    }

    // 残差均方与标准差（基于 OLS 闭式形式）
    double residual_mse() const {
        if (n_ < min_n_) return 0.0;
        double nx = static_cast<double>(n_);
        double b1 = slope();
        double b0 = mean_y() - b1 * mean_x();
        // SSE = sum( (y - (b0+b1*x))^2 ) = Syy - 2*b0*Sy - 2*b1*Sxy + n*b0^2 + 2*b0*b1*Sx + b1^2*Sxx
        double sse = sum_yy_ - 2.0 * b0 * sum_y_ - 2.0 * b1 * sum_xy_ + nx * b0 * b0 + 2.0 * b0 * b1 * sum_x_ + b1 * b1 * sum_xx_;
        double dof = std::max(1.0, nx - 2.0);
        return std::max(0.0, sse / dof);
    }

    double residual_std() const { return std::sqrt(residual_mse()); }

private:
    int min_n_ = 2;
    long long n_ = 0;
    double sum_x_ = 0.0;
    double sum_y_ = 0.0;
    double sum_xx_ = 0.0;
    double sum_xy_ = 0.0;
    double sum_yy_ = 0.0;
};

// 时间窗口版 OLS：按 int_time（HHMMSSmmm）维护滑窗回归
struct XYMark {
    int mark;     // int_time
    double x;
    double y;
    size_t idx;
};

class WindowOnlineOLS {
public:
    // window_seconds: 时间窗口（毫秒）；max_events: 事件上限（防止环形缓冲区无限扩容）
    // 默认构造尽量小，防止大规模成员默认构造导致内存占用过大
    WindowOnlineOLS() : WindowOnlineOLS(60000, 256, 2) {}
    WindowOnlineOLS(int window_seconds, int max_events = 2000, int min_n = 2)
        : window_seconds_(window_seconds),
          max_events_(max_events),
          queue_(std::max(max_events * 2, 1024)),
          min_n_(min_n) {}

    void update(int int_time, double x, double y) {
        queue_.push_back(XYMark{int_time, x, y, back_idx_});
        ols_.update(x, y);
        ++size_;
        ++back_idx_;
        update_flag_ = true;

        // 事件容量约束：若超过上限，立即弹出最早元素（与 WindowBasicStats 一致的策略）
        while (static_cast<int>(size_) > max_events_) {
            const XYMark& front = queue_.front();
            ols_.pop(front.x, front.y);
            queue_.pop_front();
            if (size_ > 0) --size_;
            ++front_idx_;
        }
    }

    void compute() {
        if (!update_flag_ || queue_.empty()) return;
        int curr_mark = queue_.back().mark;
        compute(curr_mark);
    }

    void compute(int int_mark) {
        if (!update_flag_) return;
        while (!queue_.empty()) {
            bool cond_time = queue_.front().mark <= int_mark - window_seconds_;
            // 使用逻辑事件上限判断，而不是依赖 RingBuffer 的物理 capacity
            bool cond_cap = static_cast<int>(size_) > max_events_;
            if (!(cond_time || cond_cap)) break;
            const XYMark& front = queue_.front();
            ols_.pop(front.x, front.y);
            queue_.pop_front();
            if (size_ > 0) --size_;
            ++front_idx_;
        }
        update_flag_ = false;
    }

    long long num() const { return ols_.num(); }
    bool is_ready() const { maybe_compute(); return ols_.is_ready(); }
    double slope() const { maybe_compute(); return (ols_.is_ready() ? ols_.slope() : 0.0); }
    double intercept() const { maybe_compute(); return (ols_.is_ready() ? ols_.intercept() : 0.0); }
    double r2() const { maybe_compute(); return (ols_.is_ready() ? ols_.r2() : 0.0); }
    double residual_std() const { maybe_compute(); return (ols_.is_ready() ? ols_.residual_std() : 0.0); }

private:
    void maybe_compute() const {
        if (update_flag_) const_cast<WindowOnlineOLS*>(this)->compute();
    }

    int window_seconds_ = 60;
    int max_events_ = 2000;
    mutable bool update_flag_ = false;
    RingBuffer<XYMark> queue_;
    OnlineOLS ols_{};
    size_t size_ = 0;
    size_t front_idx_ = 0;
    size_t back_idx_ = 0;
    int min_n_ = 2;
};

}  // namespace base
}  // namespace tools
}  // namespace demofw00
}  // namespace factors
