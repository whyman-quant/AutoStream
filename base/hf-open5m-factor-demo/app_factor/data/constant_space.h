#pragma once

#include <cstddef>

namespace constant_space {

// --- 时间相关常量 ---

// 集合竞价开始时间，也是股票交易开始时间，单位毫秒
static constexpr int kStartTimeHHMMSSms = 91500000;
// 股票交易结束时间，单位毫秒
static constexpr int kEndTimeHHMMSSms = 150000000;
// 框架内策略时间戳间隔与最大精度，10毫秒，这是一个在速度和精度之间的平衡，满足大多数策略的需求，同时也不会占用过多的内存
static constexpr int kTimeIntervalMs = 10;
// 因子的定时通知的开始时刻
static constexpr int kGlobalStartTimeHHMMSSms = 93000000;
// 因子的定时通知的结束时刻
static constexpr int kGlobalEndTimeHHMMSSms = 145701000;
// 因子的定时通知的时间戳间隔，1000 毫秒
static constexpr int kGlobalTimeIntervalMs = 1000;

// --- 线程 CPU 轨迹参数 ---
// 供 velatools::thread_cpu_trace 使用；线程构造时经 MakeTraceOptions 传入，引擎 Start 末尾亦直接引用部分常量。

// 工作线程入口 settle 等待（毫秒）：采 initial 后 sleep，再采 reallocate，用于观察调度迁核是否稳定。
#if defined(ENABLE_APP_LIVE)
static constexpr int kThreadCpuTraceReallocSettleSleepMs = 5000;
// 全部工作线程拉起后的同步等待（毫秒）：须 ≥ kThreadCpuTraceReallocSettleSleepMs，再读取 initial/reallocate 快照。
static constexpr int kThreadCpuTracePostStartSyncSleepMs = 6000;
#else
static constexpr int kThreadCpuTraceReallocSettleSleepMs = 250;
static constexpr int kThreadCpuTracePostStartSyncSleepMs = 400;
#endif
// 主循环 run 阶段采样：每 N 次迭代至少触发一次 CPU 采样（兜底，防止单轮极慢时长期无样本）。
static constexpr int kThreadCpuTraceLoopSampleInterval = 100;
// 主循环 run 阶段采样：距上次采样满该 TSC 换算墙钟微秒数也触发一次（约 1 秒一条 run 记录）。
static constexpr double kThreadCpuTraceWallSampleIntervalUs = 1000000.0;
// run_segments 预分配容量：按约 1 秒一条采样估算，减少 vector 扩容。
static constexpr int kThreadCpuTraceRunSegmentReserve = 25200;

// --- 行情相关常量 ---

// 最大股票代码，我们用6位整数表示股票代码，最大值为 999999，可表示的最多数量为 1000000 个
static constexpr size_t kMaxAssetCode = 999999;
// 每个股票每天的Quote行情数量上限，实际上集合竞价和连续竞价加起来也不会超过5100条
static constexpr size_t kQuoteNumPerAsset = 5200;
// 平均每个股票每天的Trans行情数量
static constexpr size_t kTransNumPerAsset = kQuoteNumPerAsset * 100;
// 平均每个股票每天的Order行情数量
static constexpr size_t kOrderNumPerAsset = kQuoteNumPerAsset * 100;

// --- 配置文件的缺省默认值 ---

// 读取股票代码的文件名称，缺省默认值
// CONTINUOUS：对齐 hf-open5m-live-demo（strategy.cpp 中 EV 代码文件名 lab200005）
// OPEN/CLOSE：hf-open-live-demo 与 hf-close-live-demo 的 constant_space 均为 lab200019_codes.h5
#if defined(ENABLE_STRATEGY_SESSION_MODE_CONTINUOUS)
static constexpr const char* kDefaultCodeFileName = "lab200005_codelist.h5";
#elif defined(ENABLE_STRATEGY_SESSION_MODE_OPEN) || defined(ENABLE_STRATEGY_SESSION_MODE_CLOSE)
static constexpr const char* kDefaultCodeFileName = "lab200019_codes.h5";
#else
#error "STRATEGY_SESSION_MODE 未定义：需由 CMake 传入 OPEN / CLOSE / CONTINUOUS 之一"
#endif

// EV里用于读取股票代码的文件名称，缺省默认值
#if defined(ENABLE_STRATEGY_SESSION_MODE_CONTINUOUS)
static constexpr const char* kDefaultEvCodeFileName = "mengmai/lab200005_codelist.h5";
#elif defined(ENABLE_STRATEGY_SESSION_MODE_OPEN) || defined(ENABLE_STRATEGY_SESSION_MODE_CLOSE)
static constexpr const char* kDefaultEvCodeFileName = "mengmai/lab200019_codes.h5";
#else
#error "STRATEGY_SESSION_MODE 未定义：需由 CMake 传入 OPEN / CLOSE / CONTINUOUS 之一"
#endif

// EV文件夹是本地路径，缺省默认值
static constexpr const char* kDefaultEvFolderPath =
	"/mnt/beegfs_dev/storage_r/706_wgh/app_working_dir/data/stock_open/ev_v20250605";

// 股票代码文件的备份路径
static constexpr const char* kBackupCodeListFolderPath = "/mnt/beegfs_ssd_raid91/706_wgh_new/stock_open/basedata";

}  // namespace constant_space
