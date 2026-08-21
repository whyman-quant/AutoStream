#include "hdf5_utils.h"

namespace hdf5_utils {

// @brief 从 HDF5 文件中获取所有数据集名称
bool GetDatasetNames(hid_t file_id, std::vector<std::string>& result) {
	result.clear();

	// 1. 打开根组
	hid_t root_group = H5Gopen2(file_id, "/", H5P_DEFAULT);
	if (root_group < 0) {
		std::cerr << "Failed to open root group" << std::endl;
		return false;
	}

	// 2. 遍历根组中的所有对象
	hsize_t num_objects = 0;
	herr_t status = H5Gget_num_objs(root_group, &num_objects);
	if (status < 0) {
		std::cerr << "Failed to get number of objects in root group" << std::endl;
		H5Gclose(root_group);
		return false;
	}

	// 3. 遍历每个对象，检查是否为数据集
	for (hsize_t i = 0; i < num_objects; ++i) {
		// 获取对象名称
		char obj_name[256];
		ssize_t name_len = H5Gget_objname_by_idx(root_group, i, obj_name, sizeof(obj_name));
		if (name_len < 0) {
			std::cerr << "Failed to get object name at index " << i << std::endl;
			continue;
		}

		// 获取对象类型
		H5G_obj_t obj_type = H5Gget_objtype_by_idx(root_group, i);
		if (obj_type == H5G_DATASET) {
			// 如果是数据集，添加到结果中
			result.push_back(std::string(obj_name));
		}
	}

	// 4. 关闭根组
	H5Gclose(root_group);

	return true;
}

// @brief 从 HDF5 读取 1-D 定长字符串数据集 -> std::vector<std::string>
bool Load1DStringVector(hid_t file_id,
						const std::string& dataset_name,
						std::vector<std::string>& result) {
	// 1. 打开数据集
	// 使用 H5Dopen2 打开文件内指定路径的数据集；返回值 <0 表示失败。
	hid_t dataset = H5Dopen2(file_id, dataset_name.c_str(), H5P_DEFAULT);
	if (dataset < 0) {
		std::cerr << "Failed to open dataset: " << dataset_name << std::endl;  // 打开数据集失败
		return false;
	}

	// 2. 取得数据空间（shape）
	// 通过 H5Dget_space 拿到 dataspace（相当于 numpy 的 shape）。
	hid_t space = H5Dget_space(dataset);
	hsize_t dims[1] = {0};
	// 对于一维字符串数组，rank 应为 1，dims[0] 就是元素个数。
	int rank = H5Sget_simple_extent_dims(space, dims, nullptr);
	if (rank != 1) {
		std::cerr << "Dataset is not 1-D: " << dataset_name << ", rank: " << rank << std::endl;
		H5Sclose(space);
		H5Dclose(dataset);
		return false;
	}

	// 3. 取得数据类型并确认是定长字符串
	// dtype 是在文件里实际存储的“声明类型”
	hid_t dtype = H5Dget_type(dataset);
	// 转成当前内存可读写类型
	hid_t str_type = H5Tget_native_type(dtype, H5T_DIR_ASCEND);
	if (H5Tget_class(str_type) != H5T_STRING || H5Tis_variable_str(str_type)) {
		std::cerr << "Dataset is not fixed-length string: " << dataset_name << std::endl;
		H5Tclose(str_type);
		H5Tclose(dtype);
		H5Sclose(space);
		H5Dclose(dataset);
		return false;
	}

	// 4. 一次性读整块数据
	// 取得单个字符串的固定长度
	size_t str_len = H5Tget_size(str_type);
	std::vector<char> buf(dims[0] * str_len);
	herr_t status = H5Dread(dataset, str_type, H5S_ALL, H5S_ALL, H5P_DEFAULT, buf.data());
	if (status < 0) {
		std::cerr << "Failed to read dataset: " << dataset_name << std::endl;
		H5Tclose(str_type);
		H5Tclose(dtype);
		H5Sclose(space);
		H5Dclose(dataset);
	return false;
	}

	// 5. 解析并裁剪尾部 '\0'
	result.clear();
	result.reserve(dims[0]);
	for (hsize_t i = 0; i < dims[0]; ++i) {
		const char* ptr = buf.data() + i * str_len;
		// 用 strnlen 防止 '\0' 之后继续读
		result.emplace_back(ptr, strnlen(ptr, str_len));
	}

	// 6. 清理 HDF5 资源：先开后关，后开先关，防止泄漏。
	H5Tclose(str_type);
	H5Tclose(dtype);
	H5Sclose(space);
	H5Dclose(dataset);
	return true;
}

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
									std::vector<std::string>& result) {
	// 打开数据集
	hid_t dataset = H5Dopen2(file_id, dataset_name.c_str(), H5P_DEFAULT);
	if (dataset < 0) {
		std::cerr << "Failed to open dataset: " << dataset_name << std::endl;
		return false;
	}

	// 获取数据空间信息
	hid_t space = H5Dget_space(dataset);
	hsize_t dims[1];
	int rank = H5Sget_simple_extent_dims(space, dims, NULL);
	if (rank != 1) {
		std::cerr << "Dataset is not 1-D: " << dataset_name << ", rank: " << rank << std::endl;
		H5Sclose(space);
		H5Dclose(dataset);
		return false;
	}

	// 获取数据类型信息
	hid_t dtype = H5Dget_type(dataset);
	hid_t native_type = H5Tget_native_type(dtype, H5T_DIR_ASCEND);
	size_t struct_size = H5Tget_size(native_type);

	// 检查是否为复合类型
	if (H5Tget_class(native_type) != H5T_COMPOUND) {
		std::cerr << "Dataset is not compound type: " << dataset_name << std::endl;
		H5Tclose(native_type);
		H5Tclose(dtype);
		H5Sclose(space);
		H5Dclose(dataset);
		return false;
	}

	// 获取复合类型的成员数量
	int n_members = H5Tget_nmembers(native_type);
	if (n_members != 1) {
		std::cerr << "Compound type has " << n_members << " members, expected 1" << std::endl;
		H5Tclose(native_type);
		H5Tclose(dtype);
		H5Sclose(space);
		H5Dclose(dataset);
		return false;
	}

	// 获取第一个成员的信息
	char* member_name = H5Tget_member_name(native_type, 0);
	hid_t member_type = H5Tget_member_type(native_type, 0);
	size_t member_offset = H5Tget_member_offset(native_type, 0);
	size_t member_size = H5Tget_size(member_type);

	// 检查成员是否为字符串类型
	if (H5Tget_class(member_type) != H5T_STRING) {
		std::cerr << "Member is not string type" << std::endl;
		free(member_name);
		H5Tclose(member_type);
		H5Tclose(native_type);
		H5Tclose(dtype);
		H5Sclose(space);
		H5Dclose(dataset);
		return false;
	}

	// 读取数据
	std::vector<char> raw_buffer(dims[0] * struct_size);
	herr_t status = H5Dread(dataset, dtype, H5S_ALL, H5S_ALL, H5P_DEFAULT, raw_buffer.data());

	if (status >= 0) {
		result.clear();
		result.reserve(dims[0]);
		for (hsize_t i = 0; i < dims[0]; ++i) {
			// 从正确的偏移量读取字符串
			const char* str_ptr = raw_buffer.data() + i * struct_size + member_offset;
			// 使用strnlen找到实际字符串长度，避免读取多余的0字符
			size_t actual_len = strnlen(str_ptr, member_size);
			std::string str(str_ptr, actual_len);
			// 移除末尾的空格
			str.erase(str.find_last_not_of(" ") + 1);
			if (!str.empty()) {
				result.emplace_back(str);
			}
		}
	} else {
		std::cerr << "Failed to read dataset: " << dataset_name << std::endl;
	}

	// 清理资源
	free(member_name);
	H5Tclose(member_type);
	H5Tclose(native_type);
	H5Tclose(dtype);
	H5Sclose(space);
	H5Dclose(dataset);

	return status >= 0;
}


// 从 HDF5 数据集中加载二维数值型数组，按字节形式存储到 result（行优先），并且告知数据类型名称。
// @param file_id: HDF5文件ID
// @param dataset_name: 数据集名称
// @param result: 存储加载的数据（char*形式）
// @param type_name: 数据类型名称，支持 float/double/int/long
// @param expected_cols: 期望的列数
// @returns 返回值: 成功加载返回true，否则返回false
bool Load2DNumericToBytes(hid_t file_id,
						const std::string& dataset_name,
						std::vector<char>& result,
						std::string& type_name,
						int expected_cols)  {
	// 1. 打开数据集
	hid_t dataset = H5Dopen2(file_id, dataset_name.c_str(), H5P_DEFAULT);
	if (dataset < 0) {
		std::cerr << "Failed to open dataset: " << dataset_name << std::endl;  // 打开数据集失败
		return false;
	}

	// 2. 获取数据空间信息
	hid_t space = H5Dget_space(dataset);
	hsize_t dims[2] = {0, 0};
	int rank = H5Sget_simple_extent_dims(space, dims, nullptr);

	// 维度必须是 2-D
	if (rank != 2) {
		std::cerr << "Dataset " << dataset_name
					<< " is not 2-D (rank = " << rank << ")" << std::endl;
		H5Sclose(space);
		H5Dclose(dataset);
		return false;
	}

	// 只在 expected_cols > 0 时检查列数
	if (expected_cols > 0 && dims[1] != static_cast<hsize_t>(expected_cols)) {
		std::cerr << "Dataset " << dataset_name
					<< " has " << dims[1] << " columns, expected "
					<< expected_cols << std::endl;
		H5Sclose(space);
		H5Dclose(dataset);
		return false;
	}

	// 3. 取数据类型
	hid_t dtype = H5Dget_type(dataset);
	hid_t native_type = H5Tget_native_type(dtype, H5T_DIR_ASCEND);

	bool ok = false;
	type_name = "unknown";

	// 检查数据类型，支持 float/double/int/long
	if (H5Tequal(native_type, H5T_NATIVE_FLOAT) > 0) {
		type_name = "float";
	} else if (H5Tequal(native_type, H5T_NATIVE_DOUBLE) > 0) {
		type_name = "double";
	} else if (H5Tequal(native_type, H5T_NATIVE_INT) > 0) {
		type_name = "int";
	} else if (H5Tequal(native_type, H5T_NATIVE_LONG) > 0) {
		type_name = "long";
	} else {
		// 不支持的类型
		std::cerr << "Dataset " << dataset_name << " 不支持的数据类型，dtype=" << dtype
			<< ", native_type=" << native_type << std::endl;
		H5Tclose(native_type);
		H5Tclose(dtype);
		H5Sclose(space);
		H5Dclose(dataset);
		return false;
	}

	if (type_name == "float" || type_name == "double" || type_name == "int" || type_name == "long") {
		size_t elem_bytes = H5Tget_size(native_type);
		size_t n_elem = dims[0] * dims[1];
		result.resize(n_elem * elem_bytes);

		herr_t status = H5Dread(dataset, native_type, H5S_ALL, H5S_ALL,
								H5P_DEFAULT, result.data());
		ok = (status >= 0);
	}

	H5Tclose(native_type);
	H5Tclose(dtype);
	H5Sclose(space);
	H5Dclose(dataset);
	return ok;
}

// 从 HDF5 数据集中加载二维数值型数组，按字节形式存储到 result（行优先），并且告知数据类型名称。
// @param file_id: HDF5文件ID
// @param dataset_name: 数据集名称
// @param result: 存储加载的数据（二维字节向量）
// @param type_name: 数据类型名称，支持 float/double/int/long
// @param expected_cols: 期望的列数
// @returns 返回值: 成功加载返回true，否则返回false
bool Load2DNumericTo2DBytes(hid_t file_id,
							const std::string& dataset_name,
							std::vector<std::vector<char>>& result,
							std::string& type_name,
							int expected_cols) {
	result.clear();
	type_name = "unknown";

	// 1. 打开数据集
	hid_t dataset = H5Dopen2(file_id, dataset_name.c_str(), H5P_DEFAULT);
	if (dataset < 0) {
		std::cerr << "Failed to open dataset: " << dataset_name << std::endl;
		return false;
	}

	// 2. 获取 dataspace
	hid_t space = H5Dget_space(dataset);
	hsize_t dims[2] = {0, 0};
	int rank = H5Sget_simple_extent_dims(space, dims, nullptr);

	if (rank != 2) {
		std::cerr << "Dataset " << dataset_name << " is not 2-D\n";
		H5Sclose(space);
		H5Dclose(dataset);
		return false;
	}
	if (expected_cols > 0 && dims[1] != static_cast<hsize_t>(expected_cols)) {
		std::cerr << "Column mismatch: " << dims[1] << " vs " << expected_cols << "\n";
		H5Sclose(space);
		H5Dclose(dataset);
		return false;
	}

	// 3. 判断类型
	hid_t dtype = H5Dget_type(dataset);
	hid_t native_type = H5Tget_native_type(dtype, H5T_DIR_ASCEND);

	bool ok = false;

	// 支持类型: float/double/int/long
	if (H5Tequal(native_type, H5T_NATIVE_FLOAT) > 0) {
		type_name = "float";
	} else if (H5Tequal(native_type, H5T_NATIVE_DOUBLE) > 0) {
		type_name = "double";
	} else if (H5Tequal(native_type, H5T_NATIVE_INT) > 0) {
		type_name = "int";
	} else if (H5Tequal(native_type, H5T_NATIVE_LONG) > 0) {
		type_name = "long";
	} else {
		std::cerr << "Dataset " << dataset_name << " 不支持的数据类型，dtype=" << dtype
				  << ", native_type=" << native_type << std::endl;
		H5Tclose(native_type);
		H5Tclose(dtype);
		H5Sclose(space);
		H5Dclose(dataset);
		return false;
	}

	size_t elem_bytes = H5Tget_size(native_type);
	size_t n_rows = dims[0];
	size_t n_cols = dims[1];

	if (n_rows == 0 || n_cols == 0) {
		result.clear();
		ok = true;
	} else {
		std::vector<char> flat(n_rows * n_cols * elem_bytes);

		herr_t status = H5Dread(dataset, native_type, H5S_ALL, H5S_ALL,
								H5P_DEFAULT, flat.data());
		ok = (status >= 0);

		if (ok) {
			result.resize(n_rows);
			for (size_t i = 0; i < n_rows; ++i) {
				result[i].assign(
					flat.begin() + i * n_cols * elem_bytes,
					flat.begin() + (i + 1) * n_cols * elem_bytes
				);
			}
		}
	}

	H5Tclose(native_type);
	H5Tclose(dtype);
	H5Sclose(space);
	H5Dclose(dataset);
	return ok;
}

// 从HDF5读取二维float数据集到vector<vector<float>>
bool Load2DFloatTo2DVector(hid_t file_id,
						   const std::string& dataset_name,
						   std::vector<std::vector<float>>& result,
						   size_t expected_cols) {
	result.clear();
	hid_t dataset = H5Dopen2(file_id, dataset_name.c_str(), H5P_DEFAULT);
	if (dataset < 0) {
		std::cerr << "Failed to open dataset: " << dataset_name << std::endl;  // 打开数据集失败
		return false;
	}

	hid_t space = H5Dget_space(dataset);
	hsize_t dims[2] = {0, 0};
	int rank = H5Sget_simple_extent_dims(space, dims, nullptr);

	if (rank != 2) {
		std::cerr << "Dataset " << dataset_name << " is not 2-D\n";
		H5Sclose(space); H5Dclose(dataset);
		return false;
	}
	if (expected_cols > 0 && dims[1] != expected_cols) {
		std::cerr << "Column mismatch: " << dims[1] << " vs " << expected_cols << "\n";
		H5Sclose(space); H5Dclose(dataset);
		return false;
	}

	hid_t dtype = H5Dget_type(dataset);
	hid_t native = H5Tget_native_type(dtype, H5T_DIR_ASCEND);
	if (H5Tget_class(native) != H5T_FLOAT) {
		std::cerr << "Dataset is not float\n";
		H5Tclose(native); H5Tclose(dtype);
		H5Sclose(space); H5Dclose(dataset);
		return false;
	}

	std::vector<float> buf(dims[0] * dims[1]);
	herr_t st = H5Dread(dataset, native, H5S_ALL, H5S_ALL, H5P_DEFAULT, buf.data());
	bool ok = (st >= 0);

	if (ok) {
		result.resize(dims[0]);
		for (hsize_t i = 0; i < dims[0]; ++i) {
			result[i].assign(buf.begin() + i * dims[1],
								buf.begin() + (i + 1) * dims[1]);
		}
	}

	H5Tclose(native); H5Tclose(dtype);
	H5Sclose(space); H5Dclose(dataset);
	return ok;
}

// 将一维字符串数组以定长字符串格式存入H5
bool Save1DStringVectorToH5(hid_t file_id,
							const std::string& dataset_name,
							const std::vector<std::string>& values) {
	if (values.empty()) {
		std::cerr << "Error: No data to save" << std::endl;
		return false;
	}

	// 最长字符串长度
	size_t max_len = 0;
	for (const auto& s : values)
		max_len = std::max(max_len, s.size());
	if (max_len == 0) max_len = 1;

	// dataspace
	hsize_t dims[1] = { values.size() };
	hid_t space = H5Screate_simple(1, dims, nullptr);
	if (space < 0) return false;

	// 定长字符串类型
	hid_t str_type = H5Tcopy(H5T_FORTRAN_S1);
	H5Tset_size(str_type, max_len);
	H5Tset_strpad(str_type, H5T_STR_NULLPAD);

	// 数据集
	hid_t dataset = H5Dcreate2(file_id, dataset_name.c_str(), str_type, space,
								H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
	if (dataset < 0) {
		H5Tclose(str_type);
		H5Sclose(space);
		return false;
	}

	// 写入
	std::vector<char> buf(values.size() * max_len, '\0');
	for (size_t i = 0; i < values.size(); ++i)
		std::memcpy(buf.data() + i * max_len,
					values[i].c_str(),
					values[i].size());

	herr_t status = H5Dwrite(dataset, str_type, H5S_ALL, H5S_ALL,
							 H5P_DEFAULT, buf.data());

	// 清理
	H5Dclose(dataset);
	H5Tclose(str_type);
	H5Sclose(space);
	return status >= 0;
}

// 将一维字符串数组以Compound字符串格式存入H5
bool Save1DCompoundFactorStringVectorToH5(hid_t file_id,
									const std::string& dataset_name,
									const std::vector<std::string>& values) {
	if (values.empty()) {
		std::cerr << "Error: No data to save" << std::endl;
		return false;
	}

	// dataspace
	hsize_t dims[1] = { values.size() };
	hid_t space = H5Screate_simple(1, dims, NULL);
	if (space < 0) return false;

	// Compound字符串类型
	typedef struct {
		char factor[500];
	} string_t;
	hid_t s1_tid = H5Tcreate(H5T_COMPOUND, sizeof(string_t));
	hid_t str_type = H5Tcopy(H5T_FORTRAN_S1);
	H5Tset_size(str_type, 500);
	H5Tinsert(s1_tid, "factor", HOFFSET(string_t, factor), str_type);

	// 数据集
	hid_t dataset = H5Dcreate2(file_id, dataset_name.c_str(), s1_tid, space,
							   H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
	if (dataset < 0) {
		H5Tclose(s1_tid);
		H5Tclose(str_type);
		H5Sclose(space);
		return false;
	}

	// 写入
	std::vector<string_t> s_buff(values.size());
	for (size_t i = 0; i < values.size(); ++i) {
		strncpy(s_buff[i].factor, values[i].c_str(), 499);
		s_buff[i].factor[499] = '\0';
	}
	herr_t status = H5Dwrite(dataset, s1_tid, H5S_ALL, H5S_ALL, H5P_DEFAULT, s_buff.data());

	// 清理
	H5Dclose(dataset);
	H5Tclose(s1_tid);
	H5Tclose(str_type);
	H5Sclose(space);
	return status >= 0;
}

// 将二维float数组存入H5
bool Save2DFloatToH5(hid_t file_id,
					 const std::string& dataset_name,
					 const std::vector<std::vector<float>>& data,
					 size_t columns_len) {
	if (data.empty()) {
		std::cerr << "Error: No data to save" << std::endl;
		return false;
	}

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
	std::vector<float> buffer(row * column);

	// 将二维数据复制到一维缓冲区
	for (hsize_t i = 0; i < row; ++i) {
		if (data[i].size() < column) {
			std::cerr << "Error: Row " << i << " has insufficient data: " << data[i].size() << " < "
					<< column << std::endl;
			return false;
		}
		for (hsize_t j = 0; j < column; ++j) {
			buffer[i * column + j] = static_cast<float>(data[i][j]);
		}
	}

	hid_t f_tid = H5T_NATIVE_FLOAT;
	hsize_t dims[] = { static_cast<hsize_t>(row), static_cast<hsize_t>(column) };

	hid_t space = H5Screate_simple(2, dims, NULL);
	hid_t dataset = H5Dcreate2(file_id, dataset_name.c_str(), f_tid, space, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);

	// 写入数据
	herr_t status = H5Dwrite(dataset, f_tid, H5S_ALL, H5S_ALL, H5P_DEFAULT, buffer.data());

	if (status < 0) {
		std::cerr << "Error writing dataset: " << dataset_name << std::endl;
	} else {
		std::cout << "Data saved with 2-D dimensions: " << row << ", " << column << std::endl;
	}

	H5Dclose(dataset);
	H5Sclose(space);
	return status >= 0;
}

} // hdf5_utils