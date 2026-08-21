// 嵌入 Python my.data.quote 的 Get*ViaPython 实现（翻译单元拆分）。
// /usr/local/python3.8.10/lib/python3.8/site-packages/my/

#include "ticks_data.h"
#include <algorithm>
#include <cstring>

#if defined(READER_BACKEND_PYTHON_ONLY) || defined(READER_BACKEND_BOTH)

namespace {

template <typename T>
bool ReadRowsViaMyData(PyObject *data_module, int date, int mi_type, const std::string &symbol,
		       std::vector<T> &out) {
	out.clear();
	if (!data_module)
		return false;

	PyObject *args = PyTuple_New(5);
	if (!args)
		return false;
	PyObject *py_date = Py_BuildValue("I", date);
	PyObject *py_mi_type = Py_BuildValue("I", mi_type);
	PyObject *py_symbol = Py_BuildValue("s", symbol.c_str());
	PyObject *tmp = Py_BuildValue("I", 0);
	PyObject *py_ip = Py_BuildValue("s", "0");
	// 对齐 my.data.quote.data(date, mi_type, symbol, 0, "0") 的调用约定。
	if (!py_date || !py_mi_type || !py_symbol || !tmp || !py_ip) {
		Py_XDECREF(py_date);
		Py_XDECREF(py_mi_type);
		Py_XDECREF(py_symbol);
		Py_XDECREF(tmp);
		Py_XDECREF(py_ip);
		Py_DECREF(args);
		return false;
	}

	PyTuple_SET_ITEM(args, 0, py_date);
	PyTuple_SET_ITEM(args, 1, py_mi_type);
	PyTuple_SET_ITEM(args, 2, py_symbol);
	PyTuple_SET_ITEM(args, 3, tmp);
	PyTuple_SET_ITEM(args, 4, py_ip);

	PyObject *ret = PyObject_Call(data_module, args, NULL);
	Py_DECREF(args);
	if (!ret || ret == Py_None) {
		Py_XDECREF(ret);
		return false;
	}

	PyArrayObject *data_arr = reinterpret_cast<PyArrayObject *>(ret);
	if (!PyArray_IS_C_CONTIGUOUS(data_arr)) {
		// 统一转为 C 连续布局，保证后续按结构体数组 memcpy 是安全的。
		PyArrayObject *copy = reinterpret_cast<PyArrayObject *>(PyArray_NewCopy(data_arr, NPY_CORDER));
		Py_DECREF(data_arr);
		if (!copy)
			return false;
		data_arr = copy;
	}

	npy_intp *shape = PyArray_SHAPE(data_arr);
	const npy_intp row_cnt = shape ? shape[0] : 0;
	if (row_cnt < 0) {
		Py_DECREF(data_arr);
		return false;
	}
	out.resize(static_cast<size_t>(row_cnt));
	if (row_cnt > 0) {
		char *bytes = PyArray_BYTES(data_arr);
		std::memcpy(out.data(), bytes, static_cast<size_t>(row_cnt) * sizeof(T));
	}
	Py_DECREF(data_arr);
	return true;
}

} // namespace

bool TicksData::GetTickViaPython(int date, std::string symbol, std::vector<my_book_stock> &vec_tick) {
	return ReadRowsViaMyData(data_module_, date, 209, symbol, vec_tick);
}

bool TicksData::GetTransactionViaPython(int date, std::string symbol,
					std::vector<my_book_stock_transaction> &vec_trasaction) {
	return ReadRowsViaMyData(data_module_, date, 254, symbol, vec_trasaction);
}

bool TicksData::GetOrderViaPython(int date, std::string symbol, std::vector<my_book_stock_order> &vec_order) {
	return ReadRowsViaMyData(data_module_, date, 253, symbol, vec_order);
}

bool TicksData::GetOrderQueueViaPython(int date, std::string symbol,
				       std::vector<my_book_stock_order_queue> &vec_order_queue) {
	return ReadRowsViaMyData(data_module_, date, 252, symbol, vec_order_queue);
}

bool TicksData::GetOrderV2ViaPython(int date, std::string symbol, std::vector<my_book_stock_order_new> &vec_order_new) {
	return ReadRowsViaMyData(data_module_, date, 257, symbol, vec_order_new);
}

bool TicksData::GetOrderTransViaPython(int date, std::string symbol,
				       std::vector<my_book_stock_order_trans_new> &vec_order_new) {
	return ReadRowsViaMyData(data_module_, date, 231, symbol, vec_order_new);
}

bool TicksData::GetTransactionV2ViaPython(int date, std::string symbol,
					  std::vector<my_book_stock_transaction_new> &vec_transaction_new) {
	return ReadRowsViaMyData(data_module_, date, 256, symbol, vec_transaction_new);
}

bool TicksData::GetFuturesViaPython(int date, int mi_type, const std::string &symbol,
				    std::vector<my_futures_t> &vec_futures) {
	return ReadRowsViaMyData(data_module_, date, mi_type, symbol, vec_futures);
}

bool TicksData::GetFuturesBySymbolViaPython(int date, int mi_type, const std::string &symbol,
					    std::map<std::string, std::vector<my_futures_t>> &map_futures) {
	std::vector<std::string> symbols;
	if (!LoadSymbolsFromMc(date, mi_type, symbol, symbols)) {
		return false;
	}

	std::vector<my_futures_t> futures;
	std::vector<std::string>::iterator iter = symbols.begin();
	for (; iter != symbols.end(); ++iter) {
		// 与历史行为保持一致：单个合约失败不整体失败，继续读取其他合约。
		bool success = GetFuturesViaPython(date, mi_type, *iter, futures);
		if (!success) {
			continue;
		}
		if (futures.size() > 0) {
			map_futures[*iter] = futures;
		}
	}
	return true;
}

bool TicksData::Get212FuturesViaPython(int date, const std::string &symbol,
				       std::map<std::string, std::vector<my_futures_t>> &map_futures) {
	std::string s(symbol);
	std::transform(s.begin(), s.end(), s.begin(), ::tolower);
	return GetFuturesBySymbolViaPython(date, 212, s, map_futures);
}

bool TicksData::Get200FuturesViaPython(int date, const std::string &symbol,
				       std::map<std::string, std::vector<my_futures_t>> &map_futures) {
	std::string s(symbol);
	std::transform(s.begin(), s.end(), s.begin(), ::tolower);
	return GetFuturesBySymbolViaPython(date, 200, s, map_futures);
}

bool TicksData::Get225FuturesViaPython(int date, const std::string &symbol,
				       std::map<std::string, std::vector<my_futures_t>> &map_futures) {
	std::string s(symbol);
	std::transform(s.begin(), s.end(), s.begin(), ::toupper);
	return GetFuturesBySymbolViaPython(date, 225, s, map_futures);
}

#endif
