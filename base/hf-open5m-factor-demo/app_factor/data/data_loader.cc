#include "data_loader.h"

#include "comm/print.hpp"
#include "sdp_handler/core/sdp_handler.h"
#include "sdp_handler/quote_format_define.h"

double DataLoader::LoadRawData(int date, const std::string& asset) {
	auto start = std::chrono::high_resolution_clock::now();
	WLOG(TO_STRING("[DataLoader] 原始数据加载开始", date, asset), true);

	// 行情数据加载器
	// TicksData td = TicksData();

	// 原始数据容器
	TickVec vec_tick;    // Tick数据容器
	TransVec vec_trans;  // 逐笔成交数据容器
	OrderVec vec_order;  // 逐笔委托数据容器

	// 加载三类行情数据
	m_td.GetTick(date, asset, vec_tick);
	m_td.GetTransactionV2(date, asset, vec_trans);
	m_td.GetOrderV2(date, asset, vec_order);

	m_tick_storage.emplace_back(std::move(vec_tick));
	m_trans_storage.emplace_back(std::move(vec_trans));
	m_order_storage.emplace_back(std::move(vec_order));

	/* 将数据添加到统一容器中 */

	// 封装Tick数据
	merge_sort::quote_head_t quote_tick;
	quote_tick.data = m_tick_storage.back().data();
	quote_tick.mi_type = 209;
	quote_tick.itemsize = sizeof(TickRawData);
	quote_tick.size = m_tick_storage.back().size();
	m_sort_quote.push_back(quote_tick);

	// 封装Trans数据（需时间校准）
	merge_sort::quote_head_t quote_trans;
	merge_sort::inc_dec_local_time(m_trans_storage.back(), 1);  // 时间戳校准
	quote_trans.data = m_trans_storage.back().data();
	quote_trans.mi_type = 256;
	quote_trans.itemsize = sizeof(TransRawData);
	quote_trans.size = m_trans_storage.back().size();
	m_sort_quote.push_back(quote_trans);

	// 封装Order数据
	merge_sort::quote_head_t quote_order;
	quote_order.data = m_order_storage.back().data();
	quote_order.mi_type = 257;
	quote_order.itemsize = sizeof(OrderRawData);
	quote_order.size = m_order_storage.back().size();
	m_sort_quote.push_back(quote_order);

	auto duration =
	    std::chrono::duration_cast<std::chrono::duration<double>>(std::chrono::high_resolution_clock::now() - start);
	WLOG(TO_STRING("[DataLoader] 原始数据加载完成: tick data size ", m_tick_storage.back().size()), true);
	WLOG(TO_STRING("[DataLoader] 原始数据加载完成: trans data size ", m_trans_storage.back().size()), true);
	WLOG(TO_STRING("[DataLoader] 原始数据加载完成: order data size ", m_order_storage.back().size()), true);
	WLOG(TO_STRING("[DataLoader] 原始数据加载耗时:", duration.count(), "s"), true);

	return duration.count();
}

double DataLoader::MergeAndSortData() {
	auto start = std::chrono::high_resolution_clock::now();
	WLOG("[DataLoader] 数据合并排序开始");

	// 执行时间排序（按本地时间戳）
	merge_sort::load_quote_c(m_sort_quote, merge_sort::BY_LOCAL_TIME);  // 核心排序逻辑

	auto duration =
	    std::chrono::duration_cast<std::chrono::duration<double>>(std::chrono::high_resolution_clock::now() - start);
	WLOG(TO_STRING("[DataLoader] 数据合并排序耗时:", duration.count(), "s"), true);

	return duration.count();
}

void DataLoader::SetCalculationEngine(FactorCalculationEngine& calc_engine_instance) {
	m_calc_engine = &calc_engine_instance;
}

void DataLoader::DistributeData() {
	auto start = std::chrono::high_resolution_clock::now();
	WLOG(TO_STRING("[DataLoader] 数据分发开始"), true);

	merge_sort::point_t p;
	uint64_t res;
	while ((res = merge_sort::pop_tick(&p)) != merge_sort::kReachEnd) {
		if (m_calc_engine->IsStopped()) {
			break;
		}
		// 本地版其实用不着start_time_t，但是为了和平台版保持一致方便更新，这里也用一下
		start_time_t start_time;
		update_machine_time(&start_time);
		// 根据数据类型分发处理
		switch (p.mi_type) {
			case 209: {  // tick
				TickRawData* p_tick = (TickRawData*)m_sort_quote[p.col].data;
				p_tick = p_tick + p.row;
				start_time.exch_time = p_tick->quote.exch_time;
				start_time.local_time = p_tick->local_time;
				// 由于内存布局相同，直接使用 reinterpret_cast 转换指针，无需创建临时对象
				m_calc_engine->OnQuote(reinterpret_cast<Stock_Internal_Book*>(&p_tick->quote), start_time);
				break;
			}
			case 256: {  // trans
				TransRawData* p_trans = (TransRawData*)m_sort_quote[p.col].data;
				p_trans = p_trans + p.row;
				start_time.exch_time = p_trans->quote.int_time;
				start_time.local_time = p_trans->local_time;
				// 由于内存布局相同，直接使用 reinterpret_cast 转换指针，无需创建临时对象
				m_calc_engine->OnTrans(
				    reinterpret_cast<Stock_Transaction_Internal_Book_New*>(&p_trans->quote), start_time);
				break;
			}
			case 257: {  // order
				OrderRawData* p_order = (OrderRawData*)m_sort_quote[p.col].data;
				p_order = p_order + p.row;
				start_time.exch_time = p_order->quote.int_time;
				start_time.local_time = p_order->local_time;
				// 由于内存布局相同，直接使用 reinterpret_cast 转换指针，无需创建临时对象
				m_calc_engine->OnOrder(reinterpret_cast<Stock_Order_Internal_Book_New*>(&p_order->quote), start_time);
				break;
			}
			default: {
				std::cerr << "[DataLoader] 未知数据类型: " << p.mi_type << std::endl;
				break;
			}
		}
	}

	auto duration =
	    std::chrono::duration_cast<std::chrono::duration<double>>(std::chrono::high_resolution_clock::now() - start);
	WLOG(TO_STRING("[DataLoader] 数据分发完毕"), true);
	WLOG(TO_STRING("[DataLoader] 数据分发耗时:", duration.count(), "s"), true);
}