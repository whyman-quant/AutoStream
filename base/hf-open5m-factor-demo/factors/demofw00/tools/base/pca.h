#pragma once

#include <iostream>
#include <vector>
#include <Eigen/Dense>

namespace factors {
namespace demofw00 {
namespace tools {
namespace base {


/**
 * @class StreamingPCA
 * @brief 使用增量更新协方差矩阵的方法实现流式主成分分析（PCA）。
 *
 * 该类能够处理数据流，并在不存储所有历史数据的情况下，
 * 实时更新数据的主成分。
 */
class StreamingPCA {
private:
    int dimensions;         // 数据的维度
    long long count;        // 已处理的数据点数量
    Eigen::VectorXd mean;   // 数据的均值向量
    Eigen::MatrixXd cov;    // 数据的协方差矩阵
    Eigen::MatrixXd scatter; // 散布矩阵 (Cov * (n-1))，用于更稳定地更新

public:
    /**
     * @brief 构造函数。
     * @param dimensions 数据的维度。
     */
    StreamingPCA(int dims) : dimensions(dims), count(0) {
        if (dims <= 0) {
            throw std::invalid_argument("维度必须是正数。");
        }
        // 初始化向量和矩阵
        mean = Eigen::VectorXd::Zero(dims);
        cov = Eigen::MatrixXd::Zero(dims, dims);
        scatter = Eigen::MatrixXd::Zero(dims, dims);
    }

    /**
     * @brief 向模型中添加一个新的数据点并更新内部状态。
     * @param data_point 一个Eigen::VectorXd类型的p维数据点。
     */
    void add(const Eigen::VectorXd& data_point) {
        if (data_point.size() != dimensions) {
            throw std::invalid_argument("输入数据点的维度与模型维度不匹配。");
        }

        count++;

        // --- 增量更新均值和散布矩阵 ---
        // 这是基于Welford算法扩展的稳定更新方法
        Eigen::VectorXd old_mean = mean;
        mean = old_mean + (data_point - old_mean) / count;

        // 更新散布矩阵 S = S + (x - old_mean) * (x - new_mean)^T
        scatter += (data_point - old_mean) * (data_point - mean).transpose();
    }

    /**
     * @brief 计算并返回当前的主成分（特征向量）。
     * @param num_components 要返回的主成分数量。
     * @return 一个矩阵，其列是按特征值大小降序排列的主成分。
     */
    Eigen::MatrixXd get_principal_components(int num_components) const { // <-- Added const
        if (count < 2) {
            // std::cerr is a side-effect, best to remove from const functions
            return Eigen::MatrixXd::Zero(dimensions, num_components);
        }
        if (num_components <= 0 || num_components > dimensions) {
            throw std::invalid_argument("请求的主成分数量无效。");
        }

        // 从散布矩阵计算协方差矩阵到临时变量
        Eigen::MatrixXd temp_cov = scatter / (count - 1); // <-- Use local variable

        // 对协方差矩阵进行特征值分解
        Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> eigen_solver(temp_cov);

        if (eigen_solver.info() != Eigen::Success) {
            // std::cerr is a side-effect, best to remove from const functions
            return Eigen::MatrixXd::Zero(dimensions, num_components);
        }

        // 特征值和特征向量默认按升序排列，我们需要降序的
        Eigen::MatrixXd principal_components = eigen_solver.eigenvectors().rightCols(num_components).rowwise().reverse();

        return principal_components;
    }
    // --- ↓↓↓ 这是我们新增的 transform 函数 ↓↓↓ ---
    /**
     * @brief 将给定的数据点投影到主成分空间，实现降维或数据变换。
     * @param data_point 要进行变换的原始数据点。
     * @param num_components 要投影到的主成分数量（即新空间的维度）。
     * @return 一个大小为 num_components 的向量，包含数据点在新坐标系下的坐标。
     * @throw std::invalid_argument 如果数据点维度或请求的组件数量无效。
     */
    Eigen::VectorXd transform(const Eigen::VectorXd& data_point, int num_components) const {
        // --- 1. 参数验证 ---
        if (data_point.size() != dimensions) {
            throw std::invalid_argument("输入数据点的维度与模型维度不匹配。");
        }
        if (num_components <= 0 || num_components > dimensions) {
            throw std::invalid_argument("请求的主成分数量无效。");
        }
        if (count < 2) {
            return Eigen::VectorXd::Zero(num_components);
        }

        // --- 2. 获取主成分和均值 ---
        Eigen::MatrixXd principal_components = get_principal_components(num_components);

        // --- 3. 计算变换 ---
        // 首先，将数据点中心化
        Eigen::VectorXd centered_point = data_point - mean;

        // 然后，通过矩阵乘法进行投影
        // (num_components x dimensions) * (dimensions x 1) -> (num_components x 1)
        return principal_components.transpose() * centered_point;
    }

    /**
     * @brief 获取当前已处理的数据点总数。
     */
    long long get_count() const {
        return count;
    }

    /**
     * @brief 获取当前数据的均值向量。
     */
    Eigen::VectorXd get_mean() const {
        return mean;
    }
};

}  // namespace base
}  // namespace tools
}  // namespace demofw00
}  // namespace factors
