#include "general.h"

#include <stdio.h>

#include <algorithm>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "hdf5.h"

using namespace std;

string AllDataDir = "/mnt/beegfs_ssd_raid91/706_wgh_new/wgh_team_share/data/AllData";

bool get_trading_date_range(int begt, int endt, vector<int> &dates) {
	hid_t file, dataset; /* Handles */

	string filepath = AllDataDir + "/BasicInfo/CommInfo.h5";
	file = H5Fopen(filepath.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT);
	dataset = H5Dopen2(file, "alldays", H5P_DEFAULT);
	hid_t datatype = H5Dget_type(dataset);   /* datatype handle */
	hid_t dataspace = H5Dget_space(dataset); /* dataspace handle */
	int rank = H5Sget_simple_extent_ndims(dataspace);
	vector<hsize_t> dims_out(static_cast<size_t>(rank)); /* dataset dimensions */
	int status_n = H5Sget_simple_extent_dims(dataspace, dims_out.data(), NULL);
	(void)status_n;
	// printf("testing rank %d, dimensions %lu\n", rank, (unsigned
	// long)(dims_out[0]));
	size_t len = dims_out[0] * dims_out[1];
	char **data = new char *[len];
	int status_r = H5Dread(dataset, datatype, H5S_ALL, dataspace, H5P_DEFAULT, data);
	(void)status_r;
	for (size_t i = 0; i < len; ++i) {
		int dateint = atoi(data[i]);
		if (dateint >= begt && dateint <= endt) {
			dates.push_back(dateint);
		}
	}
	H5Sclose(dataspace);
	H5Dclose(dataset);
	H5Fclose(file);
	return true;
}

bool get_codes(const string &date, vector<string> &codes_all, vector<string> &codes_nokcb) {
	hid_t s2_tid; /* Memory datatype handle */
	hid_t file, dataset; /* Handles */
	herr_t status;
	typedef struct code_info {
		char code[12];
	} Code_Info;

	string filepath = AllDataDir + "/BasicData/" + date + "/codes.h5";
	cout << filepath << endl;
	file = H5Fopen(filepath.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT);

	// datatype
	s2_tid = H5Tcreate(H5T_COMPOUND, sizeof(Code_Info));
	hid_t code_stringType = H5Tcopy(H5T_FORTRAN_S1);
	H5Tset_size(code_stringType, 10);
	H5Tinsert(s2_tid, "code", HOFFSET(Code_Info, code), code_stringType);

	dataset = H5Dopen2(file, "codes_all", H5P_DEFAULT);
	hid_t dataspace = H5Dget_space(dataset); /* dataspace handle */
	int rank = H5Sget_simple_extent_ndims(dataspace);
	vector<hsize_t> dims_out(static_cast<size_t>(rank)); /* dataset dimensions */
	int status_n = H5Sget_simple_extent_dims(dataspace, dims_out.data(), NULL);
	(void)status_n;
	size_t LENGTH = dims_out[0];
	vector<Code_Info> codes_all_tmp;
	codes_all_tmp.resize(LENGTH);
	status = H5Dread(dataset, s2_tid, dataspace, H5S_ALL, H5P_DEFAULT, &codes_all_tmp[0]);
	(void)status;
	for (size_t i = 0; i < LENGTH; ++i) {
		string c = codes_all_tmp[i].code;
		codes_all.push_back(c.substr(0, 9));
	}

	dataset = H5Dopen2(file, "codes_nokcb", H5P_DEFAULT);
	dataspace = H5Dget_space(dataset); /* dataspace handle */
	rank = H5Sget_simple_extent_ndims(dataspace);
	dims_out.resize(static_cast<size_t>(rank));
	status_n = H5Sget_simple_extent_dims(dataspace, dims_out.data(), NULL);
	(void)status_n;
	LENGTH = dims_out[0];
	vector<Code_Info> codes_nokcb_tmp;
	codes_nokcb_tmp.resize(LENGTH);
	status = H5Dread(dataset, s2_tid, dataspace, H5S_ALL, H5P_DEFAULT, &codes_nokcb_tmp[0]);
	(void)status;
	for (size_t i = 0; i < LENGTH; ++i) {
		string c = codes_nokcb_tmp[i].code;
		codes_nokcb.push_back(c.substr(0, 9));
	}

	H5Tclose(s2_tid);
	H5Sclose(dataspace);
	H5Dclose(dataset);
	H5Fclose(file);
	return true;
}

bool get_codes_kzz(const string &date, vector<string> &codes_kzz) {
	hid_t s2_tid; /* Memory datatype handle */
	hid_t file, dataset; /* Handles */
	herr_t status;
	typedef struct code_info {
		char code[12];
	} Code_Info;

	string filepath = AllDataDir + "/BasicData/" + date + "/codes_kzz.h5";
	cout << filepath << endl;
	file = H5Fopen(filepath.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT);

	// datatype
	s2_tid = H5Tcreate(H5T_COMPOUND, sizeof(Code_Info));
	hid_t code_stringType = H5Tcopy(H5T_FORTRAN_S1);
	H5Tset_size(code_stringType, 10);
	H5Tinsert(s2_tid, "code", HOFFSET(Code_Info, code), code_stringType);

	dataset = H5Dopen2(file, "codes_all", H5P_DEFAULT);
	hid_t dataspace = H5Dget_space(dataset); /* dataspace handle */
	int rank = H5Sget_simple_extent_ndims(dataspace);
	vector<hsize_t> dims_out(static_cast<size_t>(rank)); /* dataset dimensions */
	int status_n = H5Sget_simple_extent_dims(dataspace, dims_out.data(), NULL);
	(void)status_n;
	size_t LENGTH = dims_out[0];
	vector<Code_Info> codes_all_tmp;
	codes_all_tmp.resize(LENGTH);
	status = H5Dread(dataset, s2_tid, dataspace, H5S_ALL, H5P_DEFAULT, &codes_all_tmp[0]);
	(void)status;
	for (size_t i = 0; i < LENGTH; ++i) {
		string c = codes_all_tmp[i].code;
		codes_kzz.push_back(c.substr(0, 9));
	}

	H5Tclose(s2_tid);
	H5Sclose(dataspace);
	H5Dclose(dataset);
	H5Fclose(file);
	return true;
}

bool get_codes_from_file(const std::string &file_path, std::string dataset_name, std::vector<std::string> &codes) {
	// 打开HDF5文件（只读模式）
	hid_t file_id = H5Fopen(file_path.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT);
	if (file_id < 0) {
		fprintf(stderr, "无法打开文件: %s\n", file_path.c_str());
		return false;
	}

	// 工程链接 HDF5 1.10 C API（cmake/config_hdf5.cmake）：H5Oexists_by_name 为三参数形式；
	// 先判断对象是否存在，再用 H5Oget_info_by_name 确认 type 为 dataset，避免 H5Dopen2 失败时打印冗长 HDF5-DIAG。
	htri_t obj_exists = H5Oexists_by_name(file_id, dataset_name.c_str(), H5P_DEFAULT);
	if (obj_exists == 0) {
		H5Fclose(file_id);
		return false;
	}
	if (obj_exists < 0) {
		fprintf(stderr, "检测对象是否存在失败: %s\n", dataset_name.c_str());
		H5Fclose(file_id);
		return false;
	}
	H5O_info_t oinfo;
	std::memset(&oinfo, 0, sizeof(oinfo));
	if (H5Oget_info_by_name(file_id, dataset_name.c_str(), &oinfo, H5P_DEFAULT) < 0) {
		fprintf(stderr, "读取对象信息失败: %s\n", dataset_name.c_str());
		H5Fclose(file_id);
		return false;
	}
	if (oinfo.type != H5O_TYPE_DATASET) {
		H5Fclose(file_id);
		return false;
	}

	// 打开指定数据集
	hid_t dataset_id = H5Dopen2(file_id, dataset_name.c_str(), H5P_DEFAULT);
	if (dataset_id < 0) {
		fprintf(stderr, "无法打开数据集: %s\n", dataset_name.c_str());
		H5Fclose(file_id);
		return false;
	}

	// 获取数据空间信息
	hid_t dataspace_id = H5Dget_space(dataset_id);
	int ndims = H5Sget_simple_extent_ndims(dataspace_id);
	vector<hsize_t> dims(static_cast<size_t>(ndims));
	H5Sget_simple_extent_dims(dataspace_id, dims.data(), NULL);

	struct CodeInfo {
		char code[12];  // 资产代码（固定12字符长度）
	};

	// 创建复合数据类型，匹配HDF5文件结构
	hid_t type_id = H5Tcreate(H5T_COMPOUND, sizeof(CodeInfo));
	hid_t code_string_type = H5Tcopy(H5T_FORTRAN_S1);
	H5Tset_size(code_string_type, 12);
	H5Tinsert(type_id, "code", HOFFSET(CodeInfo, code), code_string_type);

	// 分配读取缓冲区
	size_t LENGTH = dims[0];
	std::vector<CodeInfo> code_info_all_tmp;
	code_info_all_tmp.resize(LENGTH);

	// 读取数据到缓冲区
	herr_t status = H5Dread(dataset_id, type_id, H5S_ALL, H5S_ALL, H5P_DEFAULT, &code_info_all_tmp[0]);
	(void)status;

	// 提取资产代码（取前6位）
	for (size_t i = 0; i < LENGTH; ++i) {
		std::string c = code_info_all_tmp[i].code;
		codes.push_back(c.substr(0, 9));
	}

	// 清理HDF5资源
	H5Tclose(type_id);
	H5Sclose(dataspace_id);
	H5Dclose(dataset_id);
	H5Fclose(file_id);
	return true;
}


bool get_related_codes_from_file(const std::string& csv_file_path, std::unordered_map<std::string, std::vector<std::string>>& related_codes_map) {
	try {
		std::ifstream in_file(csv_file_path, std::ios::in);
		std::string line;
		int line_count = 0;
		while (std::getline(in_file, line)) {
			++line_count;
			if (line_count == 1) continue; // 跳过header
			std::stringstream ss(line);
			std::string cell;
			std::vector<std::string> fields;
			while (std::getline(ss, cell, ',')) {
				fields.push_back(cell);
			}
			if (fields.size() < 11) continue; // 保障字段数量
			if (fields[1].size() <= 5 || std::atof(fields.back().c_str()) < -99) continue;
			std::string main_code = fields[0].substr(0, 6);
			std::vector<std::string>& rel_vec = related_codes_map[main_code];
			rel_vec.push_back(fields[1].substr(0, 6));
			rel_vec.push_back(fields[3].substr(0, 6));
			rel_vec.push_back(fields[5].substr(0, 6));
			rel_vec.push_back(fields[7].substr(0, 6));
			rel_vec.push_back(fields[9].substr(0, 6));
		}
		in_file.close();
		return true;
	} catch (const std::exception& e) {
		std::cerr << "Error: " << e.what() << std::endl;
		return false;
	}
}