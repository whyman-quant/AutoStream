#include <unistd.h>

#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "comm/general.h"
#include "comm/print.hpp"
#include "comm/tools.h"
#include "comm/velapex/chrono_utils.h"
#include "comm/velatools/build_info.h"
#include "comm/velatools/hierarchy_tree_formatter.h"
#include "comm/velatools/runtime_info.h"
#include "config/config_parser.h"
#include "data/constant_space.h"
#include "data/data_loader.h"
#include "engine/factor_calculation_engine.h"
#include "factors/_comm/factor_entry_registry.h"

using constant_space::kBackupCodeListFolderPath;
using constant_space::kDefaultCodeFileName;
using constant_space::kDefaultEvCodeFileName;
using constant_space::kDefaultEvFolderPath;

// 解析命令行参数。
// 参数 argc：参数数量，来自 main 的 argc。
// 参数 argv：参数数组，来自 main 的 argv。
//
// 支持的命令行参数格式：
//   date=20230728       // 设置日期
//   thread_num=10 / thread=10   // 线程数（别名 thread）
//   config_file=x.json / config=x.json // 配置文件（别名 config）
//   stock=000001        // 股票代码, all 或 具体个股代码
//
// 使用示例：
//   ./main date=20230728 thread_num=8 config_file=test.json stock=600000
//
// 另支持（任意位置出现即生效，处理后立即退出）：
//   --help / -h / help      打印用法说明
//   --version / -v / version 打印构建信息与已注册因子列表
namespace {

bool ArgIsHelp(const char* a) {
	return a && (std::strcmp(a, "--help") == 0 || std::strcmp(a, "-h") == 0 || std::strcmp(a, "help") == 0);
}

bool ArgIsVersion(const char* a) {
	return a && (std::strcmp(a, "--version") == 0 || std::strcmp(a, "-v") == 0 || std::strcmp(a, "version") == 0);
}

void PrintFactorCliUsage(std::ostream& os) {
	os << "local-factor (app_factor/main)\n\n";
	os << "用法:\n";
	os << "  ./main [键=值 ...]\n\n";
	os << "parse_command_line 支持的参数（均为 key=value）：\n";
	os << "  date           交易日 YYYYMMDD，例如 date=20230728\n";
	os << "  thread_num     命令行总线程数（别名 thread）；若 stock 不是 all 将置为 2；"
	      "ParseConfig 用于对账提示，因子线程数以 JSON factors_config.thread_num 为准\n";
	os << "  config_file    配置文件路径（别名 config）；缺省按时段尝试 config_factor_*.json、config_local.json\n";
	os << "  stock          股票：all 或 6 位代码，例如 stock=600000\n";
	os << "说明: 本地因子入口固定 local_simulate（解析后恒为保存因子至 H5），无需在 JSON 中配置。\n\n";
	os << "示例:\n";
	os << "  ./main date=20230728 thread_num=8 config_file=config_factor.json stock=all\n\n";
	os << "其它:\n";
	os << "  --help, -h, help              打印本说明并退出\n";
	os << "  --version, -v, version        打印构建信息与已注册因子并退出\n";
}

}  // namespace

void parse_command_line(
    int argc, char** argv, int& date, int& thread_num, std::string& config_file, std::string& stock) {
	for (int i = 1; i < argc; i++) {
		std::vector<std::string> slist;
		tools_SplitString(argv[i], "=", slist);
		if (slist.size() < 2) continue;  // 确保有键值对
		if (slist[0] == "date")
			date = std::atoi(slist[1].c_str());
		else if (slist[0] == "thread_num" || slist[0] == "thread")
			thread_num = std::atoi(slist[1].c_str());
		else if (slist[0] == "config_file" || slist[0] == "config")
			config_file = slist[1];
		else if (slist[0] == "stock")
			stock = slist[1];
	}

	if (stock != "all") {
		thread_num = 2;
	}

	// 输出解析结果
	WLOG(TO_STRING("[Main] date        =", date));
	WLOG(TO_STRING("[Main] thread_num  =", thread_num));
	WLOG(TO_STRING("[Main] stock       =", stock));
	WLOG(TO_STRING("[Main] config_file =", config_file));
}

int main(int argc, char** argv) {
	for (int i = 1; i < argc; ++i) {
		if (ArgIsHelp(argv[i])) {
			PrintFactorCliUsage(std::cout);
			return 0;
		}
		if (ArgIsVersion(argv[i])) {
			velatools::build_info::PrintBuildInfo();
			factors::comm::DisplayRegisteredFactors();
			return 0;
		}
	}

	WLOG(velatools::build_info::GetBuildInfo());
	WLOG(velatools::runtime_info::GetRuntimeInfo());
#ifdef ENABLE_TIME_STATS
	// \033[ 是 ANSI 转义码的开始，后面跟一个数字表示颜色，用于将后续文本设置为对应颜色
	// 常见颜色的 ANSI 转义码：
	// \033[0m   —— 重置颜色（恢复默认），避免影响后续输出
	// \033[30m  —— 黑色    \033[31m  —— 红色    \033[32m  —— 绿色    \033[33m  —— 黄色
	// \033[34m  —— 蓝色    \033[35m  —— 紫色    \033[36m  —— 青色    \033[37m  —— 白色
	std::cout << "\033[32m" << "--- ENABLE_TIME_STATS is ON ---" << "\033[0m" << std::endl;
#endif
	// 使用 RDTSC 计时器进行高精度、低开销的计时（演示示例）
	// 注意：对于整体程序计时（秒级或更长），使用 RDTSC 其实没有必要
	// 因为程序运行时间很长（通常几秒到几分钟），std::chrono::high_resolution_clock::now()
	// 或 gettimeofday 的开销（约 50-200 纳秒）完全可以忽略不计
	// 这里使用 RDTSC 主要是作为演示示例，展示如何使用 RdtscTimer 进行高精度计时
	//
	// RDTSC (Read Time-Stamp Counter) 是 CPU 指令，开销极小（约 1-10 纳秒）
	// 相比 std::chrono::high_resolution_clock::now()（约 50-200 纳秒）和 gettimeofday（约 20-100 纳秒）快得多
	// RDTSC 更适合用于微秒级或纳秒级的短时间高精度计时场景
	velapex::chrono_utils::RdtscTimer rdtsc_timer;
	// 在开始计时前获取 scaler，避免 GetScaler() 的开销影响计时精度
	// GetScaler() 第一次调用时会测量 CPU 频率（约几毫秒），之后会缓存结果
	// scaler 是 CPU 频率的倒数（单位：秒/cycle），用于将 cycles 转换为秒
	// 例如：如果 CPU 频率是 2.4 GHz，则 scaler = 1.0 / 2400000000 ≈ 4.17e-10 秒/cycle
	double cycles_to_us_scaler = velapex::chrono_utils::RdtscTimer::GetScaler();
	// 开始计时
	uint64_t main_start_cycles = rdtsc_timer();

#if defined(ENABLE_STRATEGY_SESSION_MODE_OPEN)
	WLOG("[Main] ========================== HF Open Factor Framework ==========================");
#elif defined(ENABLE_STRATEGY_SESSION_MODE_CLOSE)
	WLOG("[Main] ========================== HF Close Factor Framework =========================");
#elif defined(ENABLE_STRATEGY_SESSION_MODE_CONTINUOUS)
	WLOG("[Main] ========================= HF Open5m Factor Framework =========================");
#else
	WLOG("[Main] ===================== HF Cross-Section  Factor Framework =====================");
#endif

	WLOG("[Main] 程序开始运行");

	/* 解析命令行参数 */

	int date = 20220222;
	int thread_num = 10;

	std::vector<std::string> try_config_files;
#if defined(ENABLE_STRATEGY_SESSION_MODE_OPEN)
	try_config_files.push_back("config_factor_open.json");
#elif defined(ENABLE_STRATEGY_SESSION_MODE_CLOSE)
	try_config_files.push_back("config_factor_close.json");
#elif defined(ENABLE_STRATEGY_SESSION_MODE_CONTINUOUS)
	try_config_files.push_back("config_factor_continuous.json");
	try_config_files.push_back("config_factor_open5m.json");
#endif
	try_config_files.push_back("config_local.json");
	try_config_files.push_back("config_factor.json");
	std::string config_file = "EMPTY_CONFIG_FILE";
	for (const auto& try_config_file : try_config_files) {
		if (tools_IsFileExist(try_config_file.c_str())) {
			config_file = try_config_file;
			break;
		}
	}

	std::string stock = "all";  // "all","000001"
	parse_command_line(argc, argv, date, thread_num, config_file, stock);

	// -----------------------------------
	// 线程预算（先于 ParseConfig）：CLI 总数 − 引擎固定线程 − Main 主线程，剩余交给 ParseConfig / Init 分配
	// -----------------------------------
	int total_thread_num = thread_num;
	int fixed_thread_num = FactorCalculationEngine::GetFixedThreadCount();
	int allocable_thread_num = total_thread_num - fixed_thread_num - 1;
	if (allocable_thread_num < 0) {
		allocable_thread_num = 0;
		WLOG(TO_STRING("[Main] WARNING: thread_num=", thread_num,
		    " 过小，无法在预留固定线程与 Main 后为因子线程预算分配；ParseConfig / Init 将按 0 处理。"));
	}
	WLOG(TO_STRING("[Main] Thread Info:", "total:", total_thread_num, "| fixed:", fixed_thread_num,
	    "| allocable:", allocable_thread_num));
	for (const auto& info : FactorCalculationEngine::GetThreadInfoList()) {
		if (info.count != 0) {
			WLOG(TO_STRING("[Main] Thread Info: -", info.name, ", source:", info.source, ", count:", info.count));
		} else {
			WLOG(TO_STRING(
			    "[Main] Thread Info: -", info.name, ", source:", info.source, ", determined by:", info.detail));
		}
	}

	/* 加载配置文件 */

	config::ConfigParser config_parser;
	config_parser.LoadFromFile(config_file);
	config_parser.ParseConfig(std::to_string(date), allocable_thread_num);
	config::ConfigData config = config_parser.GetConfig();
	const int config_thread_total = config_parser.GetTotalAllocatedThreads();
	if (config_thread_total > allocable_thread_num) {
		WERR(TO_STRING("[Main] JSON 配置线程数合计=", config_thread_total,
		    " 大于可分配线程预算=", allocable_thread_num,
		    "，运行时可能出现多线程抢占同一 CPU 核心"));
	}
	WLOG(TO_STRING("[Main] 已成功加载配置文件", config_file));
	WLOG(TO_STRING("[Main] data_dataset_name summary: factor=", config.save_info.factors_data_dataset_name,
	    ", model=", config.save_info.models_data_dataset_name));

	if (config.factor_ev.folder_path.empty()) {
		config.factor_ev.folder_path = std::string(kDefaultEvFolderPath);
		WLOG(TO_STRING("[Main] 未设置ev，使用默认路径", config.factor_ev.folder_path));
	}

	if (config.ev_code_file.empty()) {
		config.ev_code_file = std::to_string(date) + "/" + std::string(kDefaultEvCodeFileName);
		WLOG(TO_STRING("[Main] 未设置ev_code_file，使用默认文件名", config.ev_code_file));
	}

	/* 从 EV 文件中获取股票代码 */
	// 默认已经将 code list 的文件也一并放入 ev_path 中
	// 采取防御性编程，兼容之前股票代码文件在单独的路径下的情况；数据集顺序：优先 codelist，其次 data
	std::vector<std::string> try_files_for_codes = {config.factor_ev.folder_path + "/" + config.ev_code_file,
	    std::string(kBackupCodeListFolderPath) + "/" + std::to_string(date) + "/per1day/" +
	        std::string(kDefaultCodeFileName),
	    std::string(kBackupCodeListFolderPath) + "/" + std::to_string(date) + "/" + std::string(kDefaultCodeFileName)};
	std::vector<std::string> codes_all;
	bool flag_get_codes_from_file = false;
	for (const auto& ev_file_for_codes : try_files_for_codes) {
		try {
			codes_all.clear();
			if (get_codes_from_file(ev_file_for_codes, "codelist", codes_all)) {
				flag_get_codes_from_file = true;
				WLOG(TO_STRING("[Main] 从EV文件中获取股票代码成功 (dataset=codelist):", ev_file_for_codes));
				break;
			}
			codes_all.clear();
			if (get_codes_from_file(ev_file_for_codes, "data", codes_all)) {
				flag_get_codes_from_file = true;
				WLOG(TO_STRING("[Main] 从EV文件中获取股票代码成功 (dataset=data):", ev_file_for_codes));
				break;
			}
		} catch (const std::exception& e) {
			continue;
		}
	}
	if (!flag_get_codes_from_file) {
		WLOG(TO_STRING("[Main] 无法从以下任何一个文件中获取股票代码:", try_files_for_codes));
		return -1;
	}

	// 如果指定了单只股票，则只保留该股票
	if (stock != "all") {
		codes_all = std::vector<std::string>({stock});
	}

	// 只保留以0、3、6开头的股票代码，并截取前6位
	std::vector<std::string> codes_tmp;
	for (const auto& code : codes_all) {
		if (code.substr(0, 1) == "0" || code.substr(0, 1) == "3" || code.substr(0, 1) == "6") {
			codes_tmp.push_back(code.substr(0, 6));
		}
	}
	codes_all = codes_tmp;

	// 输出股票代码数量
	WLOG(TO_STRING("[Main] codes_all.size() =", codes_all.size()));

	/* 核心运行部分 */

	DataLoader data_loader;  // 使用DataLoader数据加载器，管理行情数据的读取/整理/分发
	data_loader.LoadRawData(date, stock);  // 加载行情数据
	data_loader.MergeAndSortData();        // 合并排序行情数据

	{
		FactorCalculationEngine factor_calc_engine;
		factor_calc_engine.Init(
		    date, codes_all, config.thread_info.factors_allocated, config);

		factor_calc_engine.Start();   // 启动各个计算线程和扫描线程
		factor_calc_engine.WarmUp();  // 与 app_live 一致：预热结果池，降低首轮缺页

		// 展示运行时线程树形结构，便于快速确认引擎内部线程层次。
		{
			std::vector<std::pair<int, std::string>> runtime_thread_tree_lines;
			runtime_thread_tree_lines.push_back(std::make_pair(0, "Main Thread"));
			auto factor_runtime_threads = factor_calc_engine.CollectRuntimeThreadTreeLines();
			for (const auto& line : factor_runtime_threads) {
				runtime_thread_tree_lines.push_back(std::make_pair(line.first + 1, line.second));
			}
			WLOG("[Main] ============ Runtime Thread Tree ============");
			WLOG(TO_STRING("[Main] runtime thread count:", runtime_thread_tree_lines.size()));
			auto formatted_lines =
			    velatools::hierarchy_tree_formatter::FormatHierarchyTreeLines(runtime_thread_tree_lines);
			for (const auto& line : formatted_lines) {
				WLOG(TO_STRING("[Main] ", line));
			}
			WLOG("[Main] =============================================");
		}

		data_loader.SetCalculationEngine(factor_calc_engine);
		data_loader.DistributeData();
		factor_calc_engine.Stop();
	}

	// 结束计时
	uint64_t main_finish_cycles = rdtsc_timer();
	double main_cost_time_seconds =
	    static_cast<double>(main_finish_cycles - main_start_cycles) * cycles_to_us_scaler / 1e6;
	// 将耗时格式化为字符串，保留 9 位小数，使用 std::fixed 和 std::setprecision(9) 确保输出固定小数位数
	std::ostringstream oss;
	oss << std::fixed << std::setprecision(9) << main_cost_time_seconds;
	WLOG(TO_STRING("[Main] 程序运行总耗时:", oss.str(), "s"));
	return 0;
}
