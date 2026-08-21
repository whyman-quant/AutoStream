#pragma once

// 多因子集编排：同一资产下挂多个 FactorEntry，统一扇出行情并按因子集触发计算。
//
// 类职责划分（均在本头文件内）：
// - FactorEntryGroupBase   ：行情扇出 + TriggerCompute（只调 entry->UpdateFactors，不写结果缓冲）
// - FactorEntrySnapshotManager：组内紧凑 fvals_snapshot_，实现 IFactorEntry（单测/工具链）
// - FactorEntryRowWriter ：按 FactorSetColumnLayout 将各因子集散射写入外部 result_cache 行（引擎时序线程）
//
// 生产路径：时序线程用 RowWriter（kCompute 计算、kSend 写入）；截面线程仍直接写 result_cache 对应列区间。

#include <algorithm>
#include <cstring>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "factors/_comm/core.h"
#include "factors/_comm/factor_entry_base.h"
#include "factors/_comm/factor_entry_registry.h"

namespace factors {

template <typename Key, typename Value>
class InsertOrderMap {
public:
    using iterator = typename std::vector<std::pair<Key, Value>>::iterator;
    using const_iterator = typename std::vector<std::pair<Key, Value>>::const_iterator;

    // 插入键值对（保持插入顺序，重复键则覆盖）
    void insert(const Key& key, const Value& value) {
        auto it = index.find(key);
        if (it != index.end()) {
            // 键已存在：更新值
            data[it->second].second = value;
        } else {
            // 新键：插入到末尾
            data.emplace_back(key, value);
            index[key] = data.size() - 1;  // 记录索引
        }
    }

    void insert(const Key& key, Value&& value) {
        auto it = index.find(key);
        if (it != index.end()) {
            data[it->second].second = std::move(value);
        } else {
            data.emplace_back(key, std::move(value));
            index[key] = data.size() - 1;  // 记录索引
        }
    }

    // 按键查找（O(1)）
    Value& at(const Key& key) {
        auto it = index.find(key);
        if (unlikely(it == index.end()))
            throw std::out_of_range("Key not found");
        return data[it->second].second;
    }

    const Value& at(const Key& key) const {
        auto it = index.find(key);
        if (unlikely(it == index.end()))
            throw std::out_of_range("Key not found");
        return data[it->second].second;
    }

    Value& at(size_t pos) {
        if (unlikely(pos >= data.size()))
            throw std::out_of_range("Index out of range");
        return data[pos].second;
    }

    const Value& at(size_t pos) const {
        if (unlikely(pos >= data.size()))
            throw std::out_of_range("Index out of range");
        return data[pos].second;
    }

    // 删除键（O(n) 需移动元素）
    void erase(const Key& key) {
        auto it = index.find(key);
        if (unlikely(it == index.end())) return;

        size_t pos = it->second;
        data.erase(data.begin() + pos);
        index.erase(it);

        // 更新后续元素的索引
        for (size_t i = pos; i < data.size(); ++i) {
            index[data[i].first] = i;
        }
    }

    // 遍历接口（按插入顺序）
    const_iterator begin() const { return data.begin(); }
    const_iterator end() const { return data.end(); }
    iterator begin() { return data.begin(); }
    iterator end() { return data.end(); }

    size_t size() const { return data.size(); }
    bool empty() const { return data.empty(); }
    bool contains(const Key& key) const { return index.find(key) != index.end(); }

private:
    std::vector<std::pair<Key, Value>> data;  // 按插入顺序存储
    std::unordered_map<Key, size_t> index;      // 键 -> 索引映射
};

struct FactorEntryRecord {
    std::string name;
    comm::FactorEntryPtr entry;
    // 写入目标缓冲区的列起始下标：
    // - SnapshotManager：组内局部连续偏移（0, count1, count1+count2, ...）
    // - RowWriter：全局 result_cache 行内列下标（来自 FactorSetColumnLayout）
    size_t output_start;
    size_t factor_count;  // 该因子集的因子数量
    bool is_active;       // 是否仍接收行情（false 时 AddQuote/Trans/Order 跳过）
};

using FactorEntryRecords = InsertOrderMap<std::string, FactorEntryRecord>;

// 单个因子集在全局因子行中的列区间（由引擎按 config 顺序累加生成）
struct FactorSetLayout {
    size_t start = 0;  // 全局列起始下标（含时序/截面交错）
    size_t count = 0;  // 该因子集占用的列数

    FactorSetLayout() = default;
    FactorSetLayout(size_t start_idx, size_t factor_count) : start(start_idx), count(factor_count) {}
};

using FactorSetColumnLayout = std::unordered_map<std::string, FactorSetLayout>;

// 多因子集公共逻辑：行情扇出、按集触发计算；不实现 IFactorEntry，不负责结果行布局。
class FactorEntryGroupBase {
public:
    FactorEntryGroupBase() = default;
    FactorEntryGroupBase(const FactorEntryGroupBase&) = delete;
    FactorEntryGroupBase& operator=(const FactorEntryGroupBase&) = delete;
    FactorEntryGroupBase(FactorEntryGroupBase&&) = default;
    FactorEntryGroupBase& operator=(FactorEntryGroupBase&&) = default;
    virtual ~FactorEntryGroupBase() = default;

    const FactorEntryRecords& GetFactorRecords() const { return records_; }

    // 添加 Tick 数据（仅扇出到 is_active 的因子集）
    void AddQuote(const Stock_Internal_Book& quote) {
        for (const auto& pair : records_) {
            if (pair.second.is_active) {
                pair.second.entry->AddQuote(quote);
            }
        }
    }

    // 添加成交数据
    void AddTrans(const Stock_Transaction_Internal_Book_New& quote) {
        for (const auto& pair : records_) {
            if (pair.second.is_active) {
                pair.second.entry->AddTrans(quote);
            }
        }
    }

    // 添加订单数据
    void AddOrder(const Stock_Order_Internal_Book_New& quote) {
        for (const auto& pair : records_) {
            if (pair.second.is_active) {
                pair.second.entry->AddOrder(quote);
            }
        }
    }

    // 触发指定因子集的计算（只调 entry->UpdateFactors，结果留在各 entry 内部）
    void TriggerCompute(int64_t timestamp, const std::string& factor_set_name) {
        if (likely(HasFactor(factor_set_name))) {
            RunFactorSetCompute(timestamp, records_.at(factor_set_name));
        } else {
            std::cerr << "Error: Factor " << factor_set_name << " not found" << std::endl;
        }
    }

    void TriggerCompute(int64_t timestamp, const std::vector<std::string>& factor_set_names) {
        for (const auto& factor_set_name : factor_set_names) {
            if (likely(HasFactor(factor_set_name))) {
                RunFactorSetCompute(timestamp, records_.at(factor_set_name));
            } else {
                std::cerr << "Error: Factor " << factor_set_name << " not found" << std::endl;
            }
        }
    }

    void TriggerCompute(int64_t timestamp, size_t factor_set_index) {
        if (likely(factor_set_index < records_.size())) {
            RunFactorSetCompute(timestamp, records_.at(factor_set_index));
        } else {
            std::cerr << "Error: Factor set index " << factor_set_index << " not found" << std::endl;
        }
    }

    void TriggerCompute(int64_t timestamp, const std::vector<size_t>& factor_set_indexes) {
        for (const auto& factor_set_index : factor_set_indexes) {
            if (likely(factor_set_index < records_.size())) {
                RunFactorSetCompute(timestamp, records_.at(factor_set_index));
            } else {
                std::cerr << "Error: Factor set index " << factor_set_index << " not found" << std::endl;
            }
        }
    }

    // 触发本组内所有因子集的计算
    void TriggerCompute(int64_t timestamp) {
        for (auto& p : records_) {
            RunFactorSetCompute(timestamp, p.second);
        }
    }

    void OnGlobalTime(int exch_time) {
        for (const auto& pair : records_) {
            pair.second.entry->OnGlobalTime(exch_time);
        }
    }

    void AfterUpdateFactors(int64_t timestamp) {
        for (const auto& pair : records_) {
            pair.second.entry->AfterUpdateFactors(timestamp);
        }
    }

    bool HasFactor(const std::string& name) const { return records_.contains(name); }

    size_t GetFactorSetCount() const { return records_.size(); }

    // 标记某个因子集为不 active（不再接收新行情）
    void MarkFactorSetInactive(const std::string& factor_set_name) {
        if (likely(HasFactor(factor_set_name))) {
            records_.at(factor_set_name).is_active = false;
        }
    }

    void MarkFactorSetInactive(size_t factor_set_index) {
        if (likely(factor_set_index < records_.size())) {
            records_.at(factor_set_index).is_active = false;
        }
    }

    // 获取所有因子入口的指针（用于统计信息、线程树等）
    std::vector<comm::FactorEntryBase*> GetFactorEntryPtrs() const {
        std::vector<comm::FactorEntryBase*> result;
        result.reserve(records_.size());
        for (const auto& pair : records_) {
            result.push_back(pair.second.entry.get());
        }
        return result;
    }

    // 静态元数据查询（不实例化 entry）
    static size_t GetStaticRecordSize(const std::vector<std::string>& factor_entry_names) {
        return factor_entry_names.size();
    }

    static std::vector<std::string> GetStaticFactorNames(
        const std::vector<std::string>& factor_entry_names) {
        return comm::FactorEntryRegistry::GetInstance().GetStaticFactorNames(factor_entry_names);
    }

    static size_t GetStaticFactorSize(const std::vector<std::string>& factor_entry_names) {
        return comm::FactorEntryRegistry::GetInstance().GetStaticFactorSize(factor_entry_names);
    }

protected:
    FactorEntryRecords records_;  // 因子记录（InsertOrderMap，保持 factor_entry_names 插入顺序）

    // 单因子集计算：结果保存在 entry 内部，由 Snapshot/RowWriter 子类决定如何落盘
    void RunFactorSetCompute(int64_t timestamp, FactorEntryRecord& record) {
        record.entry->UpdateFactors(timestamp);
    }

    // 按 factor_entry_names 创建各 FactorEntry；output_start_fn 由子类提供列/快照偏移策略
    void CreateFactorEntries(const std::string& asset, const comm::FactorEntryConfig& config,
        const std::vector<std::string>& factor_entry_names,
        const std::function<size_t(const std::string& name, size_t factor_count)>& output_start_fn) {
        try {
            auto& registry = comm::FactorEntryRegistry::GetInstance();
            size_t failed_count = 0;

            for (const auto& name : factor_entry_names) {
                try {
                    if (auto entry = registry.Create(name, asset, config)) {
                        const size_t factor_count = entry->GetFactorSize();
                        // 验证 factor_count 的合理性
                        if (factor_count == 0) {
                            std::cerr << "Error: invalid factor count for " << name << ": 0"
                                      << std::endl;
                            ++failed_count;
                            continue;
                        }
                        const size_t output_start = output_start_fn(name, factor_count);
                        FactorEntryRecord record{name, std::move(entry), output_start, factor_count,
                                                 true};  // 初始化时所有因子集都是 active 的
                        records_.insert(name, std::move(record));
                    } else {
                        std::cerr << "Failed to create factor: " << name << std::endl;
                        ++failed_count;
                    }
                } catch (const std::exception& e) {
                    std::cerr << "Exception creating factor " << name << ": " << e.what() << std::endl;
                    ++failed_count;
                }
            }

            if (records_.empty()) {
                throw std::runtime_error("No factors were created successfully!");
            }

            if (failed_count > 0) {
                std::cerr << "Warning: " << failed_count << " out of " << factor_entry_names.size()
                          << " factors failed to create" << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "Fatal error in CreateFactorEntries: " << e.what() << std::endl;
            throw;
        }
    }
};

// 组内紧凑快照：各因子集在 fvals_snapshot_ 中连续排列；实现 IFactorEntry，供单测/工具链。
// 生产时序线程不使用此类，而用 FactorEntryRowWriter 直接写 result_cache。
class FactorEntrySnapshotManager : public IFactorEntry, public FactorEntryGroupBase {
public:
    FactorEntrySnapshotManager(const std::string& asset, const comm::FactorEntryConfig& config,
        const std::vector<std::string>& factor_entry_names) {
        Init(asset, config, factor_entry_names);
    }

    // 禁用拷贝构造和赋值操作
    FactorEntrySnapshotManager(const FactorEntrySnapshotManager&) = delete;
    FactorEntrySnapshotManager& operator=(const FactorEntrySnapshotManager&) = delete;

    // 允许移动构造和赋值操作
    FactorEntrySnapshotManager(FactorEntrySnapshotManager&&) = default;
    FactorEntrySnapshotManager& operator=(FactorEntrySnapshotManager&&) = default;

    // 更新指定因子集并返回快照值
    const std::vector<fval_t>& UpdateFactors(int64_t timestamp,
        const std::vector<std::string>& factor_set_names) {
        TriggerCompute(timestamp, factor_set_names);
        return fvals_snapshot_;
    }

    // 兼容原接口 - 更新所有因子并返回快照值
    const std::vector<fval_t>& UpdateFactors(int64_t timestamp) override {
        TriggerCompute(timestamp);
        return fvals_snapshot_;
    }

    const std::vector<fval_t>& GetFactorValues() const override { return fvals_snapshot_; }

    std::vector<fval_t> GetFactorValues(const std::string& factor_name) const {
        if (likely(HasFactor(factor_name))) {
            return records_.at(factor_name).entry->GetFactorValues();
        }
        return {};
    }

    std::vector<std::string> GetFactorNames() const override { return factor_names_; }

    size_t GetFactorSize() const override { return factor_size_; }

    // 以下 TriggerCompute 重载隐藏基类版本：计算后写入 fvals_snapshot_（而非仅留在 entry 内）
    void TriggerCompute(int64_t timestamp, const std::string& factor_set_name) {
        if (likely(HasFactor(factor_set_name))) {
            ComputeAndSave(timestamp, records_.at(factor_set_name));
        } else {
            std::cerr << "Error: Factor " << factor_set_name << " not found" << std::endl;
        }
    }

    void TriggerCompute(int64_t timestamp, const std::vector<std::string>& factor_set_names) {
        for (const auto& factor_set_name : factor_set_names) {
            TriggerCompute(timestamp, factor_set_name);
        }
    }

    void TriggerCompute(int64_t timestamp, size_t factor_set_index) {
        if (likely(factor_set_index < records_.size())) {
            ComputeAndSave(timestamp, records_.at(factor_set_index));
        } else {
            std::cerr << "Error: Factor set index " << factor_set_index << " not found" << std::endl;
        }
    }

    void TriggerCompute(int64_t timestamp, const std::vector<size_t>& factor_set_indexes) {
        for (const auto& factor_set_index : factor_set_indexes) {
            TriggerCompute(timestamp, factor_set_index);
        }
    }

    void TriggerCompute(int64_t timestamp) {
        for (auto& p : records_) {
            ComputeAndSave(timestamp, p.second);
        }
    }

private:
    std::vector<fval_t> fvals_snapshot_;     // 因子值快照（组内各集连续存放）
    std::vector<std::string> factor_names_;  // 展平后的因子列名
    size_t factor_size_ = 0;                 // 本组管理的因子总列数

    void Init(const std::string& asset, const comm::FactorEntryConfig& config,
        const std::vector<std::string>& factor_entry_names) {
        auto& registry = comm::FactorEntryRegistry::GetInstance();

        // 获取静态元数据
        auto static_info = registry.GetStaticFactorInfo(factor_entry_names);
        factor_size_ = static_info.factor_size;
        factor_names_ = std::move(static_info.factor_names);

        // 调整快照大小并初始化为 NaN
        fvals_snapshot_.resize(factor_size_, std::numeric_limits<fval_t>::quiet_NaN());

        // 创建因子记录：output_start 为组内局部连续偏移
        size_t local_offset = 0;
        CreateFactorEntries(asset, config, factor_entry_names,
            [&](const std::string&, size_t factor_count) {
                if (unlikely(local_offset + factor_count > factor_size_)) {
                    throw std::runtime_error("Factor set exceeds snapshot capacity");
                }
                const size_t start = local_offset;
                local_offset += factor_count;
                return start;
            });
    }

    // 更新因子值并保存到快照
    void ComputeAndSave(int64_t timestamp, FactorEntryRecord& record) {
        const auto& computed_vals = record.entry->UpdateFactors(timestamp);
        if (likely(computed_vals.size() == record.factor_count) &&
            likely(record.output_start + record.factor_count <= fvals_snapshot_.size())) {
            std::memcpy(fvals_snapshot_.data() + record.output_start, computed_vals.data(),
                record.factor_count * sizeof(fval_t));
        }
    }
};

// 按全局列布局散射写入外部 result_cache 行。
// 典型用法（时序线程）：kCompute 调 TriggerCompute 仅计算；kSend 调 Write*Into 写入行缓冲。
// 只写入本对象持有的因子集对应列区间，不覆盖截面等其他线程负责的列。
class FactorEntryRowWriter : public FactorEntryGroupBase {
public:
    FactorEntryRowWriter(const std::string& asset, const comm::FactorEntryConfig& config,
        const std::vector<std::string>& factor_entry_names,
        const FactorSetColumnLayout& column_layout, size_t row_factor_capacity)
        : row_factor_capacity_(row_factor_capacity) {
        CreateFactorEntries(asset, config, factor_entry_names,
            [&](const std::string& name, size_t) {
                const auto it = column_layout.find(name);
                if (unlikely(it == column_layout.end())) {
                    throw std::runtime_error("Factor set not found in column layout: " + name);
                }
                return it->second.start;
            });
    }

    FactorEntryRowWriter(const FactorEntryRowWriter&) = delete;
    FactorEntryRowWriter& operator=(const FactorEntryRowWriter&) = delete;
    FactorEntryRowWriter(FactorEntryRowWriter&&) = default;
    FactorEntryRowWriter& operator=(FactorEntryRowWriter&&) = default;

    size_t GetRowFactorCapacity() const { return row_factor_capacity_; }

    // 将单个因子集的计算结果写入 row[output_start : output_start+count)
    void WriteFactorSetInto(const std::string& factor_set_name, fval_t* row,
        size_t row_capacity) const {
        if (unlikely(row == nullptr)) {
            return;
        }
        if (unlikely(!HasFactor(factor_set_name))) {
            std::cerr << "Error: Factor " << factor_set_name << " not found" << std::endl;
            return;
        }
        const auto& record = records_.at(factor_set_name);
        const auto& computed_vals = record.entry->GetFactorValues();
        // 尺寸/越界时静默跳过：热路径避免抛错；正常路径在 TriggerCompute 后应与 factor_count 一致
        if (unlikely(computed_vals.size() != record.factor_count)) {
            return;
        }
        if (unlikely(record.output_start + record.factor_count > row_capacity)) {
            return;
        }
        std::memcpy(row + record.output_start, computed_vals.data(),
            record.factor_count * sizeof(fval_t));
    }

    void WriteFactorSetsInto(const std::vector<std::string>& factor_set_names, fval_t* row,
        size_t row_capacity) const {
        for (const auto& name : factor_set_names) {
            WriteFactorSetInto(name, row, row_capacity);
        }
    }

    // 写入本 RowWriter 持有的全部因子集（时序线程 kSend 路径）
    void WriteAllOwnedFactorSetsInto(fval_t* row, size_t row_capacity) const {
        for (const auto& pair : records_) {
            WriteFactorSetInto(pair.first, row, row_capacity);
        }
    }

private:
    size_t row_factor_capacity_ = 0;  // 单行因子区可容纳的列数（= 引擎 factor_size_）
};

// 向后兼容别名（原 FactorEntryManager 即现在的 SnapshotManager）
using FactorEntryManager = FactorEntrySnapshotManager;

}  // namespace factors
