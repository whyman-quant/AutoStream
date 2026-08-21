#pragma once

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "factors/_comm/core.h"
#include "factors/_comm/factor_entry_base.h"

namespace factors {
namespace comm {

// 向前声明
template <typename T>
class FactoryCreator;

class FactorEntryRegistry {
public:
    using Creator = std::function<FactorEntryPtr(
        const std::string&, const FactorMetadata&, const FactorEntryConfig&)>;
    // 元数据获取函数类型
    using MetadataGetter = std::function<const FactorMetadata&()>;
    // 静态初始化函数类型
    using StaticInitializer = std::function<void(const FactorEntryConfig&)>;

    // 静态因子信息结构体
    struct StaticFactorInfo {
        size_t record_size;
        size_t factor_size;
        std::vector<std::string> factor_names;
    };

    static FactorEntryRegistry& GetInstance() {
        static FactorEntryRegistry instance;
        return instance;
    }

    // 删除拷贝和移动操作
    FactorEntryRegistry(const FactorEntryRegistry&) = delete;
    FactorEntryRegistry& operator=(const FactorEntryRegistry&) = delete;
    FactorEntryRegistry(FactorEntryRegistry&&) = delete;
    FactorEntryRegistry& operator=(FactorEntryRegistry&&) = delete;

    // 注册创建器
    void RegisterCreator(const std::string& name, Creator creator) {
        creators_[name] = std::move(creator);
    }

    // 注册元数据获取器
    void RegisterMetadata(const std::string& name, MetadataGetter getter) {
        metadata_getters_[name] = std::move(getter);
    }

    // 注册静态初始化器
    void RegisterStaticInitializer(const std::string& name,
                                   StaticInitializer initializer) {
        static_initializers_[name] = std::move(initializer);
    }

    // 清空注册表
    void Clear() {
        creators_.clear();
        metadata_getters_.clear();
        static_initializers_.clear();
    }

    FactorEntryPtr Create(
        const std::string& name, const std::string& asset,
        const FactorEntryConfig& config = FactorEntryConfig()) const {
        auto it = creators_.find(name);
        auto meta_it = metadata_getters_.find(name);

        if (it == creators_.end()) {
            std::cerr << "Error: Factor creator not found for: " << name
                      << std::endl;
            return nullptr;
        }

        if (meta_it == metadata_getters_.end()) {
            std::cerr << "Error: Metadata not found for factor: " << name
                      << std::endl;
            return nullptr;
        }

        // 获取元数据并传递给创建器
        const FactorMetadata& metadata = meta_it->second();
        return it->second(asset, metadata, config);
    }

    // 获取指定名称的因子元数据
    const FactorMetadata* GetMetadata(const std::string& name) const {
        auto it = metadata_getters_.find(name);
        if (it != metadata_getters_.end()) {
            return &(it->second());
        }
        return nullptr;
    }

    // 执行指定因子列表的静态初始化
    void RunStaticInitializers(const std::vector<std::string>& factor_names,
                               const FactorEntryConfig& config) const {
        std::cout << "-------- Running Static Initializers --------"
                  << std::endl;
        for (const auto& name : factor_names) {
            auto it = static_initializers_.find(name);
            if (it != static_initializers_.end()) {
                std::cout << "Initializing factor: " << name << std::endl;
                try {
                    it->second(config);
                    std::cout << "Successfully initialized factor: " << name
                              << std::endl;
                } catch (const std::exception& e) {
                    std::cerr << "Error initializing factor " << name << ": "
                              << e.what() << std::endl;
                }
            } else {
                std::cout << "No static initializer for factor: " << name
                          << std::endl;
            }
        }
        std::cout << "---------------------------------------------"
                  << std::endl;
    }

    // 主要方法：获取静态因子信息
    StaticFactorInfo GetStaticFactorInfo(
        const std::vector<std::string>& factor_names) const {
        StaticFactorInfo info;
        info.record_size = factor_names.size();
        info.factor_size = 0;

        // 预估容量
        size_t total_size = 0;
        for (const auto& name : factor_names) {
            const auto* metadata = GetMetadata(name);
            if (metadata) {
                total_size += metadata->factor_size;
            }
        }
        info.factor_names.reserve(total_size);

        // 收集数据
        for (const auto& name : factor_names) {
            const auto* metadata = GetMetadata(name);
            if (metadata) {
                info.factor_size += metadata->factor_size;
                const auto& fnames = metadata->factor_names;
                info.factor_names.insert(info.factor_names.end(),
                                         fnames.begin(), fnames.end());
            }
        }

        return info;
    }

    size_t GetStaticRecordSize(
        const std::vector<std::string>& factor_names) const {
        return GetStaticFactorInfo(factor_names).record_size;
    }

    std::vector<std::string> GetStaticFactorNames(
        const std::vector<std::string>& factor_names) const {
        return GetStaticFactorInfo(factor_names).factor_names;
    }

    size_t GetStaticFactorSize(
        const std::vector<std::string>& factor_names) const {
        return GetStaticFactorInfo(factor_names).factor_size;
    }

    bool IsCrossSectional(const std::string& factor_set_name) const {
        const auto* metadata = GetMetadata(factor_set_name);
        if (metadata) {
            return metadata->is_cross_sectional;
        }
        return false;
    }

    std::vector<std::string> GetAllNames() const {
        std::vector<std::string> names;
        names.reserve(creators_.size());
        for (const auto& creator : creators_) {
            names.push_back(creator.first);
        }
        return names;
    }

    // 允许访问所有工厂创建器的友元类模板
    template <typename T>
    friend class FactoryCreator;

private:
    FactorEntryRegistry() = default;

    // 按照添加顺序存储创建器
    std::unordered_map<std::string, Creator> creators_;
    // 存储元数据获取器
    std::unordered_map<std::string, MetadataGetter> metadata_getters_;
    // 存储静态初始化器
    std::unordered_map<std::string, StaticInitializer> static_initializers_;
};

// 工厂创建器模板类
template <typename T>
class FactoryCreator {
public:
    static void Register(const std::string& name) {
        FactorEntryRegistry::GetInstance().RegisterCreator(
            name, [](const std::string& asset, const FactorMetadata& metadata,
                     const FactorEntryConfig& config) {
                // 直接使用registry提供的metadata创建实例
                return factors::make_unique<T>(asset, metadata, config);
            });
    }
};

// 元数据注册器
class MetadataCreator {
public:
    static void Register(const std::string& name,
                         std::function<const FactorMetadata&()> getter) {
        FactorEntryRegistry::GetInstance().RegisterMetadata(name, getter);
    }
};

// 静态初始化注册器
class StaticInitializerCreator {
public:
    static void Register(
        const std::string& name,
        std::function<void(const FactorEntryConfig&)> initializer) {
        FactorEntryRegistry::GetInstance().RegisterStaticInitializer(
            name, initializer);
    }
};

// 自动注册辅助类
class AutoRegister {
public:
    using RegisterFunction = std::function<void()>;

    AutoRegister(RegisterFunction func) { func(); }
};

// 显示所有因子
void DisplayRegisteredFactors();

// 与 DisplayRegisteredFactors 输出一致的各行文本（便于组装 JSON 等）
std::vector<std::string> GetRegisteredFactorsInfo();

}  // namespace comm
}  // namespace factors

// 简洁的注册宏，使用命名空间名作为因子集名
#define REGISTER_FACTOR_AUTO(ns_name, class_name)                     \
    namespace factors {                                               \
    namespace ns_name {                                               \
    inline void RegisterFactorAuto() {                                \
        comm::FactoryCreator<class_name>::Register(kFactorSetName);   \
        comm::MetadataCreator::Register(kFactorSetName, GetMetadata); \
    }                                                                 \
    namespace {                                                       \
    __attribute__((used)) static comm::AutoRegister                   \
        auto_register_auto_##ns_name(RegisterFactorAuto);             \
    }                                                                 \
    }                                                                 \
    }

// 带静态初始化的注册宏
#define REGISTER_FACTOR_WITH_STATIC_INIT(ns_name, class_name)                 \
    namespace factors {                                                       \
    namespace ns_name {                                                       \
    inline void RegisterFactorWithStaticInit() {                              \
        comm::FactoryCreator<class_name>::Register(kFactorSetName);           \
        comm::MetadataCreator::Register(kFactorSetName, GetMetadata);         \
        comm::StaticInitializerCreator::Register(kFactorSetName, StaticInit); \
    }                                                                         \
    namespace {                                                               \
    __attribute__((used)) static comm::AutoRegister                           \
        auto_register_with_static_init_##ns_name(                             \
            RegisterFactorWithStaticInit);                                    \
    }                                                                         \
    }                                                                         \
    }

// End of registration macros.
