#pragma once

#include <cmath>
#include <cstddef>
#include <cstring>

namespace factors {
namespace demofw00 {
namespace tools {
namespace base {

struct DataStats
{
    double value;
    size_t idx;

    DataStats() {
        memset(this, 0, sizeof(DataStats));
    }

    DataStats(double _value, size_t _idx): value(_value), idx(_idx) {}
};



class BasicStats {
public:
    double sum;
    double square_sum;
    double cube_sum;
    double fourth_power_sum;
    // 新增：可对称维护的辅助量
    double abs_sum;          // |x| 的和
    double log_sum;          // log(x) 的和（仅 x>0 累加）
    double reciprocal_sum;   // 1/x 的和（仅 x!=0 累加）
    long long n;
    int min_n;
    int skip_n;
    int left_skip_n;
    long long positive_count;   // x>0 的数量（用于几何平均）
    long long nonzero_count;    // x!=0 的数量（用于调和平均）

    BasicStats() {
        memset(this, 0, sizeof(BasicStats));
    }

    BasicStats(int min_n, int skip_n): min_n(min_n), skip_n(skip_n), left_skip_n(skip_n)
    {
        reset();
    }

    void reset()
    {
        n = 0;
        sum = square_sum = cube_sum = fourth_power_sum = 0.0;
        abs_sum = log_sum = reciprocal_sum = 0.0;
        positive_count = nonzero_count = 0;
    }

    void set_skip(int skip) {
        left_skip_n = skip;
    }

    void add_skip(int skip) {
        left_skip_n += skip;
    }

    void update(double x) {
        if(left_skip_n > 0) {
            --left_skip_n;
            return;
        }
        double square = x * x;
        sum += x;
        square_sum += square;
        cube_sum += square * x;
        fourth_power_sum += square * square;
        // 对称可维护的额外量
        abs_sum += std::abs(x);
        if (x > 0) {
            log_sum += std::log(x);
            ++positive_count;
        }
        if (x != 0.0) {
            reciprocal_sum += 1.0 / x;
            ++nonzero_count;
        }
        ++n;
    }

    void pop(double x) {
        double square = x * x;
        sum -= x;
        square_sum -= square;
        cube_sum -= square * x;
        fourth_power_sum -= square * square;
        // 对称回退额外量
        abs_sum -= std::abs(x);
        if (x > 0) {
            log_sum -= std::log(x);
            --positive_count;
        }
        if (x != 0.0) {
            reciprocal_sum -= 1.0 / x;
            --nonzero_count;
        }
        --n;
    }

    long long num() const
    {
        return n;
    }

    double mean() const
    {
        if (n >= min_n) {
            return sum / n;
        }
        return 0.0; // Or some other appropriate value
    }

    double variance() const
    {
        double avg = mean();
        double var = (square_sum - 2 * avg * sum + n * avg * avg);
        if ((n >= min_n) && (var > 1e-9)){
            return var / (n-1.0);
        }
        return 0.0; // Or some other appropriate value
    }

    // 注意：统一以 std_dev() 为标准名称
    #if defined(__GNUC__) || defined(__clang__)
    __attribute__((deprecated("Use std_dev() instead")))
    #endif
    double std() const { return std_dev(); }


    double std_dev() const
    {
        if (n >= min_n){
            return sqrt(variance());
        }
        return 0.0; // Or some other appropriate value
    }


    double skew() const
    {
        double avg = mean();
        double cube = cube_sum - 3 * avg * square_sum + 3 * avg * avg * sum - n * std::pow(avg, 3);
        double variance = square_sum - 2 * avg * sum + n * avg * avg;
        if ((n >= min_n) && (std::abs(variance) > 1e-9) && (std::abs(variance) > 1e-9)){
            return cube / pow(variance, 1.5) * sqrt(n);
        }
        return 0.0; // Or some other appropriate value
    }

    double kurt() const
    {
        double avg = mean();
        double fourth = fourth_power_sum  - 4 * avg * cube_sum + 6 * avg * avg * square_sum - 4 * avg * avg * avg * sum + n * std::pow(avg, 4);
        double variance = square_sum - 2 * avg * sum + n * avg * avg;
        if ((n >= min_n) && (std::abs(variance) > 1e-9) && (std::abs(fourth) > 1e-9)){
            return fourth / pow(variance, 2) * n - 3;
        }
        return 0.0; // Or some other appropriate value
    }

    double jarque_bera() const
    {
        double s = skew();
        // 这里的 kurt() 返回超额峰度（excess kurtosis）
        double k_excess = kurt();
        if ((n >= min_n) && (std::abs(variance()) > 1e-9)){
            return (pow(s, 2.0) + pow(k_excess, 2.0) / 4.0) * n / 6.0;
        }
        return 0.0; // Or some other appropriate value
    }

    double bimodality() const
    {
        double s = skew();
        double k = kurt();
        if ((n >= min_n) && (std::abs(variance()) > 1e-9)){
            return (pow(s, 2.0) + 1.0) / (k - 3.0);
        }
        return 0.0; // Or some other appropriate value
    }

    double skew_kurt_ratio() const
    {
        double s = skew();
        double k = kurt();
        if ((n >= min_n) && (std::abs(variance()) > 1e-9)){
            return s / sqrt(k);
        }
        return 0.0; // Or some other appropriate value
    }

    // 新增：更多派生指标（均可在 pop 对称维护）
    double sum_squares() const {
        if (n >= min_n) return square_sum;
        return 0.0;
    }

    // 均方根
    double rms() const {
        if (n >= min_n) return std::sqrt(square_sum / static_cast<double>(n));
        return 0.0;
    }

    // 变异系数 Coefficient of Variation = std_dev / |mean|
    double cv() const {
        if (n >= min_n) {
            double m = mean();
            if (std::abs(m) > 1e-9) {
                return std_dev() / std::abs(m);
            }
        }
        return 0.0;
    }

    // 标准误 Standard Error = std_dev / sqrt(n)
    double se() const {
        if (n >= min_n) return std_dev() / std::sqrt(static_cast<double>(n));
        return 0.0;
    }

    // |x| 的平均
    double mean_abs() const {
        if (n >= min_n) return abs_sum / static_cast<double>(n);
        return 0.0;
    }

    // 几何平均（要求全部 x>0）
    double geometric_mean() const {
        if (n >= min_n && positive_count == n) {
            return std::exp(log_sum / static_cast<double>(n));
        }
        return 0.0;
    }

    // 调和平均（要求全部 x!=0）
    double harmonic_mean() const {
        if (n >= min_n && nonzero_count == n && std::abs(reciprocal_sum) > 1e-12) {
            return static_cast<double>(n) / reciprocal_sum;
        }
        return 0.0;
    }

};

}  // namespace base
}  // namespace tools
}  // namespace demofw00
}  // namespace factors
