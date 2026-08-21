#pragma once

#include <hdf5.h>

#include <vector>
#include <string>
#include <cstring>
#include <iostream>

// factors::hdf5_utils：封装常用的 HDF5 读写操作。
// 约定：
// - 1D/2D 数据集维度需与传入容器匹配；
// - 数值类型仅支持 float/double/int/long；
// - 失败返回 false，不抛异常；调用方负责句柄有效与维度检查。
namespace factors {
namespace hdf5_utils {

// 从HDF5文件中获取所有数据集名称
// file_id: HDF5文件ID
// result: 存储数据集名称的向量
// 返回: 成功返回true，失败返回false
bool GetDatasetNames(hid_t file_id, std::vector<std::string>& result);

// 从HDF5读取一维定长字符串数据集到vector<string>
// file_id: HDF5文件ID
// dataset_name: 数据集名称
// result: 存储字符串的向量
// 返回: 成功返回true，失败返回false
bool Load1DStringVector(hid_t file_id,
						const std::string& dataset_name,
						std::vector<std::string>& result);


// @brief 从 HDF5 读取 1-D 复合字符串数据集到 std::vector<std::string>（自动检测版本）
// 适用于复合结构体只包含一个 char[N] 字段的情况，自动检测字符串长度
//
// 使用方式：
// std::vector<std::string> codes;
// hdf5_utils::Load1DCompoundStringVectorAuto(file_id, "dataset_name", codes);
//
// 该函数会自动检测复合结构体的大小，并假设整个结构体就是一个字符串字段
// 适用于：CodeInfo{char code[12]}、FactorInfo{char factor[500]} 等
//
// @param file_id: HDF5文件ID
// @param dataset_name: 数据集名称
// @param result: 存储解析后的字符串向量
// @returns 成功返回true，失败返回false
bool Load1DCompoundStringVectorAuto(hid_t file_id,
									const std::string& dataset_name,
									std::vector<std::string>& result);


// 从HDF5加载二维数值数组，按字节形式存储（行优先）
// file_id: HDF5文件ID
// dataset_name: 数据集名称
// result: 存储数据的字节向量
// type_name: 数据类型名称（float/double/int/long）
// expected_cols: 期望的列数，0表示不检查
// 返回: 成功返回true，失败返回false
bool Load2DNumericToBytes(hid_t file_id,
						const std::string& dataset_name,
						std::vector<char>& result,
						std::string& type_name,
						int expected_cols = 0);

// 从HDF5读取二维数值数组，按字节形式存储（行优先）
// file_id: HDF5文件ID
// dataset_name: 数据集名称
// result: 存储数据的二维字节向量
// type_name: 数据类型名称（float/double/int/long）
// expected_cols: 期望的列数，0表示不检查
// 返回: 成功返回true，失败返回false
bool Load2DNumericTo2DBytes(hid_t file_id,
							const std::string& dataset_name,
							std::vector<std::vector<char>>& result,
							std::string& type_name,
							int expected_cols = 0);

// 从HDF5读取二维float数据集到vector<vector<float>>
// file_id: HDF5文件ID
// dataset_name: 数据集名称
// result: 存储二维float数据的向量
// expected_cols: 期望的列数，0表示不检查
// 返回: 成功返回true，失败返回false
bool Load2DFloatTo2DVector(hid_t file_id,
						   const std::string& dataset_name,
						   std::vector<std::vector<float>>& result,
						   size_t expected_cols = 0);

// 将一维字符串数组以定长字符串格式存入H5
// file_id: HDF5文件ID
// dataset_name: 数据集名称
// values: 要保存的字符串向量
// 返回: 成功返回true，失败返回false
bool Save1DStringVectorToH5(hid_t file_id,
						const std::string& dataset_name,
						const std::vector<std::string>& values);

// 将一维字符串数组以复合字符串格式存入H5
// file_id: HDF5文件ID
// dataset_name: 数据集名称
// values: 要保存的字符串向量
// 返回: 成功返回true，失败返回false
bool Save1DCompoundFactorStringVectorToH5(hid_t file_id,
									const std::string& dataset_name,
									const std::vector<std::string>& values);

// 将二维float数组存入H5（行优先存储）
// file_id: HDF5文件ID
// dataset_name: 数据集名称
// data: 要保存的二维float数据
// columns_len: 固定列宽，0表示使用第一行长度
// 返回: 成功返回true，失败返回false
bool Save2DFloatToH5(hid_t file_id,
					 const std::string& dataset_name,
					 const std::vector<std::vector<float>>& data,
					 size_t columns_len = 0);

// 从HDF5读取一维数值数据集到vector<T>
// 支持类型: float, double, int, long
template <typename T>
bool Load1DNumericVector(hid_t file_id,
						const std::string& dataset_name,
						std::vector<T>& result) {
	result.clear();
	static_assert(std::is_same<T, float>::value || std::is_same<T, double>::value ||
					 std::is_same<T, int>::value || std::is_same<T, long>::value,
				  "Unsupported numeric type");

	hid_t dataset = H5Dopen2(file_id, dataset_name.c_str(), H5P_DEFAULT);
	if (dataset < 0) {
		std::cerr << "Failed to open dataset: " << dataset_name << std::endl;  // 打开数据集失败
		return false;
	}

	hid_t space = H5Dget_space(dataset);
	hsize_t dims[1] = {0};
	int rank = H5Sget_simple_extent_dims(space, dims, nullptr);
	if (rank != 1) {
		H5Sclose(space);
		H5Dclose(dataset);
		return false;
	}

	hid_t mem_type = 0;
	if (std::is_same<T, float>::value)
		mem_type = H5T_NATIVE_FLOAT;
	else if (std::is_same<T, double>::value)
		mem_type = H5T_NATIVE_DOUBLE;
	else if (std::is_same<T, int>::value)
		mem_type = H5T_NATIVE_INT;
	else
		mem_type = H5T_NATIVE_LONG;

	result.resize(dims[0]);
	herr_t status = H5Dread(dataset, mem_type, H5S_ALL, H5S_ALL, H5P_DEFAULT, result.data());

	H5Sclose(space);
	H5Dclose(dataset);
	return status >= 0;
}

// 将一维数值数组存入H5
// 支持类型: float, double, int, long
template <typename T>
inline bool Save1DNumericVectorToH5(hid_t file_id,
									const std::string& dataset_name,
									const std::vector<T>& values) {
	if (values.empty()) {
		std::cerr << "Error: No data to save" << std::endl;
		return false;
	}

	// 只允许 float / double / int / long
	static_assert(std::is_same<T, float>::value  ||
				  std::is_same<T, double>::value ||
				  std::is_same<T, int>::value    ||
				  std::is_same<T, long>::value,
				  "Unsupported numeric type");

	hsize_t dims[1] = { values.size() };
	hid_t space = H5Screate_simple(1, dims, nullptr);
	if (space < 0) return false;

	hid_t mem_type = 0;
	if      (std::is_same<T, float>::value)  mem_type = H5T_NATIVE_FLOAT;
	else if (std::is_same<T, double>::value) mem_type = H5T_NATIVE_DOUBLE;
	else if (std::is_same<T, int>::value)    mem_type = H5T_NATIVE_INT;
	else                                     mem_type = H5T_NATIVE_LONG;

	hid_t dataset = H5Dcreate2(file_id, dataset_name.c_str(), mem_type, space,
							H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
	if (dataset < 0) { H5Sclose(space); return false; }

	herr_t status = H5Dwrite(dataset, mem_type, H5S_ALL, H5S_ALL,
							 H5P_DEFAULT, values.data());

	H5Dclose(dataset);
	H5Sclose(space);
	return status >= 0;
}

// 将二维数值数组存入H5
// 支持类型: float, double, int, long
template <typename T>
inline bool Save2DNumericVectorToH5(hid_t file_id,
									const std::string& dataset_name,
									const std::vector<std::vector<T>>& data,
									size_t columns_len = 0) {
	if (data.empty()) {
		std::cerr << "Error: No data to save" << std::endl;
		return false;
	}

	// 只允许 float / double / int / long
	static_assert(std::is_same<T, float>::value  ||
				  std::is_same<T, double>::value ||
				  std::is_same<T, int>::value    ||
				  std::is_same<T, long>::value,
				  "Unsupported numeric type");

	// 计算维度
	hsize_t row = data.size();
	hsize_t column;
	if (columns_len == 0) {
		column = data[0].size();
	} else {
		column = columns_len;
	}

	if (row == 0 || column == 0) {
		std::cerr << "Error: Invalid dimensions - rows: " << row << ", columns: " << column << std::endl;
		return false;
	}

	// 创建连续的数据缓冲区
	std::vector<T> buffer(row * column);

	// 将二维数据复制到一维缓冲区
	for (hsize_t i = 0; i < row; ++i) {
		if (data[i].size() < column) {
			std::cerr << "Error: Row " << i << " has insufficient data: " << data[i].size() << " < "
					<< column << std::endl;
			return false;
		}
		for (hsize_t j = 0; j < column; ++j) {
			buffer[i * column + j] = data[i][j];
		}
	}

	hid_t f_tid = 0;
	if      (std::is_same<T, float>::value)  f_tid = H5T_NATIVE_FLOAT;
	else if (std::is_same<T, double>::value) f_tid = H5T_NATIVE_DOUBLE;
	else if (std::is_same<T, int>::value)    f_tid = H5T_NATIVE_INT;
	else                                     f_tid = H5T_NATIVE_LONG;

	hsize_t dims[] = { static_cast<hsize_t>(row), static_cast<hsize_t>(column) };

	hid_t space = H5Screate_simple(2, dims, NULL);
	if (space < 0) return false;

	hid_t dataset = H5Dcreate2(file_id, dataset_name.c_str(), f_tid, space, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
	if (dataset < 0) { H5Sclose(space); return false; }

	// 写入数据
	herr_t status = H5Dwrite(dataset, f_tid, H5S_ALL, H5S_ALL, H5P_DEFAULT, buffer.data());

	if (status < 0) {
		std::cerr << "Error writing dataset: " << dataset_name << std::endl;
	} else {
		std::cout << "Dataset '" << dataset_name << "' saved with 2-D dimensions: [" << row << ", " << column << "]" << std::endl;
	}

	H5Dclose(dataset);
	H5Sclose(space);
	return status >= 0;
}

} // namespace hdf5_utils
} // namespace factors