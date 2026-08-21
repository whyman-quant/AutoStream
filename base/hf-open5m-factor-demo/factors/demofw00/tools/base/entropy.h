#pragma once

#include <iostream>
#include <unordered_map>
#include <string>
#include <vector>
#include <cmath> // For log2

namespace factors {
namespace demofw00 {
namespace tools {
namespace base {

// 使用模板，使该类可以用于任何可哈希的数据类型 (string, int, etc.)
template <typename T>
class StreamingEntropy {
private:
    // 使用哈希表 (unordered_map) 来高效存储唯一元素及其计数
    std::unordered_map<T, int> counts;
    // 记录接收到的元素总数
    int total_count;

public:
    /**
     * @brief 构造函数，初始化一个空的熵计算器
     */
    StreamingEntropy() : total_count(0) {}

    /**
     * @brief 向数据流中添加一个新元素。
     * @param item 到达数据流的新元素。
     */
    void add(const T& item) {
        counts[item]++;      // 如果 item 不存在，会自动创建并初始化为0，然后+1
        total_count++;
    }

    /**
     * @brief 计算并返回当前数据流的香农熵。
     * @return double 当前的熵值（以比特为单位）。如果流为空，返回0.0。
     */
    double get_entropy() const {
        if (total_count == 0) {
            return 0.0;
        }

        double entropy = 0.0;
        // 遍历哈希表中的每一个键值对
        for (const auto& pair : counts) {
            int count = pair.second;
            // 计算该元素的概率
            double probability = static_cast<double>(count) / total_count;
            if (probability > 0) {
                // 根据公式 H = -Σ p * log2(p) 累加
                entropy -= probability * std::log2(probability);
            }
        }
        return entropy;
    }

    /**
     * @brief 打印当前内部状态，用于调试。
     */
    void print_state() const {
        std::cout << "  元素计数: { ";
        for (const auto& pair : counts) {
            std::cout << "'" << pair.first << "': " << pair.second << " ";
        }
        std::cout << "}" << std::endl;
        std::cout << "  总数: " << total_count << std::endl;
    }
};

}  // namespace base
}  // namespace tools
}  // namespace demofw00
}  // namespace factors
