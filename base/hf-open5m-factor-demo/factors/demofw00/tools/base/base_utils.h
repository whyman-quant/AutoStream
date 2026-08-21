#pragma once
#include <cmath>

// 模块内工具函数独立命名空间，避免多因子合并时的重复定义
namespace factors {
namespace demofw00 {
namespace tools {
namespace base {

// 安全的数值检查函数
inline bool is_valid_number(double value) {
    return !std::isnan(value) && !std::isinf(value) && std::isfinite(value);
}

// 安全的除法函数
static constexpr double epsilon = 1e-9;
inline double safe_divide(double numerator, double denominator,
                          double default_value = epsilon) {
    if (std::abs(denominator) < 1e-9) {
        denominator += default_value;
    }
    double result = numerator / denominator;
    return is_valid_number(result) ? result : default_value;
}

// 时间解码
inline int decode_time(int int_t) {
    int ms = int_t % 1000;
    int s = (int_t / 1000) % 100 * 1000;
    int m = (int_t / 100000) % 100 * 60000;
    int h = int_t / 10000000 * 3600000;
    return h + m + s + ms;
}

}  // namespace base
}  // namespace tools
}  // namespace demofw00
}  // namespace factors
