#pragma once

#include "sdp_handler/quote_format_define.h"

#include "factors/demofw00/tools/base/window_basic_stats.h"
#include <functional>
#include <algorithm>
#include <cmath>

namespace factors {
namespace demofw00 {
namespace tools {
namespace market {

// 独立于 OFI 的大单判定器：仅依据"全体事件"的体量参考分布做在线动态阈值判定
// 使用约定：先读取参考分布快照计算阈值与判定，再将当前事件写入参考窗口，避免前视
class BigEventDetector {
public:

    struct Decision {
        bool is_big;                      // 是否为大单
        double threshold;                   // 使用的阈值
        double zscore;                      // z 分数（截断后）
        double ref_mean;                    // 参考分布均值
        double ref_std;                     // 参考分布标准差
        bool is_ready;                    // 参考分布是否有效
        int ref_count;                        // 参考分布样本数
        bool fallback_used;               // 是否使用了绝对回退阈值

        Decision() : is_big(false), threshold(0.0), zscore(0.0), ref_mean(0.0),
                     ref_std(0.0), is_ready(false), ref_count(0), fallback_used(false) {}
    };

private:
    // 配置参数
    double k_;                          // z 门限系数
    int min_n_;                         // 参考分布最小样本数
    double eps_;                        // 数值下限
    bool require_marketable_;           // 仅对 order 判定生效
    bool require_pricedist_zero_;       // 仅对 order 判定生效
    double fallback_abs_threshold_;     // 绝对回退阈值（<=0 表示禁用）
    double z_clip_;                     // z 分数截断上限

    std::reference_wrapper<base::WindowBasicStats> ref_window_;  // 参考分布窗口（外部传入）

public:
    // 构造函数：传入外部维护的参考分布窗口和基础参数
    BigEventDetector(base::WindowBasicStats& ref_window,
                     double k = 2.0,
                     int min_n = 30,
                     double eps = 1e-12,
                     bool require_marketable = false,
                     bool require_pricedist_zero = false,
                     double fallback_abs_threshold = 0.0,
                     double z_clip = 8.0)
        : k_(k), min_n_(min_n), eps_(eps),
          require_marketable_(require_marketable),
          require_pricedist_zero_(require_pricedist_zero),
          fallback_abs_threshold_(fallback_abs_threshold),
          z_clip_(z_clip),
          ref_window_(ref_window) {}

    // 更新配置
    void update_config(double k, int min_n, double eps, bool require_marketable,
                       bool require_pricedist_zero, double fallback_abs_threshold, double z_clip) {
        k_ = k; min_n_ = min_n; eps_ = eps;
        require_marketable_ = require_marketable;
        require_pricedist_zero_ = require_pricedist_zero;
        fallback_abs_threshold_ = fallback_abs_threshold;
        z_clip_ = z_clip;
    }

    // 事件窗判定并更新（无前视版本）
    Decision decide_and_update_count(int event_idx, double vol) {
        // 1. 快照参考分布统计
        Decision decision = compute_decision(vol);

        // 2. 更新参考窗口（严格在判定之后）
        ref_window_.get().update(event_idx, vol);

        return decision;
    }

    // 时间窗判定并更新（无前视版本）
    Decision decide_and_update_time(int64_t t_ms, double vol) {
        // 1. 快照参考分布统计
        Decision decision = compute_decision(vol);

        // 2. 更新参考窗口（严格在判定之后）
        ref_window_.get().update(t_ms, vol);

        return decision;
    }

    // 委托专用判定并更新（事件窗版本）
    Decision decide_order_and_update_count(int event_idx, double vol, bool marketable,
                                         int pricedist_ticks) {
        // 1. 快照参考分布统计
        Decision decision = compute_decision(vol);

        // 2. 应用委托特定约束
        if (require_marketable_ && !marketable) {
            decision.is_big = false;
        }
        if (require_pricedist_zero_ && pricedist_ticks != 0) {
            decision.is_big = false;
        }

        // 3. 更新参考窗口（严格在判定之后）
        ref_window_.get().update(event_idx, vol);

        return decision;
    }

    // 委托专用判定并更新（时间窗版本）
    Decision decide_order_and_update_time(int64_t t_ms, double vol, bool marketable,
                                        int pricedist_ticks) {
        // 1. 快照参考分布统计
        Decision decision = compute_decision(vol);

        // 2. 应用委托特定约束
        if (require_marketable_ && !marketable) {
            decision.is_big = false;
        }
        if (require_pricedist_zero_ && pricedist_ticks != 0) {
            decision.is_big = false;
        }

        // 3. 更新参考窗口（严格在判定之后）
        ref_window_.get().update(t_ms, vol);

        return decision;
    }

    // 仅判定（不更新窗口）
    Decision decide_only(double vol) const {
        return compute_decision(vol);
    }

    // 仅判定委托（不更新窗口）
    Decision decide_order_only(double vol, bool marketable, int pricedist_ticks) const {
        Decision decision = compute_decision(vol);

        // 应用委托特定约束
        if (require_marketable_ && !marketable) {
            decision.is_big = false;
        }
        if (require_pricedist_zero_ && pricedist_ticks != 0) {
            decision.is_big = false;
        }

        return decision;
    }

    // 获取参考窗口状态
    bool is_ready() const {
        return ref_window_.get().num() >= min_n_;
    }

    // 获取参考窗口样本数
    int ref_count() const {
        return ref_window_.get().num();
    }

private:
    // 核心判定逻辑（基于当前参考分布快照）
    Decision compute_decision(double vol) const {
        Decision decision;

        // 获取参考分布快照
        auto& ref = ref_window_.get();
        decision.ref_count = ref.num();
        decision.is_ready = decision.ref_count >= min_n_;

        if (!decision.is_ready) {
            // 样本不足，不判定为大单
            return decision;
        }

        decision.ref_mean = ref.mean();
        decision.ref_std = ref.std();

        // 检查分布有效性
        if (decision.ref_std <= eps_) {
            // 标准差过小，使用绝对回退阈值或拒绝判定
            if (fallback_abs_threshold_ > 0.0) {
                decision.threshold = fallback_abs_threshold_;
                decision.fallback_used = true;
                decision.is_big = vol >= decision.threshold;
                decision.zscore = 0.0;  // 无法计算有效 z 分数
            }
            return decision;
        }

        // 计算动态阈值
        decision.threshold = decision.ref_mean + k_ * decision.ref_std;

        // 计算 z 分数（截断）
        double raw_zscore = (vol - decision.ref_mean) / decision.ref_std;
        decision.zscore = std::max(-z_clip_, std::min(z_clip_, raw_zscore));

        // 判定是否为大单
        decision.is_big = vol >= decision.threshold;

        return decision;
    }
};

}  // namespace market
}  // namespace tools
}  // namespace demofw00
}  // namespace factors
