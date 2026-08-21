#pragma once

// 对外仅大驼峰 API；读数接口末尾 BackendTarget 可省略，缺省见 io::kDefaultRead（cpp_only→Cpp，python_only→Python，both→Both：按 quote_reader 顺序双端尝试；显式 Cpp/Python 则单端）。

#include "backend_config.h"
#include "data_path_config.h"
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "raw_data_types/my_futures.h"
#include "raw_data_types/my_stock.h"
#include "raw_data_types/my_stock_order.h"
#include "raw_data_types/my_stock_order_new.h"
#include "raw_data_types/my_stock_order_queue.h"
#include "raw_data_types/my_stock_transaction.h"
#include "raw_data_types/my_stock_transaction_new.h"

#if defined(READER_BACKEND_PYTHON_ONLY) || defined(READER_BACKEND_BOTH)
#include <numpy/arrayobject.h>
#include <python3.8/Python.h>
#endif

// TicksData：在数据根下读行情/逐笔类落盘（.npq 等）及 MC/ESMC；可 C++ 直读或嵌 Python（my.data）。
// symbol / fid：可与 Python 约定「all」等字面量（单文件聚合，取决于 my.data 与落盘）。
class TicksData {
public:
	enum DATA_TYPE {
		TICK = 0,
		TRANSACTION = 1,
		ORDER = 2,
		ORDER_QUEUE = 4,
		ORDER_NEW = 5,
		TRANSACTION_NEW = 6,
		MC = 7,
		ESMC = 8,
		FUTURES = 9,
		ORDER_TRANS = 10,
	};

	bool SetMydataPath(const std::string &path, BackendTarget target = io::kDefaultPath);

	bool GetTick(int date, const std::string &symbol, std::vector<my_book_stock> &vec_tick,
		BackendTarget backend = io::kDefaultRead);
	bool GetTransaction(int date, const std::string &symbol, std::vector<my_book_stock_transaction> &vec_trasaction,
		BackendTarget backend = io::kDefaultRead);
	bool GetOrder(int date, const std::string &symbol, std::vector<my_book_stock_order> &vec_order,
		      BackendTarget backend = io::kDefaultRead);
	bool GetOrderQueue(int date, const std::string &symbol, std::vector<my_book_stock_order_queue> &vec_order_queue,
			   BackendTarget backend = io::kDefaultRead);
	bool GetOrderV2(int date, const std::string &symbol, std::vector<my_book_stock_order_new> &vec_order_new,
			BackendTarget backend = io::kDefaultRead);
	bool GetTransactionV2(int date, const std::string &symbol,
			      std::vector<my_book_stock_transaction_new> &vec_transaction_new,
			      BackendTarget backend = io::kDefaultRead);
	bool GetFutures(int date, int mi_type, const std::string &symbol, std::vector<my_futures_t> &vec_futures,
			BackendTarget backend = io::kDefaultRead);
	bool GetFuturesBySymbol(int date, int mi_type, const std::string &symbol,
				std::map<std::string, std::vector<my_futures_t>> &map_futures,
				BackendTarget backend = io::kDefaultRead);
	bool Get212Futures(int date, const std::string &symbol, std::map<std::string, std::vector<my_futures_t>> &map_futures,
			   BackendTarget backend = io::kDefaultRead);
	bool Get200Futures(int date, const std::string &symbol, std::map<std::string, std::vector<my_futures_t>> &map_futures,
			   BackendTarget backend = io::kDefaultRead);
	bool Get225Futures(int date, const std::string &symbol, std::map<std::string, std::vector<my_futures_t>> &map_futures,
			   BackendTarget backend = io::kDefaultRead);
	bool GetOrderTrans(int date, const std::string &symbol, std::vector<my_book_stock_order_trans_new> &vec_out,
			   BackendTarget backend = io::kDefaultRead);

	// MC ≈ Main Contract（主连映射表）；实际仅读磁盘 npq，BackendTarget::Python 会 cerr 后仍走磁盘。
	bool GetSymbolFromMc(int date, int mi_type, const std::string &product, std::vector<std::string> &symbols,
			     BackendTarget backend = io::kDefaultRead);

	std::string FormatPath(int date, std::string symbol, int data_type, int mi_type = 0);
	void SetLocalPath(std::string path);

	TicksData();
	~TicksData();

private:
	void OrderTransRelativeArtifacts(int date, const std::string &symbol, std::string &rel_myzst,
					 std::string &rel_npq) const;

	bool LoadSymbolsFromMc(int date, int mi_type, const std::string &product, std::vector<std::string> &symbols);

#if defined(READER_BACKEND_PYTHON_ONLY) || defined(READER_BACKEND_BOTH)
	bool GetTickViaPython(int date, std::string symbol, std::vector<my_book_stock> &vec_tick);
	bool GetTransactionViaPython(int date, std::string symbol, std::vector<my_book_stock_transaction> &vec_trasaction);
	bool GetOrderViaPython(int date, std::string symbol, std::vector<my_book_stock_order> &vec_order);
	bool GetOrderQueueViaPython(int date, std::string symbol, std::vector<my_book_stock_order_queue> &vec_order_queue);
	bool GetOrderV2ViaPython(int date, std::string symbol, std::vector<my_book_stock_order_new> &vec_order_new);
	bool GetTransactionV2ViaPython(int date, std::string symbol,
				       std::vector<my_book_stock_transaction_new> &vec_transaction_new);
	bool GetFuturesViaPython(int date, int mi_type, const std::string &symbol, std::vector<my_futures_t> &vec_futures);
	bool GetFuturesBySymbolViaPython(int date, int mi_type, const std::string &symbol,
					 std::map<std::string, std::vector<my_futures_t>> &map_futures);
	bool Get212FuturesViaPython(int date, const std::string &symbol,
				    std::map<std::string, std::vector<my_futures_t>> &map_futures);
	bool Get200FuturesViaPython(int date, const std::string &symbol,
				    std::map<std::string, std::vector<my_futures_t>> &map_futures);
	bool Get225FuturesViaPython(int date, const std::string &symbol,
				    std::map<std::string, std::vector<my_futures_t>> &map_futures);
	bool GetOrderTransViaPython(int date, std::string symbol, std::vector<my_book_stock_order_trans_new> &vec_order_new);
#endif

#if defined(READER_BACKEND_CPP_ONLY) || defined(READER_BACKEND_BOTH)
	bool GetTickViaCpp(int date, std::string symbol, std::vector<my_book_stock> &vec_tick);
	bool GetTransactionViaCpp(int date, std::string symbol, std::vector<my_book_stock_transaction> &vec_trasaction);
	bool GetOrderViaCpp(int date, std::string symbol, std::vector<my_book_stock_order> &vec_order);
	bool GetOrderQueueViaCpp(int date, std::string symbol, std::vector<my_book_stock_order_queue> &vec_order_queue);
	bool GetOrderV2ViaCpp(int date, std::string symbol, std::vector<my_book_stock_order_new> &vec_order_new);
	bool GetTransactionV2ViaCpp(int date, std::string symbol, std::vector<my_book_stock_transaction_new> &vec_transaction_new);
	bool GetFuturesViaCpp(int date, int mi_type, const std::string &symbol, std::vector<my_futures_t> &vec_futures);
	bool GetFuturesBySymbolViaCpp(int date, int mi_type, const std::string &symbol,
		std::map<std::string, std::vector<my_futures_t>> &map_futures);
	bool Get200FuturesViaCpp(int date, const std::string &symbol, std::map<std::string, std::vector<my_futures_t>> &map_futures);
	bool Get212FuturesViaCpp(int date, const std::string &symbol, std::map<std::string, std::vector<my_futures_t>> &map_futures);
	bool Get225FuturesViaCpp(int date, const std::string &symbol, std::map<std::string, std::vector<my_futures_t>> &map_futures);
	bool GetOrderTransViaCpp(int date, std::string symbol, std::vector<my_book_stock_order_trans_new> &vec_out);
#endif

	DataPathConfig path_config_;

#if defined(READER_BACKEND_PYTHON_ONLY) || defined(READER_BACKEND_BOTH)
	PyObject *m;
	PyObject *data_module_;
#endif
};
