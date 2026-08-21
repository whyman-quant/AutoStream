#include "config_parser.h"

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits.h>
#include <sstream>
#include <stdexcept>
#include <unistd.h>
#include <vector>

namespace config {

namespace {

// 若 filename 不含扩展名，则追加 ext（保证 ext 以 '.' 开头）。
std::string EnsureExtension(const std::string& filename, const std::string& ext) {
	const std::string dot_ext = (ext.empty() || ext[0] == '.') ? ext : "." + ext;
	return (filename.find_last_of('.') == std::string::npos) ? filename + dot_ext : filename;
}

int NonNegativeThreadCount(int n) { return std::max(0, n); }

bool IsAbsolutePath(const std::string& path) { return !path.empty() && path[0] == '/'; }

// JSON 中以 ./ 或 ../ 开头、意图相对配置文件所在目录的路径
bool IsExplicitConfigRelativePath(const std::string& path) {
	return path.compare(0, 2, "./") == 0 || path.compare(0, 3, "../") == 0;
}

std::string GetParentDirectory(const std::string& filepath) {
	const auto pos = filepath.find_last_of("/\\");
	if (pos == std::string::npos) {
		return ".";
	}
	if (pos == 0) {
		return "/";
	}
	return filepath.substr(0, pos);
}

std::string JoinPath(const std::string& base_dir, const std::string& relative) {
	if (relative.empty()) {
		return base_dir;
	}
	if (IsAbsolutePath(relative)) {
		return relative;
	}
	std::string rel = relative;
	while (!rel.empty()) {
		if (rel == ".") {
			rel.clear();
			break;
		}
		if (rel.compare(0, 2, "./") == 0) {
			rel.erase(0, 2);
			continue;
		}
		break;
	}
	if (rel.empty()) {
		return base_dir;
	}
	if (base_dir.empty() || base_dir == ".") {
		return rel;
	}
	if (base_dir.back() == '/') {
		return base_dir + rel;
	}
	return base_dir + "/" + rel;
}

std::string CanonicalizePathIfExists(const std::string& path) {
	if (path.empty()) {
		return path;
	}
	char resolved[PATH_MAX];
	if (realpath(path.c_str(), resolved) != nullptr) {
		return std::string(resolved);
	}
	return path;
}

std::string AbsPathFromCwd(const std::string& path) {
	if (path.empty() || IsAbsolutePath(path)) {
		return CanonicalizePathIfExists(path);
	}
	char cwd[PATH_MAX];
	if (getcwd(cwd, sizeof(cwd)) == nullptr) {
		return path;
	}
	return CanonicalizePathIfExists(JoinPath(cwd, path));
}

}  // namespace

void ConfigParser::SetConfigFilePath(const std::string& config_file_path) {
	config_file_path_ = AbsPathFromCwd(config_file_path);
	config_file_dir_ = GetParentDirectory(config_file_path_);
}

void ConfigParser::LoadFromFile(const std::string& config_file) {
	std::ifstream file(config_file);
	if (!file.is_open()) {
		std::cerr << "[ConfigParser] ERROR: 无法打开配置文件: " << config_file << std::endl;
		throw std::runtime_error("Failed to open file " + config_file);
	}

	std::ostringstream oss;
	oss << file.rdbuf();
	file.close();
	SetConfigFilePath(config_file);
	LoadFromString(oss.str());
}

void ConfigParser::LoadFromString(const std::string& json_str) {
	try {
		json_content_ = json::parse(json_str);
	} catch (const json::exception& e) {
		std::cerr << "[ConfigParser] ERROR: JSON读取失败: " << e.what() << std::endl;
		throw e;
	}
}

void ConfigParser::LoadFromJson(const json& json_content) {
	try {
		json_content_ = json_content;
		if (json_content_.contains("config_file_path") && json_content_["config_file_path"].is_string()) {
			SetConfigFilePath(json_content_["config_file_path"].get<std::string>());
		}
	} catch (const std::exception& e) {
		std::cerr << "[ConfigParser] ERROR: JSON读取失败: " << e.what() << std::endl;
		throw e;
	}
}

void ConfigParser::ParseConfig() { ParseConfig("19491001", 10); }

void ConfigParser::ParseConfig(const std::string& date, const int thread_num) {
	date_ = date;
	thread_num_ = thread_num;
	try {
		ParseFactorConfig();
		ParseRunningConfig();
		// 本地因子可执行程序：仅解析因子与运行项，不走 models_config / 模型线程瓜分
		AllocateThreadsForFactors();
	} catch (const json::exception& e) {
		std::cerr << "[ConfigParser] ERROR: JSON解析发生错误 json::exception: " << e.what() << std::endl;
		throw e;
	} catch (const std::exception& e) {
		std::cerr << "[ConfigParser] ERROR: JSON解析发生错误 std::exception: " << e.what() << std::endl;
		throw e;
	}
}

void ConfigParser::SetRootPathForEv(const std::string& root_path) {
	config_.factor_ev.folder_path = root_path + "/factor_ev";
	for (size_t i = 0; i < config_.model_info_list.size(); ++i) {
		// 模型 ev 文件夹路径，添加模型索引，防止同名但不同配置的模型ev文件夹名称重复
		config_.model_info_list[i].ev.folder_path =
		    root_path + "/model_ev_" + std::to_string(i) + "_" + config_.model_info_list[i].name;
	}
}

void ConfigParser::UpdateEvFolderPath(int ev_sid, const std::string& folder_path) {
	// 只有检测到 C类EV 时，才会调用本函数
	// 找到含有 ev_sid 的依赖信息，更新其 EV 文件夹路径
	bool found = false;
	for (auto sid : config_.factor_ev.sid) {
		if (sid == ev_sid) {
			found = true;
			break;
		}
	}
	if (found) {
		config_.factor_ev.folder_path = folder_path;
		if (config_.factor_ev.sid.size() > 1) {
			std::cerr << "[ConfigParser] WARNING: 因子同时依赖多个EV，且含有C类EV，解析冲突，删除其余EV，只保留C类EV("
			          << ev_sid << ")"
			          << ", 路径更新为: " << folder_path << std::endl;
		}
	}
	for (size_t i = 0; i < config_.model_info_list.size(); ++i) {
		found = false;
		for (auto sid : config_.model_info_list[i].ev.sid) {
			if (sid == ev_sid) {
				found = true;
				break;
			}
		}
		if (found) {
			config_.model_info_list[i].ev.folder_path = folder_path;
			if (config_.model_info_list[i].ev.sid.size() > 1) {
				std::cerr << "[ConfigParser] WARNING: 模型(" << config_.model_info_list[i].name
				          << ")同时依赖多个EV，且含有C类EV，解析冲突，删除其余EV，只保留C类EV(" << ev_sid << ")"
				          << ", 路径更新为: " << folder_path << std::endl;
			}
		}
	}
}

void ConfigParser::GetEv(std::vector<EvInfo>& ev_list) {
	ev_list.push_back(config_.factor_ev);
	for (size_t i = 0; i < config_.model_info_list.size(); ++i) {
		ev_list.push_back(config_.model_info_list[i].ev);
	}
}

void ConfigParser::ParseFactorConfig() {
	config_.enabled_factor_sets.clear();
	config_.enabled_factor_set_names.clear();

	if (!json_content_.contains("factors_config")) return;

	const auto& cfg = json_content_["factors_config"];

	config_.factor_omp_num_threads = cfg.value("omp_num_threads", 16);

	/* ---------- ev ---------- */
	if (cfg.contains("ev")) {
		const auto& ev_json = cfg["ev"];
		EvInfo ev_info;

		if (ev_json.is_number_integer()) {  // 1. 单数字 -> vector<int>{该数字}
			ev_info.sid = {ev_json.get<int>()};
		} else if (ev_json.is_array()) {  // 2. 数组 -> vector<int>
			ev_info.sid = ev_json.get<std::vector<int>>();
		} else if (ev_json.is_string()) {  // 3. 字符串 -> folder_path
			ev_info.folder_path = ResolveConfigRelativePath(ev_json.get<std::string>());
		}
		// 可选：若出现其他类型，可在此加 warning / error

		config_.factor_ev = std::move(ev_info);
	}

	/* ---------- send_times ---------- */
	if (cfg.contains("send_times")) {
		ParseTimesInfo(cfg["send_times"], config_.send_times);
	}

	/* ---------- ev_code_file ---------- */
	// 仅做占位符展开：路径相对 EV 根目录解析，不按本 config 文件所在目录做相对路径拼接
	if (cfg.contains("ev_code_file")) {
		config_.ev_code_file = ResolvePlaceholder(cfg["ev_code_file"].get<std::string>());
	}

	/* ---------- factor_sets ---------- */
	if (cfg.contains("factor_sets") && cfg["factor_sets"].is_array()) {
		for (const auto& factor_set : cfg["factor_sets"]) {
			FactorSetInfo fs_info;
			fs_info.name = factor_set["name"].get<std::string>();
			fs_info.enabled = factor_set.value("enabled", true);
			if (factor_set.contains("trigger_points")) {
				ParseTimesInfo(factor_set["trigger_points"], fs_info.trigger_points);
			}
			// 如果因子启用，才添加到因子集列表中
			if (fs_info.enabled) {
				config_.enabled_factor_sets.push_back(fs_info);
				config_.enabled_factor_set_names.push_back(fs_info.name);
			}
		}
	}

	/* ---------- save_info ---------- */
	if (cfg.contains("save_info")) {
		ParseFactorSaveInfo(cfg["save_info"]);
	}

	/* ---------- thread_num ---------- */
	if (cfg.contains("factor_sets") && cfg["factor_sets"].is_array()) {
		config_.thread_info.factors_request = cfg.value("thread_num", 0);
	}
}

void ConfigParser::ParseModelConfig() {
	config_.model_info_list.clear();
	config_.model_names.clear();
	if (!json_content_.contains("models_config")) return;

	const auto& cfg = json_content_["models_config"];

	const int models_omp_default = cfg.value("omp_num_threads", 16);

	// models_config 顶层 elapsed：合并发送路径覆盖 SDP 耗时；-1 表示不覆盖（与单模型项内 elapsed 独立）。
	config_.sdp_merge_overwrite_elapsed_us = -1;
	if (cfg.contains("elapsed") && cfg["elapsed"].is_number_integer()) {
		config_.sdp_merge_overwrite_elapsed_us = static_cast<long>(cfg["elapsed"].get<int64_t>());
	}

	int model_id = 0;

	/* ---------- models ---------- */
	if (cfg.contains("models") && cfg["models"].is_array()) {
		for (const auto& model_json : cfg["models"]) {
			// 跳过禁用的模型
			if (!model_json.value("enabled", false)) continue;
			ModelInfo model_info;
			model_info.model_id = model_id++;
			model_info.name = model_json["name"].get<std::string>();
			model_info.extra_threads_request = model_json.value("extra_threads_request", 0);
			model_info.extra_threads_allocated = 0;
			model_info.omp_num_threads = model_json.value("omp_num_threads", models_omp_default);
			model_info.runtime_config = model_json.value("runtime_config", nlohmann::json());
			model_info.sdp_overwrite_elapsed_us = -1;
			if (model_json.contains("elapsed") && model_json["elapsed"].is_number_integer()) {
				model_info.sdp_overwrite_elapsed_us = static_cast<long>(model_json["elapsed"].get<int64_t>());
			}
			/* ---------- ev ---------- */
			if (model_json.contains("ev")) {
				const auto& ev_json = model_json["ev"];
				EvInfo ev_info;

				if (ev_json.is_number_integer()) {  // 1. 单数字 -> vector<int>{该数字}
					ev_info.sid = {ev_json.get<int>()};
				} else if (ev_json.is_array()) {  // 2. 数组 -> vector<int>
					ev_info.sid = ev_json.get<std::vector<int>>();
				} else if (ev_json.is_string()) {  // 3. 字符串 -> folder_path
					ev_info.folder_path = ResolveConfigRelativePath(ev_json.get<std::string>());
				}
				// 可选：若出现其他类型，可在此加 warning / error

				model_info.ev = std::move(ev_info);
			}
			config_.model_info_list.push_back(model_info);
			config_.model_names.push_back(model_info.name);
		}
	}

	/* ---------- save_info ---------- */
	if (cfg.contains("save_info")) {
		ParseModelSaveInfo(cfg["save_info"]);
	}

	/* ---------- thread_num ---------- */
	if (cfg.contains("models") && cfg["models"].is_array()) {
		config_.thread_info.models_request = cfg.value("thread_num", 0);
	}
}

void ConfigParser::ParseRunningConfig() {
	// app_factor：固定为本地因子落盘模式（与 app_live 可切换 local_simulate 区分）
	config_.local_simulate = true;
	config_.skip_missed_time_triggers = json_content_.value("skip_missed_time_triggers", false);
	config_.disable_sdp_timer = json_content_.value("disable_sdp_timer", false);
	config_.warm_library_memory_size = json_content_.value("warm_library_memory_size", -1);
}

void ConfigParser::AllocateThreadsForFactors() {
	config_.model_info_list.clear();
	config_.model_names.clear();
	config_.thread_info.models_request = 0;
	config_.thread_info.models_allocated = 0;
	config_.thread_info.factors_allocated = NonNegativeThreadCount(config_.thread_info.factors_request);
	std::cout << "[ConfigParser] factor-only executable: factors threads request: "
	          << config_.thread_info.factors_request
	          << ", allocated: " << config_.thread_info.factors_allocated << std::endl;
}

void ConfigParser::AllocateThreads() {
	config_.thread_info.factors_allocated = NonNegativeThreadCount(config_.thread_info.factors_request);
	config_.thread_info.models_allocated = NonNegativeThreadCount(config_.thread_info.models_request);
	std::cout << "[ConfigParser] factors threads request: " << config_.thread_info.factors_request
	          << ", allocated: " << config_.thread_info.factors_allocated << std::endl;
	std::cout << "[ConfigParser] models threads request: " << config_.thread_info.models_request
	          << ", allocated: " << config_.thread_info.models_allocated << std::endl;
	for (auto& model_info : config_.model_info_list) {
		model_info.extra_threads_allocated = NonNegativeThreadCount(static_cast<int>(model_info.extra_threads_request));
	}
}

int ConfigParser::GetTotalAllocatedThreads() const {
	// models_allocated 为模型侧线程池总量（已涵盖各模型 extra_threads_allocated 的预算），不与 extra 重复累加。
	return config_.thread_info.factors_allocated + config_.thread_info.models_allocated;
}

void ConfigParser::ParseModelSaveInfo(const json& json_cfg) {
#if defined(ENABLE_APP_LIVE)
	config_.save_info.save_model_stats = json_cfg.value("save_stats", false);
#else
	config_.save_info.save_model_stats = json_cfg.value("save_stats", true);
#endif
	config_.save_info.model_stats_dir = ResolveConfigRelativePath("./");

	if (json_cfg.contains("file_path")) {
		std::string file_path = ResolveConfigRelativePath(json_cfg.value("file_path", ""));
		auto pos = file_path.find_last_of("/\\");
		config_.save_info.models_dir = (pos == std::string::npos) ? "./" : file_path.substr(0, pos);
		config_.save_info.models_name =
		    EnsureExtension((pos == std::string::npos) ? file_path : file_path.substr(pos + 1), ".h5");
		config_.save_info.models_save_file = config_.save_info.models_dir + "/" + config_.save_info.models_name;
	} else {
		config_.save_info.models_dir = ResolveConfigRelativePath(json_cfg.value("dir", "./"));
		config_.save_info.models_name = EnsureExtension(ResolvePlaceholder(json_cfg.value("name", "models.h5")), ".h5");
		config_.save_info.models_save_file = config_.save_info.models_dir + "/" + config_.save_info.models_name;
	}
	config_.save_info.models_data_dataset_name = json_cfg.value("data_dataset_name", "data");
	std::cout << "[ConfigParser] models save data dataset name: " << config_.save_info.models_data_dataset_name
	          << std::endl;

#if defined(ENABLE_STRATEGY_SESSION_MODE_CONTINUOUS)
	// open5m 策略默认不合并
	config_.save_info.merge = json_cfg.value("merge", false);
#else
	// 其他策略默认合并
	config_.save_info.merge = json_cfg.value("merge", true);
#endif
}

void ConfigParser::ParseFactorSaveInfo(const json& json_cfg) {
#if defined(ENABLE_APP_LIVE)
	config_.save_info.save_factor_stats = json_cfg.value("save_stats", false);
#else
	config_.save_info.save_factor_stats = json_cfg.value("save_stats", true);
#endif
	config_.save_info.factor_stats_dir = ResolveConfigRelativePath("./");

	if (json_cfg.contains("file_path")) {
		std::string file_path = ResolveConfigRelativePath(json_cfg.value("file_path", ""));
		auto pos = file_path.find_last_of("/\\");
		config_.save_info.factors_dir = (pos == std::string::npos) ? "./" : file_path.substr(0, pos);
		config_.save_info.factors_name =
		    EnsureExtension((pos == std::string::npos) ? file_path : file_path.substr(pos + 1), ".h5");
		config_.save_info.factors_save_file = config_.save_info.factors_dir + "/" + config_.save_info.factors_name;
	} else {
		config_.save_info.factors_dir = ResolveConfigRelativePath(json_cfg.value("dir", "./"));
		config_.save_info.factors_name =
		    EnsureExtension(ResolvePlaceholder(json_cfg.value("name", "factors.h5")), ".h5");
		config_.save_info.factors_save_file = config_.save_info.factors_dir + "/" + config_.save_info.factors_name;
	}
	config_.save_info.factors_data_dataset_name = json_cfg.value("data_dataset_name", "factordata");
	std::cout << "[ConfigParser] factors save data dataset name: " << config_.save_info.factors_data_dataset_name
	          << std::endl;

	// 解析保存时间
	if (json_cfg.contains("save_times")) {
		ParseTimesInfo(json_cfg["save_times"], config_.save_info.save_times);
	}
}

void ConfigParser::ParseTimesInfo(const json& json_cfg, TimesInfo& times_info) {
	times_info.all_times.clear();
	if (json_cfg.is_array()) {
		times_info.all_times = json_cfg.get<std::vector<int>>();
		if (!times_info.all_times.empty()) {
			// 将秒时间戳转换为毫秒时间戳
			int multiplier = times_info.all_times[0] <= 240000 ? 1000 : 1;
			for (size_t i = 0; i < times_info.all_times.size(); i++) {
				times_info.all_times[i] *= multiplier;
			}
		}
		// 排序去重
		std::sort(times_info.all_times.begin(), times_info.all_times.end());
		times_info.all_times.erase(
		    std::unique(times_info.all_times.begin(), times_info.all_times.end()), times_info.all_times.end());
	} else if (json_cfg.is_object()) {
		times_info.start = json_cfg["start"].get<int>();
		int multiplier = times_info.start <= 240000 ? 1000 : 1;
		times_info.start *= multiplier;
		times_info.end = json_cfg["end"].get<int>();
		times_info.end *= multiplier;
		times_info.interval = json_cfg["interval"].get<int>();
		times_info.interval *= multiplier;
		if (json_cfg.contains("add")) {
			if (json_cfg["add"].is_array()) {
				times_info.add = json_cfg["add"].get<std::vector<int>>();
			} else if (json_cfg["add"].is_number_integer()) {
				times_info.add = {json_cfg["add"].get<int>()};
			} else {
				std::cerr << "[ConfigParser] ERROR: TimesInfo.add 必须是 整数列表 或 整数" << std::endl;
				throw std::runtime_error(
				    std::string("TypeError: TimesInfo.add cannot be ") + json_cfg["add"].type_name());
			}
			for (size_t i = 0; i < times_info.add.size(); i++) {
				times_info.add[i] *= multiplier;
			}
		}
		if (json_cfg.contains("skip")) {
			if (json_cfg["skip"].is_array()) {
				times_info.skip = json_cfg["skip"].get<std::vector<int>>();
			} else if (json_cfg["skip"].is_number_integer()) {
				times_info.skip = {json_cfg["skip"].get<int>()};
			} else {
				std::cerr << "[ConfigParser] ERROR: TimesInfo.skip 必须是 整数列表 或 整数" << std::endl;
				throw std::runtime_error(
				    std::string("TypeError: TimesInfo.skip cannot be ") + json_cfg["skip"].type_name());
			}
			for (size_t i = 0; i < times_info.skip.size(); i++) {
				times_info.skip[i] *= multiplier;
			}
		}
	} else {
		std::cerr << "[ConfigParser] ERROR: TimesInfo 必须是 整数列表 或 对象" << std::endl;
		throw std::runtime_error(std::string("TypeError: TimesInfo cannot be ") + json_cfg.type_name());
	}
	times_info.set = true;
}

std::string ConfigParser::ResolvePlaceholder(const std::string& filename) const {
	std::string result = filename;
	std::string placeholder = "[DATE]";
	std::string replacement = date_;

	// 查找并替换占位符
	size_t pos = 0;
	while ((pos = result.find(placeholder, pos)) != std::string::npos) {
		result.replace(pos, placeholder.length(), replacement);
		pos += replacement.length();  // 移动到替换后的位置
	}

	return result;
}

std::string ConfigParser::ResolveConfigRelativePath(const std::string& path) const {
	std::string result = ResolvePlaceholder(path);
	if (!result.empty() && !IsAbsolutePath(result) && IsExplicitConfigRelativePath(result) &&
	    !config_file_dir_.empty()) {
		result = JoinPath(config_file_dir_, result);
		return CanonicalizePathIfExists(result);
	}
	return result;
}

void ConfigParser::SetRunMode(int sdp_init_type) {
	// 与平台 my_st_init_v3 首参 type、SDPHandler::run_mode 同源，对应 strategy_interface.h 中 CONFIG_TYPE。
	config_.run_mode = sdp_init_type;
	// SIMULATION_CONFIG 当前取值为 1；仅模拟路径允许用 JSON「elapsed」覆盖 SDP 发送耗时。
	constexpr int k_simulation_config_type = 1;
	if (config_.run_mode != k_simulation_config_type) {
		// 实盘等非模拟配置：禁用单模型与合并路径的人为 elapsed，走 send_factor_matrix 内原有 TSC 计时。
		config_.sdp_merge_overwrite_elapsed_us = -1;
		for (auto& m : config_.model_info_list) {
			m.sdp_overwrite_elapsed_us = -1;
		}
	}
}

}  // namespace config