#pragma once

#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "sdp_handler/utils/json.hpp"

namespace config {

using json = nlohmann::json;

// 发送时间信息
// 精度为毫秒(HHMMSSmmm)
struct TimesInfo {
	// 起始时刻（毫秒精度 HHMMSSmmm）
	int start;
	// 结束时刻
	int end;
	// 均匀抽样间隔
	int interval;
	// 额外并入的时刻
	std::vector<int> add;
	// 排除的时刻
	std::vector<int> skip;
	// 解析或展开后的全部触发时刻
	std::vector<int> all_times;
	// 是否在 JSON 中显式配置过时间相关字段
	bool set = false;
};

// 因子集信息
// （local 因子 demo 与 live demo 共用该结构体）
struct FactorSetInfo {
	// 因子集名称
	std::string name;
	// 是否启用
	bool enabled;
	// 触发点
	TimesInfo trigger_points;
};

// ev 信息
struct EvInfo {
	// ev 编号，本地版框架忽略此值，平台版框架读取此值
	std::vector<int> sid;
	// 包含所有 ev 的目录（文件夹）地址，本地版框架会直接读取此值；平台版框架会将ev解压后覆写此值
	std::string folder_path;
};

// 模型信息
// （local 模型 demo 与 live demo 共用该结构体）
struct ModelInfo {
	// 模型ID
	int model_id;
	// 模型名称
	std::string name;
	// 申请的额外线程数
	int extra_threads_request;
	// 分配到的额外线程数
	int extra_threads_allocated;
	// OpenMP 预处理等并行段线程上界（来自 models_config / 单模型覆盖，默认见解析）
	int omp_num_threads = 16;
	// ev 信息
	EvInfo ev;
	// 运行时配置
	nlohmann::json runtime_config;
	// 非合并模式通过 SDP 发送模型矩阵时，若 >=0 则覆盖每条因子包的 elapsed_time（微秒）；-1 表示不覆盖。对应 JSON 键 elapsed。
	long sdp_overwrite_elapsed_us = -1;
};

// 保存信息
struct SaveInfo {
	// --- 因子保存信息 ---
	// 因子保存文件目录
	std::string factors_dir;
	// 因子保存文件名称
	std::string factors_name;
	// 保存因子值的文件路径
	std::string factors_save_file;
	// 因子二维数据集名称（按时间分片文件场景）
	std::string factors_data_dataset_name = "factordata";
	// 是否保存因子计算统计信息（解析缺省 true；详见 ParseFactorSaveInfo）
	bool save_factor_stats = true;
	// 因子计算统计信息保存文件目录
	std::string factor_stats_dir;

	// --- 模型保存信息 ---
	// 模型保存文件目录
	std::string models_dir;
	// 模型保存文件名称
	std::string models_name;
	// 保存模型预测值的文件路径
	std::string models_save_file;
	// 模型二维数据集名称（按时间分片文件场景）
	std::string models_data_dataset_name = "data";
	// 是否保存模型计算统计信息（解析缺省 true；详见 ParseModelSaveInfo）
	bool save_model_stats = true;
	// 模型计算统计信息保存文件目录
	std::string model_stats_dir;

#if defined(ENABLE_STRATEGY_SESSION_MODE_CONTINUOUS)
	// open5m 策略默认不合并，因为一天内会多次交易，不将多个模型结果进行对齐，
	// 算的快的，就先拿来决策交易，更快更灵活
	// 是否将多个模型结果进行对齐，默认不合并
	bool merge = false;
#else
	// 其他策略默认合并，因为一天只在某个固定时点做一次交易，所以需要将多个模型结果进行对齐
	// 是否将多个模型结果进行对齐，默认合并
	bool merge = true;
#endif

	// 保存时间
	TimesInfo save_times;
};

// 线程分配信息
struct ThreadInfo {
	// 因子计算申请的线程数
	int factors_request = -1;
	// 因子计算分配的线程数
	int factors_allocated = 0;
	// 模型计算申请的线程数
	int models_request = -1;
	// 模型计算分配的线程数（模型侧线程池总量，已涵盖各模型 extra_threads_allocated）
	int models_allocated = 0;
};

struct ConfigData {
	// 资产代码列表（与 app_live 字段对齐；因子独立程序可由引擎从代码表填充，配置中可留空）
	std::vector<std::string> asset_codes;

	// --- 因子信息 ---
	// 因子的 ev 信息
	EvInfo factor_ev;
	// 读取股票代码的文件名称
	std::string ev_code_file;
	// 发送时间
	TimesInfo send_times;
	// （启用的）因子时间戳列表
	std::vector<int> factor_timestamps;
	// （启用的）因子集信息列表
	std::vector<FactorSetInfo> enabled_factor_sets;
	// （启用的）因子集名称列表
	std::vector<std::string> enabled_factor_set_names;
	// （启用的）因子列名列表
	std::vector<std::string> factor_column_names;
	// factors_config.omp_num_threads，传入 FactorEntryConfig::omp_num_threads（缺省 16）
	int factor_omp_num_threads = 16;

	// --- 模型信息 ---
	// （启用的）模型信息列表
	std::vector<ModelInfo> model_info_list;
	// （启用的）模型名称列表
	std::vector<std::string> model_names;
	// models_config 顶层 JSON 键 elapsed：合并多模型结果一次性下发 SDP 时覆盖 send_factor_matrix 的耗时（微秒）；-1 表示不覆盖。
	long sdp_merge_overwrite_elapsed_us = -1;

	// --- 运行保存信息 ---
	// 保存信息
	SaveInfo save_info;
	// 线程分配信息
	ThreadInfo thread_info;

	// 是否在本地保存因子中间结果（app_factor 固定为本地落盘，解析后恒为 true）
	bool local_simulate = true;
	// 因子引擎：是否跳过延迟错过触发
	// 如果为true，则会跳过所有因为行情延迟而错过触发的时间戳；如果为false，则会复制上一次的值
	bool skip_missed_time_triggers = false;
	// 策略层：是否禁用定时器兜底触发
	// 如果为true，my_on_timer_v3 会直接返回，仅依赖行情回调推进
	bool disable_sdp_timer = false;
	// 动态库预热大小（字节），默认 -1 表示使用实际区域大小
	int warm_library_memory_size = -1;

	// 与 my_st_init_v3 首参 type、SDPHandler 内部 run_mode 一致；缺省 1 表示模拟（CONFIG_TYPE 中 SIMULATION_CONFIG）。
	// 不由 JSON/配置文件解析，仅通过 ConfigParser::SetRunMode 写入。
	int run_mode = 1;
};

// JSON 配置的加载与解析：因子段、模型段、保存路径、线程预算与运行开关；
// ParseConfig(date, thread_num) 的 thread_num 为运行时线程预算（仅用于对账提示）；
// 实际 allocated 字段严格取自 JSON 配置。app_factor 经 AllocateThreadsForFactors 仅解析因子侧。
class ConfigParser {
public:
	ConfigParser() = default;

	// 从文件读取配置
	void LoadFromFile(const std::string& config_file);

	// 从JSON字符串读取配置
	void LoadFromString(const std::string& json_str);

	// 从nlohmann::json读取配置
	void LoadFromJson(const json& json_content);

	// 解析配置文件内容
	void ParseConfig();

	// 结合外部参数解析配置文件内容
	void ParseConfig(const std::string& date, const int thread_num);

	// 设置存放解压的 EV 文件的根目录，并依此构建好 因子集体 与 所有模型各自 的子目录
	void SetRootPathForEv(const std::string& root_path);

	// 更新EV文件夹路径
	void UpdateEvFolderPath(int ev_sid, const std::string& folder_path);

	// 获取因子的 EV 目录文件夹地址
	const std::string& GetFactorEvFolderPath() { return config_.factor_ev.folder_path; }

	// 获取在EV中读取股票代码的文件名称
	const std::string& GetEvCodeFileName() { return config_.ev_code_file; }

	// 获取所有的 EV 信息
	void GetEv(std::vector<EvInfo>& ev_list);

	// 写入 config_.run_mode（与 my_st_init_v3 首参、SDPHandler run_mode 一致）；非 SIMULATION_CONFIG（当前值为 1）时将各模型 sdp elapsed 覆盖与 sdp_merge_overwrite_elapsed_us 置为 -1。
	void SetRunMode(int sdp_init_type);

	// 获取配置数据
	const ConfigData& GetConfig() const { return config_; }

	// ParseConfig 完成后，返回配置中实际参与预算对账的线程总数（因子池 + 模型池）
	int GetTotalAllocatedThreads() const;

private:
	// 解析因子配置信息
	void ParseFactorConfig();

	// 解析模型配置信息
	void ParseModelConfig();

	// 解析时间信息
	void ParseTimesInfo(const json& json_cfg, TimesInfo& times_info);

	// 解析运行设置信息
	void ParseRunningConfig();

	// 分配线程方案（含因子/模型，用于与实盘配置结构对齐）
	void AllocateThreads();

	// app_factor：仅因子落盘，按 JSON 配置分配因子侧线程，忽略 models_config
	void AllocateThreadsForFactors();

	// 解析因子保存信息
	void ParseFactorSaveInfo(const json& json_cfg);

	// 解析模型保存信息
	void ParseModelSaveInfo(const json& json_cfg);

	// 填充文件路径中的占位符
	// 目前支持的占位符有`[DATE]`
	std::string ResolvePlaceholder(const std::string& filename) const;

	// 在 ResolvePlaceholder 之后，仅将以 ./ 或 ../ 开头的显式相对路径按配置文件目录展开
	std::string ResolveConfigRelativePath(const std::string& path) const;

	// 记录配置文件绝对路径及其所在目录（LoadFromFile / JSON 中 config_file_path）
	void SetConfigFilePath(const std::string& config_file_path);

	// --- 成员变量 ---
	// 配置文件内容
	json json_content_;
	// 配置数据
	ConfigData config_;
	// 配置文件绝对路径
	std::string config_file_path_;
	// 配置文件所在目录（相对路径解析的基准）
	std::string config_file_dir_;
	// 日期
	std::string date_;
	// 线程数
	int thread_num_;
};

}  // namespace config