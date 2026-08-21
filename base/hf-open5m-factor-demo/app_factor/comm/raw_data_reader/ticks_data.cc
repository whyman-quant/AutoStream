// TicksData：构造/析构、FormatPath、MC、SetMydataPath / Get*（BackendTarget）调度；
// 直读实现在 ticks_data_io_cpp.cc、ticks_data_io_python.cc。
// 编译宏由 Makefile 注入（见 backend_config.h 顶部）。
#include "ticks_data.h"
#include "backend_config.h"
#include "string.h"
#include "stdlib.h"
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <utility>

#if defined(READER_BACKEND_PYTHON_ONLY) || defined(READER_BACKEND_BOTH)
#include <cstdio>
#endif

void TicksData::OrderTransRelativeArtifacts(int date, const std::string &symbol, std::string &rel_myzst,
					    std::string &rel_npq) const {
	NpqOrderTransRelativeArtifacts(path_config_, date, symbol, rel_myzst, rel_npq);
}

// ---------- 构造 / 析构 ----------

#if defined(READER_BACKEND_CPP_ONLY)

TicksData::TicksData() {}

TicksData::~TicksData() {}

// Python-only 与 both 在构造/析构阶段一致：都需要初始化并持有 my.data.quote.data。
#elif defined(READER_BACKEND_PYTHON_ONLY) || defined(READER_BACKEND_BOTH)

TicksData::TicksData() {
	m = NULL;
	data_module_ = NULL;
	if (!Py_IsInitialized()) {
		// 进程级解释器：只在未初始化时初始化，避免重复初始化/销毁带来的不稳定。
		Py_Initialize();
	}

	m = PyImport_ImportModule("my.data.quote");
	if (m == NULL) {
		printf("import my.data.quote module error !");
		PyErr_Print();
		return;
	}
	data_module_ = PyObject_GetAttrString(m, "data");
	if (!data_module_) {
		printf("data module import error");
		PyErr_Print();
		Py_DECREF(m);
		m = NULL;
		return;
	}
}

TicksData::~TicksData() {
	// 这里不做 Py_Finalize()：测试/示例里一个进程会多次构造 TicksData，
	// 仅释放本实例持有的对象引用，解释器生命周期交由进程结束时回收。
	Py_XDECREF(data_module_);
	Py_XDECREF(m);
	data_module_ = NULL;
	m = NULL;
}

#else
#error ticks_data.cc: expected exactly one of READER_BACKEND_CPP_ONLY, READER_BACKEND_PYTHON_ONLY, READER_BACKEND_BOTH
#endif


void TicksData::SetLocalPath(std::string path) {
	path_config_.SetLocalRoot(std::move(path));
}

std::string TicksData::FormatPath(int date, std::string symbol, int data_type, int mi_type) {
	std::string rel_z, rel_npq;
	// 对外统一 data_type/mi_type 输入，内部按 Python quote 的路径规则组装相对路径，
	// 再交由 DataPathConfig 解析为可读绝对路径。
	switch (data_type) {
	case TICK:
		BuildPythonQuoteStyleRelativePaths(path_config_, date, 209, 0, 0, symbol, false, rel_z, rel_npq);
		return path_config_.ResolvePathParseStyleRelative(rel_npq, false);
	case TRANSACTION:
		BuildPythonQuoteStyleRelativePaths(path_config_, date, 254, 0, 0, symbol, true, rel_z, rel_npq);
		return path_config_.ResolvePathParseStyleRelative(rel_npq, false);
	case ORDER:
		BuildPythonQuoteStyleRelativePaths(path_config_, date, 253, 0, 0, symbol, true, rel_z, rel_npq);
		return path_config_.ResolvePathParseStyleRelative(rel_npq, false);
	case ORDER_QUEUE:
		BuildPythonQuoteStyleRelativePaths(path_config_, date, 252, 0, 0, symbol, true, rel_z, rel_npq);
		return path_config_.ResolvePathParseStyleRelative(rel_npq, false);
	case ORDER_NEW:
		BuildPythonQuoteStyleRelativePaths(path_config_, date, 257, 0, 0, symbol, true, rel_z, rel_npq);
		return path_config_.ResolvePathParseStyleRelative(rel_npq, false);
	case TRANSACTION_NEW:
		BuildPythonQuoteStyleRelativePaths(path_config_, date, 256, 0, 0, symbol, true, rel_z, rel_npq);
		return path_config_.ResolvePathParseStyleRelative(rel_npq, false);
	case FUTURES:
		BuildPythonQuoteStyleRelativePaths(path_config_, date, mi_type, 0, 0, symbol, false, rel_z, rel_npq);
		return path_config_.ResolvePathParseStyleRelative(rel_npq, false);
	case MC: {
		const std::string rel = FillPathTemplate(std::string(npq_relative_template::kMc), date, symbol, mi_type);
		return path_config_.ResolvePathParseStyleRelative(rel, false);
	}
	case ESMC: {
		const std::string rel = FillPathTemplate(std::string(npq_relative_template::kEsmc), date, symbol, mi_type);
		return path_config_.ResolvePathParseStyleRelative(rel, false);
	}
	case ORDER_TRANS: {
		std::string rel_myzst;
		std::string rel_npq;
		NpqOrderTransRelativeArtifacts(path_config_, date, symbol, rel_myzst, rel_npq);
		return path_config_.ResolvePathParseStyleRelative(rel_npq, false);
	}
	default:
		return ResolvePathTemplate(path_config_, date, std::move(symbol), data_type, mi_type);
	}
}

// 扫 mc/esmc 二进制表，按 product 过滤出 symbol；Python 侧期货展开同样依赖本函数读盘。
// MC：Main Contract，主连/连续合约映射表落盘；与 ESMC 对应不同 mi（如 225）与记录结构。
bool TicksData::LoadSymbolsFromMc(int date, int mi_type, const std::string &product, std::vector<std::string> &symbols) {
	symbols.clear();
	int type = MC;
	if (mi_type == 225) {
		type = ESMC;
	}
	std::string data_path = FormatPath(date, "", type);
	std::ifstream is(data_path, std::ifstream::binary);
	if (!is) {
		std::cerr << "TicksData::LoadSymbolsFromMc: 无法打开「" << data_path << "」：" << std::strerror(errno) << "\n";
		return false;
	}
	int nlength = sizeof(my_mc_t);
	if (mi_type == 225) {
		nlength = sizeof(my_esmc_t);
	}

	char *buff = new char[nlength];
	memset(buff, 0, nlength);
	std::string mc_symbol;
	std::string mc_product;
	while (is.peek() != EOF)
	{
		is.read(buff, nlength);
		if (mi_type == 225) {
			my_esmc_t mc_data = *(my_esmc_t*)buff;
			mc_symbol = std::string(mc_data.symbol, 8);
			mc_product = std::string(mc_data.product, 4);
		} else {
			my_mc_t mc_data = *(my_mc_t*)buff;
			mc_symbol = std::string(mc_data.symbol, 6);
			mc_product = std::string(mc_data.product, 2);
		}

		// 字段可能无 '\0'，用 strlen 截到第一个 0 再比 product。
		size_t len = strlen(mc_product.c_str());
		if (len == product.size() && strncmp(mc_product.c_str(), product.c_str(), len) == 0) {
			symbols.push_back(mc_symbol.substr(0, strlen(mc_symbol.c_str())));
		}
	}
	delete []buff;
	is.close();
	return true;
}

bool TicksData::GetSymbolFromMc(int date, int mi_type, const std::string &product, std::vector<std::string> &symbols,
				BackendTarget backend) {
	BackendTarget b = backend;
	if (b == BackendTarget::Python) {
		std::cerr << "TicksData::GetSymbolFromMc: MC/ESMC 表仅磁盘直读；BackendTarget::Python 无独立实现，仍读磁盘。\n";
	}
	(void)b;
	return LoadSymbolsFromMc(date, mi_type, product, symbols);
}

// ---------- SetMydataPath / Get*（BackendTarget）调度：经 Get*ViaCpp / Get*ViaPython 分流 ----------

namespace {

void WarnBackend(const char *api, const char *detail) {
	std::cerr << "TicksData::" << api << ": " << detail << "\n";
}

template <typename F>
bool WarnIfPythonReadFails(const char *api, F &&fn) {
	const bool ok = std::forward<F>(fn)();
	if (!ok)
		WarnBackend(api, "Python（my.data）读取失败。");
	return ok;
}

BackendTarget NormalizeReadBackend(const char * /*api*/, BackendTarget b) {
	// 预留扩展点：目前直接透传，后续若引入 API 级别默认策略可在此集中处理。
	return b;
}

#if defined(READER_BACKEND_BOTH)
template <typename Fc, typename Fp>
bool BothTryOrdered(const char *api, Fc &&try_cpp, Fp &&try_py) {
	using quote_reader::Order;
	// both 模式的核心策略：先后顺序由全局 order 决定，失败时自动回退另一后端并打日志。
	if (quote_reader::current_order() == Order::CppFirst) {
		if (try_cpp())
			return true;
		std::cerr << "TicksData::" << api << ": C++ 直读未成功，将尝试 Python（my.data）。\n";
		if (try_py())
			return true;
		std::cerr << "TicksData::" << api << ": C++ 与 Python 均未成功。\n";
		return false;
	}
	if (try_py())
		return true;
	std::cerr << "TicksData::" << api << ": Python（my.data）未成功，将尝试 C++ 直读。\n";
	if (try_cpp())
		return true;
	std::cerr << "TicksData::" << api << ": Python 与 C++ 均未成功。\n";
	return false;
}
#endif

#if defined(READER_BACKEND_PYTHON_ONLY) || defined(READER_BACKEND_BOTH)
bool RunPySetNpqBasePath(const std::string &path) {
	if (path.find('"') != std::string::npos || path.find('\\') != std::string::npos)
		return false;
	char py_line[1024];
	std::snprintf(py_line, sizeof(py_line),
		      "from my.data import config\nconfig.NpqData.set_base_path(\"%s\")\n", path.c_str());
	return PyRun_SimpleString(py_line) == 0;
}
#endif

} // namespace

bool TicksData::SetMydataPath(const std::string &path, BackendTarget target) {
#if defined(READER_BACKEND_CPP_ONLY)
	if (target == BackendTarget::Python || target == BackendTarget::Both) {
		WarnBackend("SetMydataPath", "Python/Both path update ignored (cpp_only build)");
	}
	if (target == BackendTarget::Default || target == BackendTarget::Cpp || target == BackendTarget::Both)
		path_config_.SetRoot(path);
	return true;
#elif defined(READER_BACKEND_PYTHON_ONLY)
	if (target == BackendTarget::Cpp) {
		WarnBackend("SetMydataPath", "BackendTarget::Cpp ignored (python_only build)");
	}
	if (target == BackendTarget::Default || target == BackendTarget::Python || target == BackendTarget::Both) {
		// Python 侧 set_base_path 与本地 path_config_ 同步更新，便于 FormatPath 等辅助函数行为一致。
		path_config_.SetRoot(path);
		return RunPySetNpqBasePath(path);
	}
	return true;
#elif defined(READER_BACKEND_BOTH)
	if (target == BackendTarget::Default || target == BackendTarget::Both) {
		// both + Default/Both：显式同时更新两套路径来源。
		path_config_.SetRoot(path);
		return RunPySetNpqBasePath(path);
	}
	if (target == BackendTarget::Cpp) {
		path_config_.SetRoot(path);
		return true;
	}
	if (target == BackendTarget::Python) {
		return RunPySetNpqBasePath(path);
	}
	return true;
#else
#error ticks_data.cc SetMydataPath: expected exactly one READER_BACKEND_* macro
#endif
}

#if defined(READER_BACKEND_CPP_ONLY)

namespace {

template <typename F>
bool DispatchCppOnlyRead(const char *api, BackendTarget backend, F read_cpp) {
	// cpp_only 下所有 Get* 共享同一调度语义：忽略 Python/Both 请求并给出提示。
	backend = NormalizeReadBackend(api, backend);
	if (backend == BackendTarget::Python || backend == BackendTarget::Both)
		WarnBackend(api, "Python/Both read request ignored (cpp_only)");
	return read_cpp();
}

} // namespace

bool TicksData::GetTick(int date, const std::string &symbol, std::vector<my_book_stock> &vec_tick, BackendTarget backend) {
	return DispatchCppOnlyRead("GetTick", backend, [&]() { return GetTickViaCpp(date, symbol, vec_tick); });
}

bool TicksData::GetTransaction(int date, const std::string &symbol, std::vector<my_book_stock_transaction> &vec_trasaction, BackendTarget backend) {
	return DispatchCppOnlyRead("GetTransaction", backend,
				   [&]() { return GetTransactionViaCpp(date, symbol, vec_trasaction); });
}

bool TicksData::GetOrder(int date, const std::string &symbol, std::vector<my_book_stock_order> &vec_order, BackendTarget backend) {
	return DispatchCppOnlyRead("GetOrder", backend, [&]() { return GetOrderViaCpp(date, symbol, vec_order); });
}

bool TicksData::GetOrderQueue(int date, const std::string &symbol, std::vector<my_book_stock_order_queue> &vec_order_queue, BackendTarget backend) {
	return DispatchCppOnlyRead("GetOrderQueue", backend,
				   [&]() { return GetOrderQueueViaCpp(date, symbol, vec_order_queue); });
}

bool TicksData::GetOrderV2(int date, const std::string &symbol, std::vector<my_book_stock_order_new> &vec_order_new, BackendTarget backend) {
	return DispatchCppOnlyRead("GetOrderV2", backend, [&]() { return GetOrderV2ViaCpp(date, symbol, vec_order_new); });
}

bool TicksData::GetTransactionV2(int date, const std::string &symbol, std::vector<my_book_stock_transaction_new> &vec_transaction_new, BackendTarget backend) {
	return DispatchCppOnlyRead("GetTransactionV2", backend,
				   [&]() { return GetTransactionV2ViaCpp(date, symbol, vec_transaction_new); });
}

bool TicksData::GetFutures(int date, int mi_type, const std::string &symbol, std::vector<my_futures_t> &vec_futures, BackendTarget backend) {
	return DispatchCppOnlyRead("GetFutures", backend,
				   [&]() { return GetFuturesViaCpp(date, mi_type, symbol, vec_futures); });
}

bool TicksData::GetFuturesBySymbol(int date, int mi_type, const std::string &symbol, std::map<std::string, std::vector<my_futures_t>> &map_futures, BackendTarget backend) {
	return DispatchCppOnlyRead("GetFuturesBySymbol", backend,
				   [&]() { return GetFuturesBySymbolViaCpp(date, mi_type, symbol, map_futures); });
}

bool TicksData::Get200Futures(int date, const std::string &symbol, std::map<std::string, std::vector<my_futures_t>> &map_futures, BackendTarget backend) {
	return DispatchCppOnlyRead("Get200Futures", backend,
				   [&]() { return Get200FuturesViaCpp(date, symbol, map_futures); });
}

bool TicksData::Get212Futures(int date, const std::string &symbol, std::map<std::string, std::vector<my_futures_t>> &map_futures, BackendTarget backend) {
	return DispatchCppOnlyRead("Get212Futures", backend,
				   [&]() { return Get212FuturesViaCpp(date, symbol, map_futures); });
}

bool TicksData::Get225Futures(int date, const std::string &symbol, std::map<std::string, std::vector<my_futures_t>> &map_futures, BackendTarget backend) {
	return DispatchCppOnlyRead("Get225Futures", backend,
				   [&]() { return Get225FuturesViaCpp(date, symbol, map_futures); });
}

bool TicksData::GetOrderTrans(int date, const std::string &symbol, std::vector<my_book_stock_order_trans_new> &vec_out, BackendTarget backend) {
	return DispatchCppOnlyRead("GetOrderTrans", backend,
				   [&]() { return GetOrderTransViaCpp(date, symbol, vec_out); });
}

#elif defined(READER_BACKEND_PYTHON_ONLY)

namespace {

template <typename F>
bool DispatchPythonOnlyRead(const char *api, BackendTarget backend, F read_python) {
	// python_only 下所有 Get* 共享同一调度语义：忽略 Cpp 请求，统一包装 Python 失败告警。
	backend = NormalizeReadBackend(api, backend);
	if (backend == BackendTarget::Cpp)
		WarnBackend(api, "BackendTarget::Cpp ignored (python_only)");
	return WarnIfPythonReadFails(api, read_python);
}

} // namespace

bool TicksData::GetTick(int date, const std::string &symbol, std::vector<my_book_stock> &vec_tick, BackendTarget backend) {
	return DispatchPythonOnlyRead("GetTick", backend, [&]() { return GetTickViaPython(date, symbol, vec_tick); });
}

bool TicksData::GetTransaction(int date, const std::string &symbol, std::vector<my_book_stock_transaction> &vec_trasaction, BackendTarget backend) {
	return DispatchPythonOnlyRead("GetTransaction", backend,
				      [&]() { return GetTransactionViaPython(date, symbol, vec_trasaction); });
}

bool TicksData::GetOrder(int date, const std::string &symbol, std::vector<my_book_stock_order> &vec_order, BackendTarget backend) {
	return DispatchPythonOnlyRead("GetOrder", backend, [&]() { return GetOrderViaPython(date, symbol, vec_order); });
}

bool TicksData::GetOrderQueue(int date, const std::string &symbol, std::vector<my_book_stock_order_queue> &vec_order_queue, BackendTarget backend) {
	return DispatchPythonOnlyRead("GetOrderQueue", backend,
				      [&]() { return GetOrderQueueViaPython(date, symbol, vec_order_queue); });
}

bool TicksData::GetOrderV2(int date, const std::string &symbol, std::vector<my_book_stock_order_new> &vec_order_new, BackendTarget backend) {
	return DispatchPythonOnlyRead("GetOrderV2", backend,
				      [&]() { return GetOrderV2ViaPython(date, symbol, vec_order_new); });
}

bool TicksData::GetTransactionV2(int date, const std::string &symbol, std::vector<my_book_stock_transaction_new> &vec_transaction_new, BackendTarget backend) {
	return DispatchPythonOnlyRead("GetTransactionV2", backend,
				      [&]() { return GetTransactionV2ViaPython(date, symbol, vec_transaction_new); });
}

bool TicksData::GetFutures(int date, int mi_type, const std::string &symbol, std::vector<my_futures_t> &vec_futures, BackendTarget backend) {
	return DispatchPythonOnlyRead("GetFutures", backend,
				      [&]() { return GetFuturesViaPython(date, mi_type, symbol, vec_futures); });
}

bool TicksData::GetFuturesBySymbol(int date, int mi_type, const std::string &symbol, std::map<std::string, std::vector<my_futures_t>> &map_futures, BackendTarget backend) {
	return DispatchPythonOnlyRead("GetFuturesBySymbol", backend, [&]() {
		return GetFuturesBySymbolViaPython(date, mi_type, symbol, map_futures);
	});
}

bool TicksData::Get200Futures(int date, const std::string &symbol, std::map<std::string, std::vector<my_futures_t>> &map_futures, BackendTarget backend) {
	return DispatchPythonOnlyRead("Get200Futures", backend,
				      [&]() { return Get200FuturesViaPython(date, symbol, map_futures); });
}

bool TicksData::Get212Futures(int date, const std::string &symbol, std::map<std::string, std::vector<my_futures_t>> &map_futures, BackendTarget backend) {
	return DispatchPythonOnlyRead("Get212Futures", backend,
				      [&]() { return Get212FuturesViaPython(date, symbol, map_futures); });
}

bool TicksData::Get225Futures(int date, const std::string &symbol, std::map<std::string, std::vector<my_futures_t>> &map_futures, BackendTarget backend) {
	return DispatchPythonOnlyRead("Get225Futures", backend,
				      [&]() { return Get225FuturesViaPython(date, symbol, map_futures); });
}

bool TicksData::GetOrderTrans(int date, const std::string &symbol, std::vector<my_book_stock_order_trans_new> &vec_out, BackendTarget backend) {
	return DispatchPythonOnlyRead("GetOrderTrans", backend,
				      [&]() { return GetOrderTransViaPython(date, symbol, vec_out); });
}

#elif defined(READER_BACKEND_BOTH)

namespace {

template <typename Fc, typename Fp>
bool DispatchBothRead(const char *api, BackendTarget backend, Fc read_cpp, Fp read_python) {
	// both 下三态分流：强制 Cpp / 强制 Python / 按全局顺序自动尝试。
	backend = NormalizeReadBackend(api, backend);
	if (backend == BackendTarget::Cpp)
		return read_cpp();
	if (backend == BackendTarget::Python)
		return WarnIfPythonReadFails(api, read_python);
	return BothTryOrdered(api, read_cpp, read_python);
}

} // namespace

bool TicksData::GetTick(int date, const std::string &symbol, std::vector<my_book_stock> &vec_tick, BackendTarget backend) {
	return DispatchBothRead("GetTick", backend, [&]() { return GetTickViaCpp(date, symbol, vec_tick); },
				[&]() { return GetTickViaPython(date, symbol, vec_tick); });
}

bool TicksData::GetTransaction(int date, const std::string &symbol, std::vector<my_book_stock_transaction> &vec_trasaction, BackendTarget backend) {
	return DispatchBothRead("GetTransaction", backend,
				[&]() { return GetTransactionViaCpp(date, symbol, vec_trasaction); },
				[&]() { return GetTransactionViaPython(date, symbol, vec_trasaction); });
}

bool TicksData::GetOrder(int date, const std::string &symbol, std::vector<my_book_stock_order> &vec_order, BackendTarget backend) {
	return DispatchBothRead("GetOrder", backend, [&]() { return GetOrderViaCpp(date, symbol, vec_order); },
				[&]() { return GetOrderViaPython(date, symbol, vec_order); });
}

bool TicksData::GetOrderQueue(int date, const std::string &symbol, std::vector<my_book_stock_order_queue> &vec_order_queue, BackendTarget backend) {
	return DispatchBothRead("GetOrderQueue", backend,
				[&]() { return GetOrderQueueViaCpp(date, symbol, vec_order_queue); },
				[&]() { return GetOrderQueueViaPython(date, symbol, vec_order_queue); });
}

bool TicksData::GetOrderV2(int date, const std::string &symbol, std::vector<my_book_stock_order_new> &vec_order_new, BackendTarget backend) {
	return DispatchBothRead("GetOrderV2", backend, [&]() { return GetOrderV2ViaCpp(date, symbol, vec_order_new); },
				[&]() { return GetOrderV2ViaPython(date, symbol, vec_order_new); });
}

bool TicksData::GetTransactionV2(int date, const std::string &symbol, std::vector<my_book_stock_transaction_new> &vec_transaction_new, BackendTarget backend) {
	return DispatchBothRead("GetTransactionV2", backend,
				[&]() { return GetTransactionV2ViaCpp(date, symbol, vec_transaction_new); },
				[&]() { return GetTransactionV2ViaPython(date, symbol, vec_transaction_new); });
}

bool TicksData::GetFutures(int date, int mi_type, const std::string &symbol, std::vector<my_futures_t> &vec_futures, BackendTarget backend) {
	return DispatchBothRead("GetFutures", backend,
				[&]() { return GetFuturesViaCpp(date, mi_type, symbol, vec_futures); },
				[&]() { return GetFuturesViaPython(date, mi_type, symbol, vec_futures); });
}

bool TicksData::GetFuturesBySymbol(int date, int mi_type, const std::string &symbol, std::map<std::string, std::vector<my_futures_t>> &map_futures, BackendTarget backend) {
	return DispatchBothRead("GetFuturesBySymbol", backend,
				[&]() { return GetFuturesBySymbolViaCpp(date, mi_type, symbol, map_futures); },
				[&]() { return GetFuturesBySymbolViaPython(date, mi_type, symbol, map_futures); });
}

bool TicksData::Get200Futures(int date, const std::string &symbol, std::map<std::string, std::vector<my_futures_t>> &map_futures, BackendTarget backend) {
	return DispatchBothRead("Get200Futures", backend,
				[&]() { return Get200FuturesViaCpp(date, symbol, map_futures); },
				[&]() { return Get200FuturesViaPython(date, symbol, map_futures); });
}

bool TicksData::Get212Futures(int date, const std::string &symbol, std::map<std::string, std::vector<my_futures_t>> &map_futures, BackendTarget backend) {
	return DispatchBothRead("Get212Futures", backend,
				[&]() { return Get212FuturesViaCpp(date, symbol, map_futures); },
				[&]() { return Get212FuturesViaPython(date, symbol, map_futures); });
}

bool TicksData::Get225Futures(int date, const std::string &symbol, std::map<std::string, std::vector<my_futures_t>> &map_futures, BackendTarget backend) {
	return DispatchBothRead("Get225Futures", backend,
				[&]() { return Get225FuturesViaCpp(date, symbol, map_futures); },
				[&]() { return Get225FuturesViaPython(date, symbol, map_futures); });
}

bool TicksData::GetOrderTrans(int date, const std::string &symbol, std::vector<my_book_stock_order_trans_new> &vec_out, BackendTarget backend) {
	return DispatchBothRead("GetOrderTrans", backend,
				[&]() { return GetOrderTransViaCpp(date, symbol, vec_out); },
				[&]() { return GetOrderTransViaPython(date, symbol, vec_out); });
}

#else
#error ticks_data.cc: Get* dispatch expects exactly one of READER_BACKEND_CPP_ONLY, READER_BACKEND_PYTHON_ONLY, READER_BACKEND_BOTH
#endif

