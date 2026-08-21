#pragma once

#include <chrono>
#include <iostream>
#include <unordered_map>
#include <vector>

#include "comm/raw_data_reader/merge_sort.h"
#include "comm/raw_data_reader/ticks_data.h"
#include "factor_calculation_engine.h"
#include "sdp_handler/quote_format_define.h"

// Tick 数据类型（金融行情原始快照）
using TickRawData = my_book_stock;
// Trans 数据类型（逐笔成交）
using TransRawData = my_book_stock_transaction_new;
// Order 数据类型（逐笔委托）
using OrderRawData = my_book_stock_order_new;
// Tick 数据容器
using TickVec = std::vector<TickRawData>;
// Trans 数据容器
using TransVec = std::vector<TransRawData>;
// Order 数据容器
using OrderVec = std::vector<OrderRawData>;

// DataLoader：加载、排序、合并并分发金融行情数据。
//
// 示例：
//   DataLoader data_loader;
//   data_loader.LoadRawData(20241008, "all");
//   data_loader.MergeAndSortData();
//   data_loader.SetCalculationEngine(calc_engine);
//   data_loader.DistributeData();
class DataLoader {
public:
	// 默认构造函数
	DataLoader() = default;

	// 加载原始行情数据。
	// 参数 date：交易日日期（格式 YYYYMMDD）。
	// 参数 asset：资产或资产池代码（"all" 表示全部股票，"all_other" 表示全部可转债）。
	// 返回值：加载耗时（秒）。
	//
	// 从数据源加载三类行情：
	// 1. 高频快照 Tick；
	// 2. 逐笔成交 Trans；
	// 3. 逐笔委托 Order。
	double LoadRawData(int date, const std::string& asset);

	// 合并所有行情数据并按本地时间戳排序。
	// 返回值：排序合并耗时（秒）。
	double MergeAndSortData();

	// 设置计算引擎。
	// 参数 calc_engine_instance：计算引擎实例。
	void SetCalculationEngine(FactorCalculationEngine& calc_engine_instance);

	// 按数据类型将行情分发到计算引擎。
	void DistributeData();

private:
	// 混合排序后的数据容器（合并排序后的数据结构）
	std::vector<merge_sort::quote_head_t> m_sort_quote;

	// 指向计算引擎的指针
	FactorCalculationEngine* m_calc_engine;

	std::vector<TickVec> m_tick_storage;
	std::vector<TransVec> m_trans_storage;
	std::vector<OrderVec> m_order_storage;

	TicksData m_td;
};
