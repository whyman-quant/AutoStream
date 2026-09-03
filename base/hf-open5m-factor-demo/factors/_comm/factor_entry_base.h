#pragma once

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "factors/_comm/core.h"

#ifdef ENABLE_TIME_STATS
#include "factors/_comm/timer.h"
#endif  // ENABLE_TIME_STATS

namespace factors {

#ifdef ENABLE_TIME_STATS
using Timer = timer::RdtscTimer;
#endif  // ENABLE_TIME_STATS

namespace comm {

struct FactorEntryConfig {
	// 预处理等 OpenMP 段允许使用的最大线程数（上界，默认 16）。
	// <=0 表示不施加该上界、按各模型原逻辑（环境变量等）解析。
	int omp_num_threads{16};
	// 日期
	std::string date;
	// 新版本，ev_path直接赋值，因子内部访问对应文件夹
	std::string ev_path;
	// 兼容旧版本，保留ev_paths字段，但不推荐使用
	std::unordered_map<std::string, std::string> ev_paths;
	// 股票代码列表，仅当因子为截面因子时有效，否则不会处理该字段
	std::vector<std::string> asset_codes;
};

// 因子静态元数据结构
struct FactorMetadata {
	std::string factor_set_name;
	size_t factor_size;
	std::vector<std::string> factor_names;
	bool is_cross_sectional;  // 是否为截面因子

	// 默认构造函数：is_cross_sectional默认为false
	FactorMetadata(const std::string& name, size_t size, const std::vector<std::string>& names)
	    : factor_set_name(name), factor_size(size), factor_names(names), is_cross_sectional(false) {}

	// 4参数构造函数：显式指定is_cross_sectional
	FactorMetadata(const std::string& name, size_t size, const std::vector<std::string>& names, bool is_cs)
	    : factor_set_name(name), factor_size(size), factor_names(names), is_cross_sectional(is_cs) {}
};

namespace {
inline bool Is6DigitNumber(const std::string& s) {
	if (s.size() != 6) {
		return false;
	}
	for (char c : s) {
		if (!std::isdigit(static_cast<unsigned char>(c))) {
			return false;
		}
	}
	return true;
}
}  // namespace

class FactorEntryBase : public IFactorEntry {
public:
	FactorEntryBase(
	    const std::string& asset, const FactorMetadata& metadata, const FactorEntryConfig& config = FactorEntryConfig())
	    : asset_(asset.substr(0, 6)),
	      config_(config),
	      factor_size_(metadata.factor_size),
	      factor_names_(metadata.factor_names),
	      factor_set_name_(metadata.factor_set_name),
	      is_cross_sectional_(metadata.is_cross_sectional) {
		if (is_cross_sectional_) {
			// 截面因子
			// 先验证asset_codes是否有效
			if (config.asset_codes.empty()) {
				throw std::invalid_argument(
				    "asset_codes cannot be empty for cross-sectional factor set: " + factor_set_name_);
			}
			// 验证asset_codes中的股票代码是否有效
			for (const auto& asset_code : config.asset_codes) {
				// 检查股票代码是否为6位数字
				if (!Is6DigitNumber(asset_code)) {
					throw std::invalid_argument(
					    "asset must be 6 numbers, got: " + asset_code + " for factor set: " + factor_set_name_);
				}
			}
			asset_codes_ = config.asset_codes;
			total_factor_size_ = factor_size_ * asset_codes_.size();
		} else {
			// 非截面因子
			// 检查asset是否为6位数字
			if (!Is6DigitNumber(asset_)) {
				throw std::invalid_argument(
				    "asset must be 6 numbers, got: " + asset_ + " for factor set: " + factor_set_name_);
			}
			asset_codes_ = {asset_};
			total_factor_size_ = factor_size_;
		}
		fvals_.resize(total_factor_size_, 0.0);
#ifdef ENABLE_TIME_STATS
		// 预热计时器，避免后续因子延迟：Timer::GetScaler() 第一次调用会有一定开销。
		// 比如 RdtscTimer::GetScaler() 内部会调用
		// get_cpu_mhz()，这个操作需要执行系统调用和一定计算，会导致第一次计时器使用时出现明显延迟。
		// 因此在构造函数主动调用一次 Timer::GetScaler()，提前完成初始化和缓存。
		// 保证后续所有与因子延迟统计相关的 get_cpu_mhz 操作都是读取缓存，计时不被首次初始化影响。
		// 这里只需调用，不关心返回值，强转 void 消除未使用警告。
		(void)Timer::GetScaler();
#endif  // ENABLE_TIME_STATS
	}

	virtual ~FactorEntryBase() = default;

	void AddQuote(const Stock_Internal_Book& quote) override {
#ifdef ENABLE_TIME_STATS
		timer::ScopedTiming<Timer> quote_timer(quote_time_stats_);
#endif  // ENABLE_TIME_STATS
		DoOnAddQuote(quote);
	}

	void AddTrans(const Stock_Transaction_Internal_Book_New& quote) override {
#ifdef ENABLE_TIME_STATS
		timer::ScopedTiming<Timer> trans_timer(trans_time_stats_);
#endif
		DoOnAddTrans(quote);
	}

	void AddOrder(const Stock_Order_Internal_Book_New& quote) override {
#ifdef ENABLE_TIME_STATS
		timer::ScopedTiming<Timer> order_timer(order_time_stats_);
#endif  // ENABLE_TIME_STATS
		DoOnAddOrder(quote);
	}

	const std::vector<fval_t>& UpdateFactors(int64_t timestamp) override {
#ifdef ENABLE_TIME_STATS
		timer::ScopedTiming<Timer> update_timer(update_time_stats_);
#endif  // ENABLE_TIME_STATS
		DoOnUpdateFactors(timestamp);
		return fvals_;
	}

	void OnGlobalTime(int exch_time) override {
#ifdef ENABLE_TIME_STATS
		timer::ScopedTiming<Timer> global_time_timer(global_time_stats_);
#endif  // ENABLE_TIME_STATS
		DoOnGlobalTime(exch_time);
	}

	void AfterUpdateFactors(int64_t timestamp) override {
#ifdef ENABLE_TIME_STATS
		timer::ScopedTiming<Timer> after_update_timer(after_update_time_stats_);
#endif  // ENABLE_TIME_STATS
		DoOnAfterUpdateFactors(timestamp);
	}

	const std::vector<fval_t>& GetFactorValues() const override { return fvals_; }
	// Optional event-level validity channel.  Numeric factor values may remain
	// finite for backwards-compatible writers, while consumers use this mask to
	// distinguish an unsupported/not-ready value from a measured zero.
	virtual std::vector<bool> GetReadinessMask(int64_t timestamp) const {
		(void)timestamp;
		return std::vector<bool>(factor_size_, true);
	}
	std::vector<std::string> GetFactorNames() const override { return factor_names_; }
	std::string GetAsset() const { return asset_; }
	FactorEntryConfig GetConfig() const { return config_; }
	size_t GetFactorSize() const override { return factor_size_; }
	size_t GetTotalFactorSize() const { return total_factor_size_; }
	std::string GetFactorSetName() const { return factor_set_name_; }
	bool IsCrossSectional() const { return is_cross_sectional_; }
	const std::vector<std::string>& GetAssetCodes() const { return asset_codes_; }

	// 返回该因子实例内部额外创建的辅助线程名称树（depth 相对本实例）。
	// 默认无子线程，返回空；子类如有内部 worker 线程可覆写。
	virtual std::vector<std::pair<int, std::string>> CollectRuntimeThreadTreeLines() const { return {}; }

#ifdef ENABLE_TIME_STATS
	const timer::ElapsedTimeStats& GetQuoteTimeStats() const { return quote_time_stats_; }
	const timer::ElapsedTimeStats& GetTransTimeStats() const { return trans_time_stats_; }
	const timer::ElapsedTimeStats& GetOrderTimeStats() const { return order_time_stats_; }
	const timer::ElapsedTimeStats& GetUpdateTimeStats() const { return update_time_stats_; }
	const timer::ElapsedTimeStats& GetGlobalTimeStats() const { return global_time_stats_; }
	const timer::ElapsedTimeStats& GetAfterUpdateTimeStats() const { return after_update_time_stats_; }
#endif  // ENABLE_TIME_STATS

protected:
	std::vector<fval_t> fvals_;

private:
	std::string asset_;
	FactorEntryConfig config_;

	size_t factor_size_ = 0;
	size_t total_factor_size_ = 0;
	std::vector<std::string> factor_names_;
	std::string factor_set_name_;
	bool is_cross_sectional_ = false;
	std::vector<std::string> asset_codes_;

#ifdef ENABLE_TIME_STATS
	timer::ElapsedTimeStats quote_time_stats_;
	timer::ElapsedTimeStats trans_time_stats_;
	timer::ElapsedTimeStats order_time_stats_;
	timer::ElapsedTimeStats update_time_stats_;
	timer::ElapsedTimeStats global_time_stats_;
	timer::ElapsedTimeStats after_update_time_stats_;
#endif  // ENABLE_TIME_STATS

	virtual void DoOnAddQuote(const Stock_Internal_Book& quote) = 0;
	virtual void DoOnAddTrans(const Stock_Transaction_Internal_Book_New& quote) = 0;
	virtual void DoOnAddOrder(const Stock_Order_Internal_Book_New& quote) = 0;
	virtual void DoOnUpdateFactors(int64_t timestamp) = 0;
	// DoOnGlobalTime 是可选的，子类可以不实现，如果不实现，将使用基类的默认空实现
	virtual void DoOnGlobalTime(int exch_time) { (void)exch_time; }
	// DoOnAfterUpdateFactors 是可选的，子类可以不实现，如果不实现，将使用基类的默认空实现
	virtual void DoOnAfterUpdateFactors(int64_t timestamp) { (void)timestamp; }
};

using FactorEntryPtr = std::unique_ptr<FactorEntryBase>;

namespace {
inline std::string Zfill(const std::string& s, size_t width, char fill_char = '0') {
	return (width <= s.size()) ? s : std::string(width - s.size(), fill_char) + s;
}
}  // namespace

// 生成因子名称
inline std::vector<std::string> GenerateFactorNames(
    const std::string& factor_set_name, size_t factor_size, std::vector<std::string> factor_names = {}) {
	// 保存原始名称的数量
	size_t original_size = factor_names.size();

	// 预分配足够空间
	factor_names.resize(factor_size);

	// 只为缺失的部分生成名称
	for (size_t i = original_size; i < factor_size; ++i) {
		factor_names[i] = factor_set_name + "_F" + Zfill(std::to_string(i), 3);
	}

	return factor_names;
}

}  // namespace comm
}  // namespace factors
