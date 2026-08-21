#pragma once

#include <iostream>
#include <cmath>
#include <Eigen/Dense>

namespace factors {
namespace demofw00 {
namespace tools {
namespace base {

/**
 * @struct GARCHParams
 * @brief 存储GARCH(1,1)模型的参数。
 *
 * GARCH(1,1)模型: σ_t² = ω + α * r_{t-1}² + β * σ_{t-1}²
 */
struct GARCHParams {
    double omega = 0.01;      // ω: 长期平均方差的常数项 (截距)
    double alpha = 0.1;       // α: ARCH项系数，衡量前期收益率波动对当前波动率的影响
    double beta = 0.85;       // β: GARCH项系数，衡量前期波动率对当前波动率的影响
    double conditional_variance = 0.0; // σ_t²: 当前的条件方差估计
};

/**
 * @class StreamingGARCHFitter
 * @brief 使用在线梯度上升法对GARCH(1,1)模型进行流式拟合。
 */
class StreamingGARCHFitter {
public:
    GARCHParams params;

private:
    double learning_rate_;      // 参数更新的学习率
    double prev_return_sq_;     // 上一期的收益率平方 (r_{t-1}²)
    double prev_variance_;      // 上一期的条件方差 (σ_{t-1}²)
    bool initialized_;

public:
    /**
     * @brief 构造函数
     * @param initial_params 初始的GARCH模型参数。
     * @param learning_rate 参数更新的学习率。
     */
    explicit StreamingGARCHFitter(
        const GARCHParams& initial_params = GARCHParams(),
        double learning_rate = 1e-6
    ) :
        params(initial_params),
        learning_rate_(learning_rate),
        prev_return_sq_(0.0),
        prev_variance_(0.0),
        initialized_(false)
    {
        // 初始方差设为长期无条件方差
        if (1.0 - params.alpha - params.beta > 1e-9) {
            prev_variance_ = params.omega / (1.0 - params.alpha - params.beta);
        } else {
            prev_variance_ = params.omega; // 避免除以零
        }
    }

    /**
     * @brief 使用新的收益率数据点更新模型。
     * @param return_value 资产的对数收益率或简单收益率。
     */
    void update(double return_value) {
        if (!initialized_) {
            prev_return_sq_ = return_value * return_value;
            initialized_ = true;
            return;
        }

        // 1. 计算当前条件方差 (σ_t²)
        double current_variance = params.omega + params.alpha * prev_return_sq_ + params.beta * prev_variance_;
        params.conditional_variance = current_variance;

        // 2. 计算新息 (innovation)
        double innovation = return_value * return_value - current_variance;

        // 3. 计算对数似然函数关于参数的梯度 (简化形式)
        // dL/d(param) ≈ (r_t² / σ_t⁴ - 1/σ_t²) * d(σ_t²)/d(param)
        double common_gradient_term = (innovation) / (current_variance * current_variance);

        double grad_omega = common_gradient_term * 1.0;
        double grad_alpha = common_gradient_term * prev_return_sq_;
        double grad_beta  = common_gradient_term * prev_variance_;

        // 4. 使用梯度上升更新参数
        params.omega += learning_rate_ * grad_omega;
        params.alpha += learning_rate_ * grad_alpha;
        params.beta  += learning_rate_ * grad_beta;

        // 5. 施加约束，确保模型平稳性
        project_parameters();

        // 6. 为下一次迭代更新状态
        prev_variance_ = current_variance;
        prev_return_sq_ = return_value * return_value;
    }

    /**
     * @brief 获取当前预测的条件方差。
     * @return double 下一期的条件方差 σ_t²。
     */
    double get_conditional_variance() const {
        return params.conditional_variance;
    }

private:
    /**
     * @brief 将参数投影回有效区域，以保证模型的稳定性。
     */
    void project_parameters() {
        // ω > 0
        if (params.omega < 1e-9) params.omega = 1e-9;

        // α >= 0
        if (params.alpha < 0) params.alpha = 0;

        // β >= 0
        if (params.beta < 0) params.beta = 0;

        // α + β < 1 (弱平稳条件)
        if (params.alpha + params.beta >= 1.0) {
            double sum = params.alpha + params.beta;
            params.alpha = (params.alpha / sum) * 0.9999;
            params.beta = (params.beta / sum) * 0.9999;
        }
    }
};

}  // namespace base
}  // namespace tools
}  // namespace demofw00
}  // namespace factors
