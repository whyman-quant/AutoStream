#include "factors/_share/per1day_data_getter.h"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <iostream>
#include <unordered_map>
#include <cstring>

#include "hdf5.h"
#include "factors/_comm/core.h"

namespace factors {
namespace share {

Per1DayDataGetter::Per1DayDataGetter(const std::string& filename)
    : default_data_() {
    data_ = ReadCSV(filename);
}

std::unordered_map<std::string, Per1DayData> Per1DayDataGetter::ReadCSV(const std::string& filename) {
    std::unordered_map<std::string, Per1DayData> data;
    std::ifstream file(filename);

    if (!file) {
        std::cerr << "Failed to open file: " << filename << std::endl;
        return data;
    }

    std::cout << "ReadCSV: " << filename << std::endl;

    std::string line;
    size_t line_num = 0;

    // 跳过标题行
    if (!std::getline(file, line)) {
        return data;  // 空文件
    }
    line_num++;

    while (std::getline(file, line)) {
        line_num++;
        if (line.empty()) continue;

        Per1DayData sd;
        std::stringstream ss(line);
        std::string token;

        // 分割整个行
        std::vector<std::string> fields;
        while (std::getline(ss, token, ',')) {
            fields.push_back(token);
        }

        // 检查字段数量
        if (fields.size() < 9) {
            std::cerr << "Line " << line_num << ": Only " << fields.size()
                      << " fields, expected 9. Skipping: " << line << std::endl;
            continue;
        }

        // 处理ticker
        sd.ticker = fields[0];
        sd.ticker = sd.ticker.substr(0, 6);

        // 安全的字段解析
        auto parse_field = [](const std::string& field) -> double {
            if (field.empty()) {
                return 0.0;
            }
            try {
                return std::stod(field);
            } catch (const std::exception& e) {
                std::cerr << "Conversion error for '" << field
                          << "': " << e.what() << std::endl;
                return 0.0;
            }
        };

        // 解析数值字段
        sd.high = parse_field(fields[1]);
        sd.low = parse_field(fields[2]);
        sd.open = parse_field(fields[3]);
        sd.close = parse_field(fields[4]);
        sd.vwap = parse_field(fields[5]);
        sd.vol = parse_field(fields[6]);
        sd.amount = parse_field(fields[7]);
        sd.ashare = parse_field(fields[8]);

        data[sd.ticker] = sd;
    }

    std::cout << "Loaded " << data.size() << " records from " << filename
              << std::endl;
    return data;
}

const std::unordered_map<std::string, Per1DayData>& Per1DayDataGetter::GetData()
    const {
    return data_;
}

const Per1DayData& Per1DayDataGetter::GetData(const std::string& ticker) const {
    auto it = data_.find(ticker);
    if (it != data_.end()) {
        return it->second;
    }
    std::cerr << "ticker not found: " << ticker << std::endl;
    return default_data_;
}

bool get_codes_from_file(const std::string &file_path, std::string dataset_name, std::vector<std::string> &codes) {
	// 打开HDF5文件（只读模式）
	hid_t file_id = H5Fopen(file_path.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT);
	if (file_id < 0) {
		fprintf(stderr, "无法打开文件: %s\n", file_path.c_str());
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
	std::vector<hsize_t> dims(static_cast<size_t>(ndims));
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
	(void)H5Dread(dataset_id, type_id, H5S_ALL, H5S_ALL, H5P_DEFAULT, &code_info_all_tmp[0]);

	// 提取资产代码（取前6位）
	for (size_t i = 0; i < LENGTH; i++) {
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

bool mxer_get_trading_date_range(std::string dir_mxwork, int begt, int endt, std::vector<int> &dates) {
	std::string filepath = dir_mxwork + "/baseinfo/CommInfo.h5";
	std::cout << "[mxer_get_trading_date_range] Opening file: " << filepath << std::endl;
	
	hid_t file = H5Fopen(filepath.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT);
	if (file < 0) {
		std::cerr << "[mxer_get_trading_date_range] Failed to open file: " << filepath << std::endl;
		return false;
	}
	hid_t dataset = H5Dopen2(file, "alldays", H5P_DEFAULT);
	if (dataset < 0) {
		std::cerr << "[mxer_get_trading_date_range] Failed to open dataset 'alldays'" << std::endl;
		H5Fclose(file);
		return false;
	}
	
	hid_t datatype = H5Dget_type(dataset);   /* datatype handle */
	hid_t dataspace = H5Dget_space(dataset); /* dataspace handle */
	int rank = H5Sget_simple_extent_ndims(dataspace);
	std::vector<hsize_t> dims_out(static_cast<size_t>(rank)); /* dataset dimensions */
	(void)H5Sget_simple_extent_dims(dataspace, dims_out.data(), NULL);
	int len = dims_out[0] * dims_out[1];
	char **data = new char *[len];
	(void)H5Dread(dataset, datatype, H5S_ALL, dataspace, H5P_DEFAULT, data);
	for (int i = 0; i < len; i++) {
		int dateint = atoi(data[i]);
		if (dateint >= begt && dateint <= endt) {
			dates.push_back(dateint);
		}
	}
	
	std::cout << "[mxer_get_trading_date_range] Found " << dates.size() << " dates in range [" << begt << ", " << endt << "]" << std::endl;
	
	H5Sclose(dataspace);
	H5Dclose(dataset);
	H5Fclose(file);
	return true;
}



dayinfo* mxer_get_dayinfo(std::string dir_mxwork, std::string date, int &codenum)
{
    dayinfo* AllInfo;

    typedef struct
    {
        char data[500];
    } swhy_t;
    swhy_t *swhyInfo;

    std::string h5file = dir_mxwork + "/basedata/" + date + "/per1day/swhy.h5";
	std::cout << "[mxer_get_dayinfo] Opening file: " << h5file << std::endl;

    hid_t file = H5Fopen(h5file.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT);
    hid_t dataset = H5Dopen2(file, "data", H5P_DEFAULT);
    hid_t dataspace = H5Dget_space(dataset);    /* dataspace handle */
    int rank = H5Sget_simple_extent_ndims(dataspace);
    hsize_t dims_out[1];           /* dataset dimensions */
    int status_n = H5Sget_simple_extent_dims(dataspace, dims_out, NULL);
    (void)status_n;
    hid_t datatype  = H5Tcreate(H5T_COMPOUND, sizeof(swhy_t));
    hid_t stringType1 = H5Tcopy(H5T_FORTRAN_S1);
    H5Tset_size(stringType1, 500);
    H5Tinsert(datatype, "data", HOFFSET(swhy_t, data), stringType1);
    hid_t memspace = H5Screate_simple(rank, dims_out, NULL);
    swhyInfo = new swhy_t[dims_out[0]];
    codenum = dims_out[0];
    (void)H5Dread(dataset, datatype, memspace, dataspace, H5P_DEFAULT, swhyInfo);
    AllInfo = new dayinfo[codenum];
    for (int c = 0; c < codenum; c++)
    {
        memset(AllInfo[c].swhy, 0, 12);
        std::copy(&swhyInfo[c].data[0], &swhyInfo[c].data[9], &AllInfo[c].swhy[0]);
        AllInfo[c].swhy[11] = '\0';
        // string datastr = swhyInfo[c].data;
        // cout << c << "," << datastr.substr(0, 12) << "," << AllInfo[c].swhy << endl;
    }
	
    H5Tclose(stringType1);
    H5Tclose(datatype);
    H5Dclose(dataset);
    H5Sclose(dataspace);
    H5Sclose(memspace);
    H5Fclose(file);

	std::cout << "[mxer_get_dayinfo] Closing file: " << h5file << std::endl;
    
    std::vector<std::string> codes_all;
	std::string h5codefile = dir_mxwork + "/basedata/" + date + "/per1day/codes_all.h5";
    get_codes_from_file(h5codefile, "data", codes_all);
    for(int c = 0; c < codenum; c++)
    {
        memset(AllInfo[c].code, 0, 12);
        std::copy(&codes_all[c][0], &codes_all[c][6], &AllInfo[c].code[0]);
        AllInfo[c].code[sizeof(AllInfo[c].code)- 1] = '\0';
    }
    std::vector<std::string> basefactorlist({"open", "high", "low", "close", "vol", "amount", "adj", "close_nr", "preclose_nr", "vwap", "share", "ashare", "st", "ipodates"});
    for (size_t fi = 0; fi < basefactorlist.size(); fi++)
    {
        std::string basefactor = basefactorlist[fi];
        std::string factor_h5file = dir_mxwork + "/basedata/" + date + "/per1day/" + basefactor + ".h5";

		std::cout << "[mxer_get_dayinfo] Opening file: " << factor_h5file << std::endl;

        hid_t factor_file = H5Fopen(factor_h5file.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT);
        hid_t factor_dataset = H5Dopen2(factor_file, "data", H5P_DEFAULT);
        hid_t factor_dataspace = H5Dget_space(factor_dataset);    /* dataspace handle */
        int factor_rank = H5Sget_simple_extent_ndims(factor_dataspace);
        hsize_t factor_dims_out[2];           /* dataset dimensions */
        (void)H5Sget_simple_extent_dims(factor_dataspace, factor_dims_out, NULL);
		hid_t factor_datatype = H5T_NATIVE_FLOAT;
        hid_t factor_memspace = H5Screate_simple(factor_rank, factor_dims_out, NULL);
        float *bsInfo = new float[factor_dims_out[0]];
        codenum = factor_dims_out[0];
        (void)H5Dread(factor_dataset, factor_datatype, factor_memspace, factor_dataspace, H5P_DEFAULT, bsInfo);
        if (basefactor == "open")
            for (int c = 0; c < codenum; c++) AllInfo[c].open = bsInfo[c];
        if (basefactor == "high")
            for (int c = 0; c < codenum; c++) AllInfo[c].high = bsInfo[c];
        if (basefactor == "low")
            for (int c = 0; c < codenum; c++) AllInfo[c].low = bsInfo[c];
        if (basefactor == "close")
            for (int c = 0; c < codenum; c++) AllInfo[c].close = bsInfo[c];
        if (basefactor == "vol")
            for (int c = 0; c < codenum; c++) AllInfo[c].vol = bsInfo[c];
        if (basefactor == "amount")
            for (int c = 0; c < codenum; c++) AllInfo[c].amount = bsInfo[c];
        if (basefactor == "adj")
            for (int c = 0; c < codenum; c++) AllInfo[c].adj = bsInfo[c];
        if (basefactor == "close_nr")
            for (int c = 0; c < codenum; c++) AllInfo[c].close_nr = bsInfo[c];
        if (basefactor == "preclose_nr")
            for (int c = 0; c < codenum; c++) AllInfo[c].preclose_nr = bsInfo[c];
        if (basefactor == "vwap")
            for (int c = 0; c < codenum; c++) AllInfo[c].vwap = bsInfo[c];
        if (basefactor == "share")
            for (int c = 0; c < codenum; c++) AllInfo[c].share = bsInfo[c];
        if (basefactor == "ashare")
            for (int c = 0; c < codenum; c++) AllInfo[c].ashare = bsInfo[c];

        H5Dclose(factor_dataset);
        H5Sclose(factor_dataspace);
        H5Sclose(factor_memspace);
        H5Fclose(factor_file);
    }
    return AllInfo;
}

}  // namespace share
}  // namespace factors