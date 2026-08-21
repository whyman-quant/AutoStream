#pragma once

#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "comm/vela_third_party/readerwriterqueue.h"
#include "comm/velapex/spmc_broadcast_buffer_recyclable.h"
#include "config/config_parser.h"
#include "data/data_and_types.h"
#include "data/runtime_policy.h"
#include "thread/cross_sectional_factor_calculation_thread.h"
#include "thread/factor_calculation_thread.h"
#include "thread/factor_result_scan_thread.h"
#include "factors/_comm/factor_entry_manager.h"

// --------------------------------------------------------------------------------
// 因子计算引擎：管理线程生命周期、行情分发、结果汇总与 HDF5 保存。
// InitConfig 按 factors_config.factor_sets 配置顺序生成 factor_set_column_layout_（支持时序/截面交错列布局）。
// --------------------------------------------------------------------------------
class FactorCalculationEngine {
public:
	// 添加默认构造函数
	FactorCalculationEngine() = default;

	~FactorCalculationEngine() {
#ifdef ENABLE_APP_LIVE
		// live模式在析构函数中打印统计信息，本地模式在Stop()中打印统计信息
		PrintTimeStats();
#endif	// ENABLE_APP_LIVE
		// 析构顺序：手动清理 → 成员自动析构（逆声明顺序）→ 基类析构（如果存在）
		// RAII对象（智能指针、容器等）会自动析构
	}

	// 初始化计算引擎。
	// date: 交易日期。
	// asset_codes: 资产代码列表。
	// thread_num: 预设线程总数。
	// config: 配置信息。
	void Init(int date, const std::vector<std::string>& codes, int thread_num, const config::ConfigData& config);

	// 启动所有线程
	void Start();

	// 收集运行时线程树（depth 相对引擎根为 0），供日志展示；子节点来自各因子集/扫描线程的扩展线程
	std::vector<std::pair<int, std::string>> CollectRuntimeThreadTreeLines() const;

	// 预热关键内存，在动态库预热后调用，确保内存状态最新
	// 预热前几条行情会用到的内存，避免首次访问时的 page fault 和缓存未命中
	void WarmUp();

	// 停止所有线程
	void Stop();

	// 将Quote数据进行分发到对应缓存
	void OnQuote(Stock_Internal_Book* quote, start_time_t t);

	// 将Trans数据进行分发到对应缓存
	void OnTrans(Stock_Transaction_Internal_Book_New* quote, start_time_t t);

	// 将Order数据进行分发到对应缓存
	void OnOrder(Stock_Order_Internal_Book_New* quote, start_time_t t);

	// 处理时间戳数据，时间格式为HHMMSSmmm，比如092459970，表示09:24:59.970
	void OnTimer(int time_ms, start_time_t t);

	// 是否未在运行（正在停止或已停止）
	bool IsNotRunning() const { return stop_status_ != 0; }

	// 是否已完全停止（stop_status_ == 2）
	bool IsStopped() const { return stop_status_ == 2; }

	const std::vector<std::string>& asset_codes() const { return asset_codes_; }

	const std::vector<std::string>& factor_column_names() const { return factor_column_names_; }

	const std::vector<int>& send_times() const { return send_time_points_vector_; }

	// --- 线程属性 ---
	struct ThreadInfo {
		// 线程名称
		const char* name;
		// 线程数量：0 代表数量不定，需后续确定；非 0 代表固定数量
		int count;
		// 线程数量确定方式：Fixed: 固定数量，ConfigDriven: 配置驱动数量，ContentDriven: 内容驱动数量
		const char* source;
		// 线程数量确定方式详情
		const char* detail;
	};

	// 线程信息列表：通过静态函数返回，避免类外定义。
	// - 背景：static const 数组在 range-for 中会被 odr-use，
	//   需在类外的源代码文件中提供唯一定义，否则链接报 undefined symbol。
	// - 做法：改用函数内 static 局部变量。
	//   C++ 标准规定，函数内 static 变量在程序生命周期内仅初始化一次。
	static const std::vector<ThreadInfo>& GetThreadInfoList() {
		static const std::vector<ThreadInfo> list = {
			{"FactorResultScanThread", 1, "Fixed", "Fixed"},
			{"FactorCalculationThread", 0, "ConfigDriven", "Number set in configuration"},
			{"CrossSectionalFactorCalculationThread", 0, "ConfigDriven", "Number set in configuration"},
		};
		return list;
	}

	// 静态函数，通过 GetThreadInfoList 计算固定线程数
	static int GetFixedThreadCount() {
		int count = 0;
		for (const auto& info : GetThreadInfoList()) {
			// source 为 const char*，不可用 == 与字面量比较指针；须按内容比较。
			if (info.source != nullptr && std::strcmp(info.source, "Fixed") == 0) {
				count += info.count;
			}
		}
		return count;
	}

private:
	// 初始化资产代码
	void InitAssetCodes(const std::vector<std::string>& codes);

	// 读取并解析配置文件，获取因子信息、保存信息
	void InitConfig(const config::ConfigData& config);

	// 确定线程分配方案
	void AssignWorkLoads(int allocable_thread_num);

	// 确定线程映射关系
	void AssignThreadMapping();

	// 打印时间统计信息
	void PrintTimeStats();

	// 保存最终的结果为*.h5文件
	void SaveResultsToH5();

	// 按时间点分别保存结果，每个时间点一个 HDF5 文件
	void SaveResultsToH5SplitTimestamp();

	// 将所有时间点的结果写入同一个 HDF5 文件
	void SaveResultsToH5CollectTimestamp();

	// ----- 基础信息 -----
	// 交易日期
	std::string trade_date_str_;

	// ----- 保存配置 -----
	// 保存路径
	std::string save_dir_;
	// 保存文件路径
	std::string save_file_path_;
	// 是否保存因子统计信息（app_factor 默认 true，见 SaveInfo）
	bool save_factor_stats_ = true;
	// 因子统计信息保存目录
	std::string factor_stats_dir_;
	// 按时间分片文件时，二维因子数据集名称
	std::string factor_data_dataset_name_;
	// 保存时间
	std::vector<int> save_times_;
	// 是否按时间点分别保存结果，每个时间点一个 HDF5 文件。
	bool save_by_split_timestamp_{runtime_policy::StrategyMatrix::kDefaultSplitTimestampSave};

	// ----- 线程与分配 -----
	// 时序因子计算线程数量
	int ts_calc_thread_num_;
	// 截面因子计算线程数量
	int cs_calc_thread_num_;

	// ----- 资产与分组 -----
	// 资产分组数量
	int asset_group_num_;
	// 所有资产的列表
	std::vector<std::string> asset_codes_;
	// 深圳股票列表
	std::vector<std::string> sz_asset_codes_;
	// 上海股票列表
	std::vector<std::string> sh_asset_codes_;
	// 资产元数据：按 6 位 code_int 完美哈希（见 Code6iMap），供行情路由与分组查询
	Code6iMap<AssetInfo> asset_info_map_;
	// 资产组内的资产代码列表
	std::vector<std::vector<std::string>> codes_in_asset_group_;
	// 每个资产组前面有多少个资产
	std::vector<size_t> asset_group_off_set_;

	// ----- 因子元信息 -----
	// 因子数量
	int factor_size_;
	// 所有因子集名称（顺序同 factors_config.factor_sets 配置，支持时序/截面交错）
	std::vector<std::string> factor_entry_names_;
	// 各因子集在全局因子行中的列布局（start/count）；由 InitConfig 按 factor_entry_names_ 顺序累加生成
	factors::FactorSetColumnLayout factor_set_column_layout_;
	// 时序因子集名称列表（相对配置顺序，未做 RESERVED/字典序重排）
	std::vector<std::string> ts_factor_entry_names_;
	// 截面因子集名称列表（相对配置顺序）
	std::vector<std::string> cs_factor_entry_names_;
	// 所有因子名称
	std::vector<std::string> factor_column_names_;
	// 各截面因子集在全局因子行中的列起始下标（= factor_set_column_layout_[name].start，与 AssignThreadMapping 一一对应）
	std::vector<size_t> cs_factor_start_indices_;

	// ----- 时间点与触发 -----
	// 是否跳过延迟错过触发
	bool skip_missed_time_triggers_ = false;
	size_t send_time_point_idx_ = 0;
	size_t all_time_point_idx_ = 0;
	// 调用 CallOnGlobalTime 的时间戳列表
	std::vector<int> global_time_points_vector_;
	// 发送因子数据的时间戳列表
	std::vector<int> send_time_points_vector_;
	// 所有时间戳列表
	std::vector<int> all_time_points_vector_;
	// 因子计算时间戳映射表，包含每个因子集的计算时间戳集合
	std::unordered_map<std::string, std::unordered_set<int>> factor_compute_time_points_map_;
	// 触发时间戳映射表，包含计算点和发送点
	std::shared_ptr<std::unordered_map<int, TriggerTimePointInfo>> trigger_time_points_map_;

	// ----- 运行时缓存与结果 -----
	// 行情分发缓存队列（时序因子使用）
	std::vector<std::shared_ptr<moodycamel::ReaderWriterQueue<TickDataInfo>>> data_queues_;
	// 公共 SPMCBuffer（截面因子使用）
	std::shared_ptr<velapex::spmc_broadcast_buffer_recyclable::SPMCBroadcastBuffer<TickDataInfo>> spmc_buffer_;
	// 行情内存池（单线程申请，多线程消费释放）
	QuoteMemoryPool quote_memory_pool_;
	TransMemoryPool trans_memory_pool_;
	OrderMemoryPool order_memory_pool_;
	// 时序因子计算线程通知扫描线程获取结果的队列
	std::vector<std::shared_ptr<velapex::spsc_queue::SPSCQueue<int>>> ts_result_queues_;
	// 截面因子计算线程通知扫描线程获取结果的队列
	std::vector<std::shared_ptr<velapex::spsc_queue::SPSCQueue<int>>> cs_result_queues_;
	// 结果缓存，每行存储不同时间戳的所有结果
	std::shared_ptr<std::vector<std::vector<char>>> result_cache_;
	// 最终结果保存容器
	std::shared_ptr<std::vector<std::vector<factors::fval_t>>> result_data_;

	// ----- 线程相关 -----
	// 时序因子计算线程
	std::vector<std::unique_ptr<FactorCalculationThread>> ts_calc_threads_;
	// 截面因子计算线程
	std::vector<std::unique_ptr<CrossSectionalFactorCalculationThread>> cs_calc_threads_;
	// 结果扫描线程
	std::unique_ptr<FactorResultScanThread> factor_result_scan_thread_;
	// 停止状态：0 未停止，1 正在停止，2 已停止
	int stop_status_ = 0;
};
