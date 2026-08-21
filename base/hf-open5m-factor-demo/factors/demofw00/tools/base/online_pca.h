#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <random>
#include <vector>

namespace factors {
namespace demofw00 {
namespace tools {
namespace base {

// 通用在线 PCA（n 维），基于 Welford 增量协方差更新：
// n_t = n_{t-1}+1
// delta  = x - mean_{t-1}
// mean_t = mean_{t-1} + delta / n_t
// S_t    = S_{t-1} + delta * (x - mean_t)^T    // S 是未归一的协方差累加器
// 协方差矩阵 Cov = S / (n-1)
// 提供第一主成分（power iteration）。不支持回退（滑窗场景建议重算或保留缓冲近似）。
class OnlinePCA {
public:
    explicit OnlinePCA(size_t dim, int min_n = 2)
        : dim_(dim), min_n_(min_n) {
        reset();
    }

    void reset() {
        n_ = 0;
        mean_.assign(dim_, 0.0);
        S_.assign(dim_ * dim_, 0.0);
    }

    void update(const std::vector<double>& x) {
        if (x.size() != dim_) return;
        ++n_;
        // delta = x - mean
        tmp_delta_.resize(dim_);
        for (size_t i = 0; i < dim_; ++i) tmp_delta_[i] = x[i] - mean_[i];
        // mean += delta / n
        for (size_t i = 0; i < dim_; ++i) mean_[i] += tmp_delta_[i] / static_cast<double>(n_);
        // delta2 = x - mean (更新后的)
        tmp_delta2_.resize(dim_);
        for (size_t i = 0; i < dim_; ++i) tmp_delta2_[i] = x[i] - mean_[i];
        // S += outer(delta, delta2)
        for (size_t r = 0; r < dim_; ++r) {
            const double dr = tmp_delta_[r];
            double* Sr = &S_[r * dim_];
            for (size_t c = 0; c < dim_; ++c) {
                Sr[c] += dr * tmp_delta2_[c];
            }
        }
    }

    long long num() const { return n_; }
    size_t dim() const { return dim_; }

    // 复制协方差矩阵（可选），大小 dim x dim
    void covariance(std::vector<double>& cov) const {
        cov = S_;
        if (n_ > 1) {
            const double scale = 1.0 / static_cast<double>(n_ - 1);
            for (double& v : cov) v *= scale;
        } else {
            std::fill(cov.begin(), cov.end(), 0.0);
        }
    }

    // 计算第一主成分方向 v（单位向量）与方差 var1（特征值），使用 power iteration
    // max_iters 与 tol 可调。若提供 init_v 作为初始向量，可加速收敛。
    void principal_component(std::vector<double>& v, double& var1,
                             int max_iters = 30, double tol = 1e-6,
                             const std::vector<double>* init_v = nullptr) const {
        v.assign(dim_, 0.0);
        var1 = 0.0;
        if (n_ < min_n_ || dim_ == 0) return;

        // 获取协方差矩阵副本（或直接在 S 基础上按比例使用）
        const double scale = (n_ > 1) ? (1.0 / static_cast<double>(n_ - 1)) : 0.0;

        // 初始化向量
        if (init_v && init_v->size() == dim_) {
            v = *init_v;
        } else {
            // 用单位向量或随机小扰动
            v[0] = 1.0;
        }
        normalize_inplace(v);

        std::vector<double> Av(dim_, 0.0);
        double prev_lambda = 0.0;
        for (int it = 0; it < max_iters; ++it) {
            // Av = Cov * v = (S/(n-1)) * v
            std::fill(Av.begin(), Av.end(), 0.0);
            for (size_t r = 0; r < dim_; ++r) {
                const double* Sr = &S_[r * dim_];
                double acc = 0.0;
                for (size_t c = 0; c < dim_; ++c) acc += Sr[c] * v[c];
                Av[r] = acc * scale;
            }
            // Rayleigh quotient 作为特征值近似
            double lambda = dot(v, Av);
            // 归一化 Av -> v
            v = Av;
            normalize_inplace(v);
            if (std::abs(lambda - prev_lambda) <= tol * std::max(1.0, std::abs(prev_lambda))) {
                prev_lambda = lambda;
                break;
            }
            prev_lambda = lambda;
        }
        var1 = prev_lambda;
    }

private:
    static double dot(const std::vector<double>& a, const std::vector<double>& b) {
        double s = 0.0;
        for (size_t i = 0; i < a.size(); ++i) s += a[i] * b[i];
        return s;
    }

    static void normalize_inplace(std::vector<double>& v) {
        double n = std::sqrt(dot(v, v));
        if (n > 1e-12) {
            for (double& x : v) x /= n;
        } else {
            // 退化时重置为 e1
            std::fill(v.begin(), v.end(), 0.0);
            if (!v.empty()) v[0] = 1.0;
        }
    }

    size_t dim_ = 0;
    int min_n_ = 2;
    long long n_ = 0;
    std::vector<double> mean_;
    std::vector<double> S_;            // dim x dim, 行主序
    mutable std::vector<double> tmp_delta_;
    mutable std::vector<double> tmp_delta2_;
};

// 兼容的二维便捷封装：内部委托给通用 OnlinePCA(dim=2)
class OnlinePCA2D {
public:
    OnlinePCA2D(int min_n = 2) : core_(2, min_n) {}
    void reset() { core_.reset(); }
    void update(double x, double y) { core_.update(std::vector<double>{x, y}); }
    long long num() const { return core_.num(); }
    void principal_component(double& vx, double& vy, double& var1) const {
        std::vector<double> v;
        core_.principal_component(v, var1);
        vx = (v.size() > 0 ? v[0] : 0.0);
        vy = (v.size() > 1 ? v[1] : 0.0);
    }
private:
    OnlinePCA core_;
};

}  // namespace base
}  // namespace tools
}  // namespace demofw00
}  // namespace factors
