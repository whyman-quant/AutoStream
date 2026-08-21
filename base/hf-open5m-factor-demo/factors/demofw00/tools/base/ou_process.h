#pragma once

#include <iostream>
#include <vector>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <numeric>
#include <cstdlib>
#include <ctime>

namespace factors {
namespace demofw00 {
namespace tools {
namespace base {

// --- 1. 基础数据结构 ---

// 用于RLS计算的简单2D向量
struct Vector2D {
    double v[2] = {0.0, 0.0};
};

// 用于RLS计算的简单2x2矩阵
struct Matrix2x2 {
    double m[2][2] = {{0.0, 0.0}, {0.0, 0.0}};
};

// 存储估计出的OU过程参数
struct OUParams {
    double curr = 0.0; // 最新值
    double theta = 0.0; // 回归速度
    double mu = 0.0;    // 长期均值
    double sigma = 0.0; // 波动率
};

// --- 2. 递归最小二乘(RLS)拟合器 ---
// 这个类实现了针对AR(1)模型 y_k = a*y_{k-1} + b + noise 的在线参数估计
class RLSFitter {
private:
    Vector2D phi;          // 参数向量 [a, b]^T
    Matrix2x2 P;           // 协方差矩阵
    double lambda;         // 参数遗忘因子
    double lambda_v;       // 方差遗忘因子
    double sigma_eps_sq;   // 新息（误差）的方差
    bool initialized;

public:
    RLSFitter(double forgetting_factor = 0.995, double variance_forgetting_factor = 0.995)
        : lambda(forgetting_factor), lambda_v(variance_forgetting_factor),
            sigma_eps_sq(0.0), initialized(false) {
        // 初始化参数为0
        phi.v[0] = 0.0; // a
        phi.v[1] = 0.0; // b
        // 初始化协方差矩阵为一个大值，表示初始不确定性高
        P.m[0][0] = 1000.0;
        P.m[1][1] = 1000.0;
    }

    // 核心更新函数
    void update(double current_y, double previous_y) {
        if (!initialized) {
            // 首次更新时，用第一个数据点来初始化部分参数
            phi.v[1] = current_y; // 将 b 的初始值设为第一个y值
            sigma_eps_sq = 1.0; // 假设初始误差方差为1
            initialized = true;
            return;
        }

        // --- RLS算法步骤 ---
        // 1. 构建回归量 H_k = [y_{k-1}, 1]^T
        Vector2D H;
        H.v[0] = previous_y;
        H.v[1] = 1.0;

        // 2. 计算预测误差 e_k = y_k - H_k^T * phi_{k-1}
        double y_pred = H.v[0] * phi.v[0] + H.v[1] * phi.v[1];
        double error = current_y - y_pred;

        // 中间计算: P * H
        Vector2D P_H;
        P_H.v[0] = P.m[0][0] * H.v[0] + P.m[0][1] * H.v[1];
        P_H.v[1] = P.m[1][0] * H.v[0] + P.m[1][1] * H.v[1];

        // 中间计算: H^T * P * H
        double H_P_H = H.v[0] * P_H.v[0] + H.v[1] * P_H.v[1];

        // 3. 计算增益向量 K_k
        Vector2D K;
        double denominator = lambda + H_P_H;
        if (std::abs(denominator) < 1e-9) denominator = 1e-9; // 避免除以零
        K.v[0] = P_H.v[0] / denominator;
        K.v[1] = P_H.v[1] / denominator;

        // 4. 更新参数 phi_k = phi_{k-1} + K_k * e_k
        phi.v[0] += K.v[0] * error;
        phi.v[1] += K.v[1] * error;

        // 5. 更新协方差矩阵 P_k = (I - K_k * H_k^T) * P_{k-1} / lambda
        Matrix2x2 K_HT;
        K_HT.m[0][0] = K.v[0] * H.v[0]; K_HT.m[0][1] = K.v[0] * H.v[1];
        K_HT.m[1][0] = K.v[1] * H.v[0]; K_HT.m[1][1] = K.v[1] * H.v[1];

        Matrix2x2 I_minus_KHT;
        I_minus_KHT.m[0][0] = 1.0 - K_HT.m[0][0]; I_minus_KHT.m[0][1] = -K_HT.m[0][1];
        I_minus_KHT.m[1][0] = -K_HT.m[1][0];     I_minus_KHT.m[1][1] = 1.0 - K_HT.m[1][1];

        Matrix2x2 P_next;
        P_next.m[0][0] = (I_minus_KHT.m[0][0] * P.m[0][0] + I_minus_KHT.m[0][1] * P.m[1][0]) / lambda;
        P_next.m[0][1] = (I_minus_KHT.m[0][0] * P.m[0][1] + I_minus_KHT.m[0][1] * P.m[1][1]) / lambda;
        P_next.m[1][0] = (I_minus_KHT.m[1][0] * P.m[0][0] + I_minus_KHT.m[1][1] * P.m[1][0]) / lambda;
        P_next.m[1][1] = (I_minus_KHT.m[1][0] * P.m[0][1] + I_minus_KHT.m[1][1] * P.m[1][1]) / lambda;
        P = P_next;

        // 6. 递归更新新息方差
        sigma_eps_sq = lambda_v * sigma_eps_sq + (1.0 - lambda_v) * error * error;
    }

    // 获取AR(1)参数
    Vector2D get_phi() const { return phi; }
    double get_sigma_epsilon_sq() const { return sigma_eps_sq; }
};

// --- 3. OU过程拟合器 ---
// 使用RLS拟合AR(1)过程，并将结果转换为OU过程参数
class OUProcessFitter {
private:
    RLSFitter rls_fitter;
    double delta_t; // 采样时间间隔 (单位：年)
    double last_value; // 上一个数据点的值

public:
    OUProcessFitter(double dt_in_years, double lambda = 0.995, double lambda_v = 0.995)
        : rls_fitter(lambda, lambda_v), delta_t(dt_in_years), last_value(0.0) {}

    void update(double current_x, double previous_x) {
        rls_fitter.update(current_x, previous_x);
        last_value = current_x;
    }

    OUParams get_ou_params() const {
        Vector2D phi = rls_fitter.get_phi();
        double a = phi.v[0];
        double b = phi.v[1];
        double sigma_eps_sq = rls_fitter.get_sigma_epsilon_sq();

        OUParams params;

        // --- 参数转换 ---
        // 为保证数值稳定性，对 a 进行约束
        if (a >= 1.0) a = 0.999999;
        if (a <= 0.0) a = 1e-9; // theta > 0 要求 a > 0

        // 计算 theta
        params.theta = -std::log(a) / delta_t;

        // 计算 mu
        if (std::abs(1.0 - a) < 1e-9) {
            params.mu = 0; // 避免除以零
        } else {
            params.mu = b / (1.0 - a);
        }

        // 计算 sigma
        double term1 = -2.0 * std::log(a) * sigma_eps_sq;
        double term2 = delta_t * (1.0 - a * a);
        if (term1 >= 0 && term2 > 1e-9) {
            params.sigma = std::sqrt(term1 / term2);
        } else {
            params.sigma = 0;
        }

        params.curr = last_value;
        return params;
    }
};



// --- 4. 通用的流式OU过程拟合器 ---
// 这个类封装了对任何单变量时间序列进行OU过程拟合的逻辑
class StreamingOUFitter {
private:
    OUProcessFitter fitter;
    double last_value;
    bool initialized;

public:
    // 构造函数
    // dt_seconds: 数据点之间的时间间隔（秒）
    // lambda: RLS的遗忘因子
    OUParams params;
    StreamingOUFitter(double dt_seconds, double lambda = 0.995, double lambda_v = 0.995)
        : fitter(
            // 将秒转换为年作为OU过程的时间单位
            dt_seconds / (252.0 * 6.5 * 3600.0),
            lambda,
            lambda_v
            ),
            last_value(0.0),
            initialized(false),
            params() {}

    // 用新的数据点更新拟合器
    void update(double current_value) {
        if (!initialized) {
            last_value = current_value;
            initialized = true;
            return;
        }
        fitter.update(current_value, last_value);
        last_value = current_value;
        params = fitter.get_ou_params();
    }
};

}  // namespace base
}  // namespace tools
}  // namespace demofw00
}  // namespace factors
