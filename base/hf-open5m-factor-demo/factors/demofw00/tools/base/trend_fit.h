#pragma once

#include <cmath>
#include "factors/demofw00/tools/base/online_ols.h"

namespace factors {
namespace demofw00 {
namespace tools {
namespace base {

// 趋势拟合（以时间为自变量），把 int_time 映射为秒并做窗口 OLS
class TrendFit {
public:
    struct Snapshot {
        long long n = 0;
        double slope = 0.0;         // 价格/秒
        double intercept = 0.0;
        double r2 = 0.0;
        double residual_std = 0.0;  // 残差标准差
        double slope_z = 0.0;       // 斜率标准化（用残差std近似归一）
    };

    explicit TrendFit(int window_seconds, int min_n = 5)
        : window_seconds_(window_seconds),
          min_n_(min_n),
          ols_(window_seconds, 200000, min_n) {}

    static double to_seconds(int int_time) {
        int hour = int_time / 10000000;
        int minute = (int_time / 100000) % 100;
        int second = (int_time / 1000) % 100;
        int millisecond = int_time % 1000;
        return hour * 3600.0 + minute * 60.0 + second + millisecond / 1000.0;
    }

    void update(int int_time, double price) {
        ols_.update(int_time, int_time, price);
    }

    Snapshot get() const {
        Snapshot s;
        s.n = ols_.num();
        if (s.n < min_n_) return s;
        s.slope = ols_.slope();
        s.intercept = ols_.intercept();
        s.r2 = ols_.r2();
        s.residual_std = ols_.residual_std();
        s.slope_z = (s.residual_std > 1e-12) ? (s.slope / s.residual_std) : 0.0;
        return s;
    }

private:
    int window_seconds_;
    int min_n_;
    WindowOnlineOLS ols_;
};

}  // namespace base
}  // namespace tools
}  // namespace demofw00
}  // namespace factors
