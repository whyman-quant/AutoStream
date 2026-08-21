#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <unordered_map>

#include "comm/velapex/fast_soa_memory_pool.h"
#include "comm/velapex/perfect_hash_map.h"
#include "sdp_handler/core/sdp_handler.h"
#include "sdp_handler/quote_format_define.h"

// 统一数据与类型定义：
// - 集中维护 engine/thread 共用的数据结构；
// - 集中维护行情池化相关类型别名，便于后续替换底层内存池实现。

// 单资产元数据：记录资产分组与交易所时间单调性，供引擎路由与去重。
// 细节：初始化时资产按数量分组、因子按配置分组；string code 查询不如 int 索引快，故用查表维护映射。
//
// 示例（4696 只股票分 3 组、因子 2 组、6 线程）：
// | 资产分组 | 分组容量 |
// | 0        | 1566     |
// | 1        | 1565     |
// | 2        | 1565     |
// | 因子组 | 因子集示例          |
// | 0      | RESERVED, demo0001 |
// | 1      | demo0000           |
// 同资产组的多线程从同一行情队列/缓冲取数。
//
// 字段语义：
// - code_int：6 位证券代码的整型形式（如 600000）；在引擎与线程侧直接用作全局主键与稀疏表下标，
//   不再单独维护「全市场第几只」的连续序号，少一层映射、取值直观；下标稀疏但不超过 999999 可接受。
// - group_idx：资产分组编号；行情分发时定位写入哪一个时序队列或路由逻辑。
// - prev_exch_time：本合约最近已处理的交易所时间，保证时间单调推进。
// 组内序号不在本结构体内存放：由时序线程的 group_code_ints_ 顺序与按 code_int 稀疏索引的 AssetState 槽位表达。
struct AssetInfo {
	// 6 位证券代码整型（如 600000）；直接作全局路由与稀疏槽位下标，语义见上文 code_int。
	int code_int = -1;
	// 资产所属分组索引（无时序因子时为 -1）。
	int group_idx = -1;
	// 最近处理过的交易所时间，确保时间单调推进。
	int prev_exch_time = 0;

	AssetInfo() = default;

	AssetInfo(int ci, int grp_id) : code_int(ci), group_idx(grp_id) {}
};

template <typename T>
using Code6iMap = velapex::perfect_hash_map::Code6iMap<T>;

// 行情对象内存池类型（SAMU = 单线程申请、多线程使用）：
// - 申请端在引擎主线程，释放端在多个计算线程；
// - 使用计数通过原子字段管理，适合当前“单生产者 + 多消费者”模式。
using QuoteMemoryPool = velapex::fast_soa_memory_pool::SAMUMemoryPool<Stock_Internal_Book>;
using TransMemoryPool = velapex::fast_soa_memory_pool::SAMUMemoryPool<Stock_Transaction_Internal_Book_New>;
using OrderMemoryPool = velapex::fast_soa_memory_pool::SAMUMemoryPool<Stock_Order_Internal_Book_New>;

// 池化元素句柄类型：
// - 内含 data 指针与引用计数控制能力（ResetUseCount / Release）；
// - 在线程间只传递该句柄，避免重复分配与重复拷贝。
using QuoteMemoryData = QuoteMemoryPool::PooledElementType;
using TransMemoryData = TransMemoryPool::PooledElementType;
using OrderMemoryData = OrderMemoryPool::PooledElementType;

// Tick 载荷类别：取值 1–4 与调度、日志及外部工具中的类型编码约定一致。
enum class TickDataKind : std::uint8_t {
	kUnknown = 0,
	kQuote = 1,
	kTrans = 2,
	kOrder = 3,
	kTimePoint = 4,
};

// Tick 级输入：统一封装 quote / trans / order / timepoint，经队列或 SPMC 在引擎与计算线程之间传递。
// 用于在引擎与计算线程之间传递行情类消息；消费端按 data_type 与时间字段分支处理。
//
// 示例（4696 只股票分 3 组）：组容量分配表同上 AssetInfo；组内路由见引擎与线程实现。
//
// 结构约定：
// - 资产标识使用 int code_int，与引擎内全局路由一致。
// - 载荷类别使用枚举 TickDataKind（data_type），并与时间戳类消息共用同一封装。
// - 行情体通过池化句柄 QuoteMemoryData* / TransMemoryData* / OrderMemoryData* 传递，
//   见上方 SAMU 内存池类型别名；线程间只传句柄，避免重复分配与整结构拷贝。
//
// 时间字段（kTimePoint）：
// - data_time_ms：数据侧时刻（与引擎 OnTimer 的 time_ms 一致；CONTINUOUS 下因子 API 入参取该字段）。
// - trigger_time_ms：离散调度网格时刻（本次在调度序列上推进到的点；调度集合比对始终用该字段）。
// OPEN/CLOSE 下因子 API 入参与调度键均取 trigger_time_ms（见各线程 CalcFunc 内 factor_api_time_ms）。
// 非 kTimePoint 时两者多为 0。
// time_operation 各二进制位语义见 runtime_policy::TimeOperationBits。
struct TickDataInfo {
	// 证券代码整型；时间戳类消息可为 -1。非时间戳时与 AssetInfo::code_int 同义，作稀疏索引键。
	int code_int = -1;

	TickDataKind data_type = TickDataKind::kUnknown;

	// 入口回调捕获的起始时间。
	start_time_t start_time;

	// 数据侧时刻（kTimePoint 时与 OnTimer 的 time_ms 一致）。
	int data_time_ms = 0;
	// 本次被触发处理的调度时刻（kTimePoint：离散网格点）。
	int trigger_time_ms = 0;
	// 发送批次索引。
	int trigger_send_batch_idx = -1;
	// 时间操作位（见 runtime_policy::TimeOperationBits）。
	int time_operation = 0;

	// quote 池化句柄（非裸 Stock_Internal_Book*）。
	QuoteMemoryData* q1 = nullptr;
	// trans 池化句柄。
	TransMemoryData* q2 = nullptr;
	// order 池化句柄。
	OrderMemoryData* q3 = nullptr;
};

// 因子结果条目：用于队列传递或耗时统计的轻量元数据。
// 因子数值由计算线程写入引擎持有的 result_cache_（按时间戳分行，与扫描/发送路径共享），不在本结构内重复承载；
// 队列侧通常只投递时刻、RDTSC 锚点与各阶段微秒耗时等，避免整行因子矩阵随队列入队拷贝。
struct FactorResultInfo {
	int time_ms = 0;
	uint64_t start_tsc = 0;
	double tick_wait_elapsed_us = 0;
	double factor_calc_duration_us = 0;
	double factor_calc_elapsed_us = 0;

	FactorResultInfo() = default;

	FactorResultInfo(int time_ms_, uint64_t start_tsc_, double tick_wait_elapsed_us_, double factor_calc_duration_us_,
	    double factor_calc_elapsed_us_) noexcept
	    : time_ms(time_ms_),
	      start_tsc(start_tsc_),
	      tick_wait_elapsed_us(tick_wait_elapsed_us_),
	      factor_calc_duration_us(factor_calc_duration_us_),
	      factor_calc_elapsed_us(factor_calc_elapsed_us_) {}
};

// 因子→模型→对外发送 一条时间轴上的分段耗时（微秒）；按每个因子对齐发送时刻占 `time_stats_info_list_` 一行。
// 因子半段与 `models::comm::time_stats_t` 中 tick_max_wait / factor_max_calc / factor_max_copy / factor_scan /
// factor_send 各对字段同源；模型与发送半段由 `ModelResultScanThread` 在合并多模型输出并调用 `SendData` 后补全。
// `*_elapsed_us`：自本批次 `time_stats.start_tsc`（因子路径写入，见结果扫描线程）起算的累计延迟。
// `*_duration_us`：链路上相邻里程碑之间的片段耗时（与引擎日志/CSV 表头各阶段列名及 `TotalElapsed` 对应关系见
// `ModelCalculationEngine`）。
struct TimeStatsInfo {
	// 自 tick 条件满足至因子侧「开始处理」前的最大等待（累计，相对 start_tsc）；与
	// time_stats_t::tick_max_wait_elapsed_us 一致。
	double tick_wait_elapsed_us = 0;
	// 自 start_tsc 至因子计算完成的最大累计；与 time_stats_t::factor_max_calc_elapsed_us 一致。
	double factor_calc_elapsed_us = 0;
	// 自 start_tsc 至 memcpy 写入 result_cache 完成的最大累计；与 time_stats_t::factor_max_copy_elapsed_us 一致。
	double factor_copy_elapsed_us = 0;
	// 自 start_tsc 至扫描线程拼齐本时刻因子矩阵的累计；与 time_stats_t::factor_scan_elapsed_us 一致。
	double factor_scan_elapsed_us = 0;
	// 自 start_tsc 至模型计算线程读到本批 input_t（进入 Calculate 前）的累计；多模型时取各模型上报 factor_send_elapsed
	// 的最大值。
	double factor_send_elapsed_us = 0;
	// 自 start_tsc 至当前模型输出被本轮收集逻辑处理到时的累计（RDTSC 换算）；多模型逐步合并时更新。
	double model_calc_scan_elapsed_us = 0;
	// 自 start_tsc 至本批次 SendData（因子矩阵对外发送）返回后的累计（RDTSC 换算）；作为日志中的 TotalElapsed。
	double model_send_elapsed_us = 0;

	// 与 tick_wait_elapsed_us 同源片段（当前写入路径下二者数值相同）。
	double tick_wait_duration_us = 0;
	// 纯因子计算阶段的最大自身耗时；与 time_stats_t::factor_max_calc_duration_us 一致。
	double factor_calc_duration_us = 0;
	// 写 result_cache  memcpy 段最大耗时；与 time_stats_t::factor_max_copy_duration_us 一致。
	double factor_copy_duration_us = 0;
	// 扫描线程在收齐各线程至 SendFactors 之间的尾段；与 time_stats_t::factor_scan_duration_us 一致。
	double factor_scan_duration_us = 0;
	// 因子矩阵已齐备至模型线程实际开始消费之间的间隔：factor_send_elapsed − factor_scan_elapsed（见
	// ModelCalculationThread）。
	double factor_send_duration_us = 0;
	// 因子已送达模型之后，至该模型结果并入前：max(当前累计 model 段 elapsed − 该模型自带 factor_send_elapsed_us)（见
	// ModelResultScanThread）。
	double model_calc_scan_duration_us = 0;
	// 对外发送尾段：model_send_elapsed_us − model_calc_scan_elapsed_us（SendData 相对此前推理/合并累计的剩余区间）。
	double model_send_duration_us = 0;
};

// 单次因子计算采样：时序/截面线程在每次命中配置计算点的 TriggerCompute 或 UpdateFactors 路径写入；
// OPEN 与 CONTINUOUS 均累积，供引擎汇总进各时间点的 factor_compute_time_stats_map 并打印 Time Statistics 表。
struct FactorComputeTimeStatsInfo {
	// 因子集名称（与配置中的因子集名一致）。
	std::string factor_set_name;
	// 与 TickDataInfo::trigger_time_ms 同源；即引擎 trigger_time_points_map_ / all_time_points_vector_ 的离散调度键（CONTINUOUS 下为网格时刻，区别于 data_time_ms）。
	int trigger_time_ms = 0;
	// 自 TickDataInfo::start_time 起至进入因子计算前的等待（微秒，RDTSC 换算）。
	double tick_wait_duration_us = 0;
	// 本次因子计算主体耗时（微秒，RDTSC 换算）。
	double compute_duration_us = 0;
	// 计算开始前 wall 时间戳（微秒）。
	uint64_t start_timestamp_us = 0;
	// 计算结束后 wall 时间戳（微秒）。
	uint64_t end_timestamp_us = 0;
};

// 时间点元信息：描述某个触发时刻要做什么，以及该时刻的上下文。
struct TriggerTimePointInfo {
	int trigger_time_ms = 0;
	bool is_compute_point = false;
	bool is_send_point = false;
	bool call_OnGlobalTime = false;
	bool saved_to_cache = false;
	bool only_sz_available = false;
	size_t send_point_idx = 0;
	size_t valid_row_num = 0;

	// 该触发时刻对应的链路起始时间（由生产者写入）；表示该时间点开始处理时刻。
	start_time_t start_time{0, 0, 0};
	// 是否已通过 set_start_time 写入有效 start_time。
	std::atomic<bool> start_time_ready{false};

	// 因子集 -> 该时间点统计信息
	std::unordered_map<std::string, FactorComputeTimeStatsInfo> factor_compute_time_stats_map;

	TriggerTimePointInfo(const TriggerTimePointInfo&) = delete;
	TriggerTimePointInfo& operator=(const TriggerTimePointInfo&) = delete;

	TriggerTimePointInfo() = default;

	// 仅生产者线程写入开始时间。
	void set_start_time(const start_time_t& t) {
		start_time = t;
		start_time_ready.store(true, std::memory_order_release);
	}

	// 消费者线程读取开始时间；未就绪返回 false。
	bool get_start_time(start_time_t& t) {
		if (start_time_ready.load(std::memory_order_acquire)) {
			t = start_time;
			return true;
		}
		return false;
	}
};

// 模型心跳消息（由因子引擎时钟推进后广播给模型线程）。
struct TimerNoticeMsg {
	int exch_time_ms = 0;
};
