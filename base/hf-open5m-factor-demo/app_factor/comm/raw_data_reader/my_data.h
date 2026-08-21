#pragma once

#include "backend_config.h"
#include <string>
#include <vector>

#include "raw_data_types/my_data.h"

#if defined(READER_BACKEND_PYTHON_ONLY) || defined(READER_BACKEND_BOTH)
#include <numpy/arrayobject.h>
#include <python3.8/Python.h>
#endif

// MyData：调 my.data 的 get_basic_data，返回 my_basic_data（基础资料如债等），不做逐笔 .npq；嵌 Python，both 下另有 C++ 桩 GetBasicDataViaCpp。
class MyData {
public:
	std::vector<my_basic_data> GetBasicData(int date, std::string &data_name, std::string &data_type,
					      BackendTarget backend = io::kDefaultRead);

	MyData();
	~MyData();

private:
	std::vector<my_basic_data> GetBasicDataViaPython(int date, std::string &data_name, std::string &data_type);
#if defined(READER_BACKEND_BOTH)
	std::vector<my_basic_data> GetBasicDataViaCpp(int date, const std::string &data_name, const std::string &data_type);
#endif

#if defined(READER_BACKEND_PYTHON_ONLY) || defined(READER_BACKEND_BOTH)
	PyObject *m;
	PyObject *data_module_;
#endif
};
