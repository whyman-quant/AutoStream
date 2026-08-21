#pragma once

// 运行时策略矩阵：
// - 将“交易时段模式 -> 默认行为”的映射集中在一个头文件内；
// - 供 config / engine / thread 共用，避免默认值散落在多个模块里。
namespace runtime_policy {

// 交易时段模式（编译期宏会映射到其中一个值）。
enum class TradePeriodMode { kOpen, kClose, kContinuous, kUnknown };

#if defined(ENABLE_STRATEGY_SESSION_MODE_OPEN)
static constexpr TradePeriodMode kTradePeriodMode = TradePeriodMode::kOpen;
#elif defined(ENABLE_STRATEGY_SESSION_MODE_CLOSE)
static constexpr TradePeriodMode kTradePeriodMode = TradePeriodMode::kClose;
#elif defined(ENABLE_STRATEGY_SESSION_MODE_CONTINUOUS)
static constexpr TradePeriodMode kTradePeriodMode = TradePeriodMode::kContinuous;
#else
static constexpr TradePeriodMode kTradePeriodMode = TradePeriodMode::kUnknown;
#endif

// 运行策略矩阵：统一描述不同交易时段下的默认行为。
// 说明：以下为「未在 JSON 里显式写死」时的编译期默认倾向；若配置里写了对应字段，仍以配置为准。
struct StrategyMatrix final {
	// 因子/模型 HDF5 落盘是否「按发送时间戳拆成多个文件」的默认倾向。
	// - true（连续竞价 kContinuous）：每个时间点单独一个 .h5，与盘中多次触发、多次落盘的习惯一致；
	//   对应引擎里 `save_by_split_timestamp_ == true` 时的 `SaveResultsToH5SplitTimestamp*` 路径。
	// - false（集合竞价 open/close）：默认把所有时间点的数据集写进同一文件（或单文件多 dataset），
	//   对应 `save_by_split_timestamp_ == false` 时的 `SaveResultsToH5CollectTimestamp*` 路径。
	static constexpr bool kDefaultSplitTimestampSave = (kTradePeriodMode == TradePeriodMode::kContinuous);

	// 多模型输出是否在「同一 bar 时间」上对齐合并的默认倾向。
	// - true（open/close）：一天通常只在固定时点做一次决策，默认合并便于一行里对齐所有模型列。
	// - false（kContinuous）：盘中可能多次推理，默认不合并，各模型可先产出先消费，避免互相等待对齐。
	static constexpr bool kDefaultMergeModelResults = (kTradePeriodMode != TradePeriodMode::kContinuous);

	// 是否在因子管线里默认打开「全局时钟」类语义（OnGlobalTime / 时间位 kGlobalTime）的倾向。
	// - true（kContinuous）：与全局时间网格、截面因子等连续时段逻辑配套；`TimeOperationBits::kGlobalTime`
	//   会在引擎组装的 `TickDataInfo::time_operation` 里被置位，线程侧据此调用因子入口的 OnGlobalTime
	//   （与 `TickDataInfo::data_type`（TickDataKind）独立：载荷类别 vs. 时间点上的操作位）。
	// - false（open/close）：默认不依赖该路径，减少与集合竞价时段无关的全局回调开销。
	static constexpr bool kEnableGlobalTimeHook = (kTradePeriodMode == TradePeriodMode::kContinuous);
};

// 引擎与线程间共享的时间操作位定义（bitmask）：
// - 可以按位或组合，例如“计算 + 发送”。
struct TimeOperationBits final {
	// bit0: 触发计算流程。
	static constexpr int kCompute = 0x01;
	// bit1: 触发发送流程。
	static constexpr int kSend = 0x02;
	// bit2: 触发 OnGlobalTime 回调。
	static constexpr int kGlobalTime = 0x04;
};

}  // namespace runtime_policy
