// TicksData::*ViaCpp：C++ 侧读数实现（当前以 .npq 为主；翻译单元与 raw_data_reader/Makefile 宏一致）。
// QUOTE 类路径与 Python NpqData._path_parse 一致；
// C++ 侧为效率优先：同 stem 先读 .npq，再回退 .myzst（与 Python 先压缩后未压缩的顺序不同）。
#include "ticks_data.h"
#include "data_path_config.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <cstdlib>

#if defined(READER_BACKEND_CPP_ONLY) || defined(READER_BACKEND_BOTH)

namespace {

void LogMyzstCppNotImplemented(const char *api, const std::string &abs_myzst) {
	std::cerr << api << ": 发现压缩文件「" << abs_myzst << "」，将进入 .myzst 读取流程（当前通过 zstd 命令行实现）。\n";
}

std::string EscapeForSingleQuotedShell(const std::string &s) {
	std::string out;
	out.reserve(s.size() + 8);
	for (size_t i = 0; i < s.size(); ++i) {
		const char c = s[i];
		if (c == '\'')
			out += "'\\''";
		else
			out += c;
	}
	return out;
}

bool DecompressMyzstToBytesByZstdCli(const std::string &abs_myzst, std::vector<char> &bytes) {
	bytes.clear();
	const std::string escaped = EscapeForSingleQuotedShell(abs_myzst);
	// TODO(gaowang): 用原生 zstd 库替代外部命令，减少进程创建和环境依赖。
	// 这里先用 zstd CLI 保持实现简单直接，且与线上工具链行为一致。
	const std::string cmd = "zstd -d -q -c '" + escaped + "'";
	FILE *pipe = popen(cmd.c_str(), "r");
	if (!pipe)
		return false;
	char buf[1 << 15];
	for (;;) {
		const size_t n = fread(buf, 1, sizeof(buf), pipe);
		if (n > 0)
			bytes.insert(bytes.end(), buf, buf + n);
		if (n < sizeof(buf)) {
			if (feof(pipe))
				break;
			if (ferror(pipe)) {
				pclose(pipe);
				return false;
			}
		}
	}
	const int status = pclose(pipe);
	return status == 0;
}

template <typename T>
bool TryReadMyzstPlaceholder(const char *api, const std::string &abs_myzst, std::vector<T> &out) {
	std::vector<char> bytes;
	if (!DecompressMyzstToBytesByZstdCli(abs_myzst, bytes)) {
		std::cerr << api << ": .myzst 解压失败，fallback 到 .npq: " << abs_myzst << "\n";
		return false;
	}
	if (bytes.empty()) {
		out.clear();
		return true;
	}
	if (bytes.size() % sizeof(T) != 0) {
		std::cerr << api << ": .myzst 解压后字节数与结构体大小不匹配: bytes=" << bytes.size()
			  << ", sizeof(T)=" << sizeof(T) << "\n";
		return false;
	}
	const size_t row_cnt = bytes.size() / sizeof(T);
	out.resize(row_cnt);
	std::memcpy(out.data(), bytes.data(), bytes.size());
	return true;
}

void ResolveQuoteArtifacts(const DataPathConfig &cfg, const std::string &rel_myzst, const std::string &rel_npq,
			  bool use_local_or_mount, std::string &abs_myzst, std::string &abs_npq) {
	abs_myzst = cfg.ResolvePathParseStyleRelative(rel_myzst, use_local_or_mount);
	abs_npq = cfg.ResolvePathParseStyleRelative(rel_npq, use_local_or_mount);
}

template <typename T>
bool ReadNpqAfterMyzstProbe(const char *api, const std::string &abs_myzst, const std::string &abs_npq, std::vector<T> &out) {
	out.clear();
	// 设计选择：C++ 侧优先 .npq（低开销直读），缺失时再回退 .myzst。
	std::ifstream is(abs_npq, std::ios::binary);
	if (is) {
		const int nlength = static_cast<int>(sizeof(T));
		char *buff = new char[static_cast<size_t>(nlength)];
		memset(buff, 0, static_cast<size_t>(nlength));
		while (is.peek() != EOF) {
			is.read(buff, nlength);
			T row = *reinterpret_cast<T *>(buff);
			out.push_back(row);
		}
		delete[] buff;
		is.close();
		return true;
	}

	// 与 Python quote.data() 的“优先读压缩文件”不同，C++ 仅在 .npq 不存在时回退到 .myzst。
	if (DataPathConfig::IsRegularFile(abs_myzst)) {
		LogMyzstCppNotImplemented(api, abs_myzst);
		if (TryReadMyzstPlaceholder(api, abs_myzst, out))
			return true;
	}
	std::cerr << api << ": 无法打开「" << abs_npq << "」，且 .myzst 不可用: " << abs_myzst << "\n";
	return false;
}

template <typename T>
bool ReadQuoteRowsViaCpp(const DataPathConfig &cfg, const char *api, int date, int mi_type,
			 const std::string &symbol, bool strict_quote_branch, std::vector<T> &out) {
	// 提取通用骨架：组装 quote 路径 -> 解析绝对路径 -> 按 C++ 规则读 .npq/.myzst。
	std::string rel_z, rel_n;
	BuildPythonQuoteStyleRelativePaths(cfg, date, mi_type, 0, 0, symbol, strict_quote_branch, rel_z, rel_n);
	std::string abs_z, abs_n;
	ResolveQuoteArtifacts(cfg, rel_z, rel_n, false, abs_z, abs_n);
	return ReadNpqAfterMyzstProbe(api, abs_z, abs_n, out);
}

template <typename T>
bool ReadFixedRecordsFromNpqExact(const char *api, const std::string &full_npq, std::vector<T> &out) {
	std::ifstream is(full_npq, std::ifstream::binary);
	if (!is) {
		std::cerr << api << ": 无法打开「" << full_npq << "」。\n";
		return false;
	}
	const int nlength = static_cast<int>(sizeof(T));
	char *buff = new char[nlength];
	memset(buff, 0, nlength);
	while (is.peek() != EOF) {
		is.read(buff, nlength);
		T row = *reinterpret_cast<T *>(buff);
		out.push_back(row);
	}
	delete[] buff;
	is.close();
	return true;
}

} // namespace

bool TicksData::GetTickViaCpp(int date, std::string symbol, std::vector<my_book_stock> &vec_tick) {
	return ReadQuoteRowsViaCpp(path_config_, "TicksData::GetTickViaCpp", date, 209, symbol, false, vec_tick);
}

bool TicksData::GetTransactionViaCpp(int date, std::string symbol, std::vector<my_book_stock_transaction> &vec_trasaction) {
	return ReadQuoteRowsViaCpp(path_config_, "TicksData::GetTransactionViaCpp", date, 254, symbol, true,
				   vec_trasaction);
}

bool TicksData::GetTransactionV2ViaCpp(int date, std::string symbol, std::vector<my_book_stock_transaction_new> &vec_trasaction) {
	return ReadQuoteRowsViaCpp(path_config_, "TicksData::GetTransactionV2ViaCpp", date, 256, symbol, true,
				   vec_trasaction);
}

bool TicksData::GetOrderViaCpp(int date, std::string symbol, std::vector<my_book_stock_order> &vec_order) {
	return ReadQuoteRowsViaCpp(path_config_, "TicksData::GetOrderViaCpp", date, 253, symbol, true, vec_order);
}

bool TicksData::GetOrderV2ViaCpp(int date, std::string symbol, std::vector<my_book_stock_order_new> &vec_order) {
	return ReadQuoteRowsViaCpp(path_config_, "TicksData::GetOrderV2ViaCpp", date, 257, symbol, true, vec_order);
}

bool TicksData::GetOrderQueueViaCpp(int date, std::string symbol, std::vector<my_book_stock_order_queue> &vec_order_queue) {
	return ReadQuoteRowsViaCpp(path_config_, "TicksData::GetOrderQueueViaCpp", date, 252, symbol, true,
				   vec_order_queue);
}

bool TicksData::Get200FuturesViaCpp(int date, const std::string &symbol, std::map<std::string, std::vector<my_futures_t>> &map_futures) {
	std::string s(symbol);
	std::transform(s.begin(), s.end(), s.begin(), ::tolower);
	return GetFuturesBySymbolViaCpp(date, 200, s, map_futures);
}

bool TicksData::Get212FuturesViaCpp(int date, const std::string &symbol, std::map<std::string, std::vector<my_futures_t>> &map_futures) {
	std::string s(symbol);
	std::transform(s.begin(), s.end(), s.begin(), ::tolower);
	return GetFuturesBySymbolViaCpp(date, 212, s, map_futures);
}

bool TicksData::Get225FuturesViaCpp(int date, const std::string &symbol, std::map<std::string, std::vector<my_futures_t>> &map_futures) {
	std::string s(symbol);
	std::transform(s.begin(), s.end(), s.begin(), ::toupper);
	return GetFuturesBySymbolViaCpp(date, 225, s, map_futures);
}

bool TicksData::GetFuturesBySymbolViaCpp(int date, int mi_type, const std::string &symbol,
					  std::map<std::string, std::vector<my_futures_t>> &map_futures) {
	std::vector<std::string> symbols;
	if (!LoadSymbolsFromMc(date, mi_type, symbol, symbols)) {
		return false;
	}

	std::vector<my_futures_t> futures;
	for (std::vector<std::string>::iterator iter = symbols.begin(); iter != symbols.end(); ++iter) {
		if (!GetFuturesViaCpp(date, mi_type, *iter, futures)) {
			continue;
		}
		if (futures.size() > 0) {
			map_futures[*iter] = futures;
		}
	}
	return true;
}

bool TicksData::GetFuturesViaCpp(int date, int mi_type, const std::string &symbol, std::vector<my_futures_t> &vec_futures) {
	return ReadQuoteRowsViaCpp(path_config_, "TicksData::GetFuturesViaCpp", date, mi_type, symbol, false, vec_futures);
}

bool TicksData::GetOrderTransViaCpp(int date, std::string symbol, std::vector<my_book_stock_order_trans_new> &vec_out) {
	vec_out.clear();
	std::string rel_zst, rel_npq;
	OrderTransRelativeArtifacts(date, symbol, rel_zst, rel_npq);
	std::string full_zst, full_npq;
	ResolveQuoteArtifacts(path_config_, rel_zst, rel_npq, false, full_zst, full_npq);
	if (DataPathConfig::IsRegularFile(full_zst))
		// 当前 order_trans 仍以 .npq 直读为主；若存在 .myzst 仅提示，不走解压解析流程。
		LogMyzstCppNotImplemented("TicksData::GetOrderTransViaCpp", full_zst);
	if (!DataPathConfig::IsRegularFile(full_npq)) {
		std::cerr << "TicksData::GetOrderTransViaCpp: 无可用 .npq「" << full_npq << "」。\n";
		return false;
	}
	return ReadFixedRecordsFromNpqExact("TicksData::GetOrderTransViaCpp", full_npq, vec_out);
}

#endif
