#define NPY_NO_DEPRECATED_API NPY_1_7_API_VERSION
#include "my_data.h"

#include <cstring>
#include <iostream>

namespace {

BackendTarget NormalizeMyDataBackend(const char * /*api*/, BackendTarget b) {
	return b;
}

} // namespace

#if defined(READER_BACKEND_PYTHON_ONLY) || defined(READER_BACKEND_BOTH)

MyData::MyData() {
	if (!Py_IsInitialized()) {
		Py_Initialize();
	}

	m = PyImport_ImportModule("my.data.basic_func");
	if (m == NULL) {
		std::cerr << "MyData: import my.data.basic_func 失败。\n";
		PyErr_Print();
	}
	data_module_ = PyObject_GetAttrString(m, "get_basic_data");
	if (!data_module_) {
		std::cerr << "MyData: 取得 get_basic_data 失败。\n";
		PyErr_Print();
	}
}

MyData::~MyData() {
	if (Py_IsInitialized()) {
		Py_DECREF(data_module_);
		Py_DECREF(m);
		Py_Finalize();
	}
}

std::vector<my_basic_data> MyData::GetBasicDataViaPython(int date, std::string &data_name, std::string &data_type) {
	std::vector<my_basic_data> empty;
	if (!data_module_)
		return empty;

	PyObject *args = PyTuple_New(2);
	PyObject *py_date = Py_BuildValue("I", date);
	PyObject *py_data_name = Py_BuildValue("s", data_name.c_str());
	PyObject *kwargs = PyDict_New();
	PyDict_SetItemString(kwargs, "data_type", PyUnicode_FromString(data_type.c_str()));

	Py_XINCREF(py_date);
	Py_XINCREF(py_data_name);
	Py_XINCREF(kwargs);
	PyTuple_SET_ITEM(args, 0, py_date);
	PyTuple_SET_ITEM(args, 1, py_data_name);

	PyObject *ret = PyObject_Call(data_module_, args, kwargs);
	if (ret == Py_None) {
		Py_DECREF(ret);
		Py_DECREF(py_date);
		Py_DECREF(py_data_name);
		Py_DECREF(kwargs);
		return empty;
	}
	PyArrayObject *data_arr = (PyArrayObject *)ret;
	if (!PyArray_IS_C_CONTIGUOUS(data_arr))
		data_arr = (PyArrayObject *)PyArray_NewCopy(data_arr, NPY_CORDER);
	npy_intp *ai1_shape = PyArray_SHAPE(data_arr);
	char *datas = PyArray_BYTES(data_arr);
	std::vector<my_basic_data> result(static_cast<size_t>(ai1_shape[0]));
	memcpy(result.data(), datas, static_cast<size_t>(ai1_shape[0]) * sizeof(my_basic_data));
	Py_DECREF(ret);
	Py_DECREF(py_date);
	Py_DECREF(py_data_name);
	Py_DECREF(kwargs);
	return result;
}

#if defined(READER_BACKEND_BOTH)
std::vector<my_basic_data> MyData::GetBasicDataViaCpp(int date, const std::string &data_name,
						      const std::string &data_type) {
	(void)date;
	(void)data_name;
	(void)data_type;
	std::cerr << "MyData::GetBasicDataViaCpp: 未链接 Apache Arrow C++（RecordBatch / NpqData.ARROW_*），C++ 直读失败。"
		     " 请改用 GetBasicData(..., BackendTarget::Python)（嵌入 Python）。\n";
	return {};
}
#endif

std::vector<my_basic_data> MyData::GetBasicData(int date, std::string &data_name, std::string &data_type,
						   BackendTarget backend) {
	BackendTarget b = NormalizeMyDataBackend("GetBasicData", backend);
#if defined(READER_BACKEND_PYTHON_ONLY)
	if (b == BackendTarget::Cpp) {
		std::cerr << "MyData::GetBasicData: python_only 构建无 Arrow/C++ basic 直读；已使用 Python。\n";
	}
	return GetBasicDataViaPython(date, data_name, data_type);
#else
	if (b == BackendTarget::Cpp)
		return GetBasicDataViaCpp(date, data_name, data_type);
	if (b == BackendTarget::Python)
		return GetBasicDataViaPython(date, data_name, data_type);
	// Default / Both：与 quote_reader 顺序一致，Arrow 空则 Python
	using quote_reader::Order;
	if (quote_reader::current_order() == Order::CppFirst) {
		std::vector<my_basic_data> v = GetBasicDataViaCpp(date, data_name, data_type);
		if (!v.empty())
			return v;
		return GetBasicDataViaPython(date, data_name, data_type);
	}
	std::vector<my_basic_data> py = GetBasicDataViaPython(date, data_name, data_type);
	if (!py.empty())
		return py;
	return GetBasicDataViaCpp(date, data_name, data_type);
#endif
}

#endif // PYTHON_ONLY || BOTH
