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
#include <vector>

#include "factors/_comm/factor_entry_base.h"

#ifdef ENABLE_TIME_STATS
#include "factors/_comm/timer.h"
#endif // ENABLE_TIME_STATS

namespace factors {

#ifdef ENABLE_TIME_STATS

struct TimeStats {
	timer::ElapsedTimeStats quote, trans, order, update, after_update, global_time;
	std::string type;  // "ts" 或 "cs"
};

inline std::unordered_map<std::string, TimeStats> GetTimeStatsMap(const std::vector<comm::FactorEntryBase*>& factor_entry_ptrs) {
	std::unordered_map<std::string, TimeStats> stats_map;
	for (const auto& entry_ptr : factor_entry_ptrs) {
		if (!entry_ptr)
			continue; // 跳过空指针
		std::string factor_name = entry_ptr->GetFactorSetName();
		auto &stats = stats_map[factor_name];
		// 设置类型（如果还未设置）
		if (stats.type.empty()) {
			stats.type = entry_ptr->IsCrossSectional() ? "cs" : "ts";
		}

		stats.quote.Merge(entry_ptr->GetQuoteTimeStats());
		stats.trans.Merge(entry_ptr->GetTransTimeStats());
		stats.order.Merge(entry_ptr->GetOrderTimeStats());
		stats.update.Merge(entry_ptr->GetUpdateTimeStats());
		stats.after_update.Merge(entry_ptr->GetAfterUpdateTimeStats());
		stats.global_time.Merge(entry_ptr->GetGlobalTimeStats());
	}
	return stats_map;
}

inline std::vector<std::string> GetTimeStatsInfo(const std::unordered_map<std::string, TimeStats> &stats_map) {
	std::vector<std::string> lines;
	lines.reserve(stats_map.size() * 8 + 20);

	auto format_stat_line = [](const std::string &label, const timer::ElapsedTimeStats &stat) {
		std::ostringstream oss;
		oss << std::left << std::setw(16) << label
			<< std::left << std::setw(9) << " avg(μs):"
			<< std::right << std::setw(12) << std::fixed << std::setprecision(3) << stat.GetElapsedAvg()
			<< std::right << std::setw(12) << "max(μs):"
			<< std::right << std::setw(12) << std::fixed << std::setprecision(3) << stat.GetElapsedMax()
			<< std::right << std::setw(12) << "count:"
			<< std::right << std::setw(12) << stat.GetCount()
			<< std::right << std::setw(12) << "sum(μs):"
			<< std::right << std::setw(18) << std::fixed << std::setprecision(3) << stat.GetElapsedSum();
		return oss.str();
	};

	// 输出每个因子的统计
	for (const auto &pair : stats_map) {
		std::ostringstream oss;
		oss << pair.first << " [" << pair.second.type << "]";
		lines.push_back(oss.str());
		const auto &stats = pair.second;
		lines.push_back(format_stat_line("quote", stats.quote));
		lines.push_back(format_stat_line("trans", stats.trans));
		lines.push_back(format_stat_line("order", stats.order));
		lines.push_back(format_stat_line("factor", stats.update));
		if (stats.after_update.GetCount() > 0) {
			lines.push_back(format_stat_line("after_update", stats.after_update));
		}
		if (stats.global_time.GetCount() > 0) {
			lines.push_back(format_stat_line("global_time", stats.global_time));
		}
		lines.emplace_back(""); // 对应原有的空行
	}

	// 汇总所有因子的各种操作的统计
	lines.emplace_back("----------------------------------------------------------------------------------------------------");
	lines.emplace_back("Summary of all factors:");
	timer::ElapsedTimeStats total_quote, total_trans, total_order, total_update, total_after_update, total_global_time;

	for (const auto &pair : stats_map) {
		const auto &stats = pair.second;
		total_quote.Summarize(stats.quote);
		total_trans.Summarize(stats.trans);
		total_order.Summarize(stats.order);
		total_update.Summarize(stats.update);
		if (stats.after_update.GetCount() > 0) {
			total_after_update.Summarize(stats.after_update);
		}
		if (stats.global_time.GetCount() > 0) {
			total_global_time.Summarize(stats.global_time);
		}
	}

	lines.push_back(format_stat_line("quote", total_quote));
	lines.push_back(format_stat_line("trans", total_trans));
	lines.push_back(format_stat_line("order", total_order));
	lines.push_back(format_stat_line("factor", total_update));
	if (total_after_update.GetCount() > 0) {
		lines.push_back(format_stat_line("after_update", total_after_update));
	}
	if (total_global_time.GetCount() > 0) {
		lines.push_back(format_stat_line("global_time", total_global_time));
	}

	// 汇总所有因子所有操作的统计
	timer::ElapsedTimeStats total_all;
	total_all.Merge(total_quote);
	total_all.Merge(total_trans);
	total_all.Merge(total_order);
	total_all.Merge(total_update);
	if (total_after_update.GetCount() > 0) {
		total_all.Merge(total_after_update);
	}
	if (total_global_time.GetCount() > 0) {
		total_all.Merge(total_global_time);
	}
	lines.push_back(format_stat_line("total", total_all));
	lines.emplace_back("");

	return lines;
}

inline void SaveTimeStatsToCsvFile(const std::unordered_map<std::string, TimeStats> &stats_map, const std::string& save_dir = "./") {
	std::string save_file_path = save_dir + "/factor_time_stats.csv";
	std::ofstream ofs(save_file_path);
	if (!ofs.is_open()) {
		std::cerr << "Error: Failed to open " + save_file_path + " for writing." << std::endl;
		return;
	}

	// 检查 after_update 和 global_time 的 count 是否全为 0
	size_t after_update_total_count = 0;
	size_t global_time_total_count = 0;
	for (const auto &p : stats_map) {
		after_update_total_count += p.second.after_update.count;
		global_time_total_count += p.second.global_time.count;
	}
	bool has_after_update = (after_update_total_count > 0);
	bool has_global_time = (global_time_total_count > 0);

	// 输出表头
	ofs << "Factor,Type,"
		<< "Quote_Sum,Quote_Max,Quote_Count,"
		<< "Trans_Sum,Trans_Max,Trans_Count,"
		<< "Order_Sum,Order_Max,Order_Count,"
		<< "Factor_Sum,Factor_Max,Factor_Count";
	if (has_after_update) {
		ofs << ",AfterUpdate_Sum,AfterUpdate_Max,AfterUpdate_Count";
	}
	if (has_global_time) {
		ofs << ",GlobalTime_Sum,GlobalTime_Max,GlobalTime_Count";
	}
	ofs << std::endl;

	// 输出数据行
	for (const auto &p : stats_map) {
		const auto &stats = p.second;
		ofs << p.first << "," << stats.type << ","
			<< stats.quote.GetElapsedSum() << "," << stats.quote.GetElapsedMax() << "," << stats.quote.GetCount() << ","
			<< stats.trans.GetElapsedSum() << "," << stats.trans.GetElapsedMax() << "," << stats.trans.GetCount() << ","
			<< stats.order.GetElapsedSum() << "," << stats.order.GetElapsedMax() << "," << stats.order.GetCount() << ","
			<< stats.update.GetElapsedSum() << "," << stats.update.GetElapsedMax() << "," << stats.update.GetCount();
		if (has_after_update) {
			ofs << "," << stats.after_update.GetElapsedSum() << "," << stats.after_update.GetElapsedMax() << "," << stats.after_update.GetCount();
		}
		if (has_global_time) {
			ofs << "," << stats.global_time.GetElapsedSum() << "," << stats.global_time.GetElapsedMax() << "," << stats.global_time.GetCount();
		}
		ofs << std::endl;
	}
	ofs.close();
	std::cout << "Factor time stats saved to " + save_file_path << std::endl;
}

inline void SaveTimeStatsToCsvFileV2(const std::unordered_map<std::string, TimeStats> &stats_map, const std::string& save_dir = "./") {
	std::string save_file_path = save_dir + "/factor_time_stats_v2.csv";
	std::ofstream ofs(save_file_path);
	if (!ofs.is_open()) {
		std::cerr << "Error: Failed to open " + save_file_path + " for writing." << std::endl;
		return;
	}

	// 检查 after_update 和 global_time 的 count 是否全为 0
	size_t after_update_total_count = 0;
	size_t global_time_total_count = 0;
	for (const auto &p : stats_map) {
		after_update_total_count += p.second.after_update.count;
		global_time_total_count += p.second.global_time.count;
	}
	bool has_after_update = (after_update_total_count > 0);
	bool has_global_time = (global_time_total_count > 0);

	// 输出表头
	ofs << "Factor,Type,Operation,Avg(us),Max(us),Count,Sum(us)" << std::endl;

	// 输出数据行
	for (const auto &p : stats_map) {
		const auto &stats = p.second;
		ofs << p.first << "," << stats.type << "," << "quote" << ","
			<< stats.quote.GetElapsedAvg() << "," << stats.quote.GetElapsedMax() << ","
			<< stats.quote.GetCount() << "," << stats.quote.GetElapsedSum() << std::endl;
		ofs << p.first << "," << stats.type << "," << "trans" << ","
			<< stats.trans.GetElapsedAvg() << "," << stats.trans.GetElapsedMax() << ","
			<< stats.trans.GetCount() << "," << stats.trans.GetElapsedSum() << std::endl;
		ofs << p.first << "," << stats.type << "," << "order" << ","
			<< stats.order.GetElapsedAvg() << "," << stats.order.GetElapsedMax() << ","
			<< stats.order.GetCount() << "," << stats.order.GetElapsedSum() << std::endl;
		ofs << p.first << "," << stats.type << "," << "factor" << ","
			<< stats.update.GetElapsedAvg() << "," << stats.update.GetElapsedMax() << ","
			<< stats.update.GetCount() << "," << stats.update.GetElapsedSum() << std::endl;
		if (has_after_update) {
			ofs << p.first << "," << stats.type << "," << "after_update" << ","
				<< stats.after_update.GetElapsedAvg() << "," << stats.after_update.GetElapsedMax() << ","
				<< stats.after_update.GetCount() << "," << stats.after_update.GetElapsedSum() << std::endl;
		}
		if (has_global_time) {
			ofs << p.first << "," << stats.type << "," << "global_time" << ","
				<< stats.global_time.GetElapsedAvg() << "," << stats.global_time.GetElapsedMax() << ","
				<< stats.global_time.GetCount() << "," << stats.global_time.GetElapsedSum() << std::endl;
		}
	}
	ofs.close();
	std::cout << "Factor time stats saved to " + save_file_path << std::endl;
}

inline std::vector<std::string> GetTimeStatsInfo(const std::vector<comm::FactorEntryBase*>& factor_entry_ptrs, bool save_stats = false, const std::string& save_dir = "./") {
	auto stats_map = GetTimeStatsMap(factor_entry_ptrs);
	if (save_stats) {
		SaveTimeStatsToCsvFile(stats_map, save_dir);
		SaveTimeStatsToCsvFileV2(stats_map, save_dir);
	}
	return GetTimeStatsInfo(stats_map);
}

inline void PrintTimeStats(const std::vector<comm::FactorEntryBase*>& factor_entry_ptrs, bool save_stats = false, const std::string& save_dir = "./") {
	auto stats_map = GetTimeStatsMap(factor_entry_ptrs);
	auto output_lines = GetTimeStatsInfo(stats_map);
	for (const auto &line : output_lines) {
		std::cout << line << std::endl;
	}
	if (save_stats) {
		SaveTimeStatsToCsvFile(stats_map, save_dir);
		SaveTimeStatsToCsvFileV2(stats_map, save_dir);
	}
}

#else

// 这是函数的空实现，用于占位，参数只写类型和默认值可避免未使用参数警告，属于常见合法用法。
inline std::vector<std::string> GetTimeStatsInfo(const std::vector<comm::FactorEntryBase*>&, bool=false, const std::string& = std::string()) {
	return { "ENABLE_TIME_STATS is not defined" };
}

// 这是函数的空实现，用于占位，参数只写类型和默认值可避免未使用参数警告，属于常见合法用法。
inline void PrintTimeStats(const std::vector<comm::FactorEntryBase*>&, bool=false, const std::string& = std::string()) {
	std::cout << "ENABLE_TIME_STATS is not defined" << std::endl;
}

#endif

} // namespace factors