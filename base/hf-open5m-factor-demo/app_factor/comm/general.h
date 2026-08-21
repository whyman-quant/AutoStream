#ifndef GENERAL_H
#define GENERAL_H

#include <stdio.h>

#include <algorithm>
#include <cstring>
#include <ctime>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

extern std::string AllDataDir;

bool get_trading_date_range(int begt, int endt, std::vector<int> &dates);

bool get_codes(const std::string &date, std::vector<std::string> &codes_all,
				std::vector<std::string> &codes_nokcb);

bool get_codes_kzz(const std::string &date, std::vector<std::string> &codes_kzz);

bool get_codes_from_file(const std::string &file_path, std::string dataset_name, std::vector<std::string> &codes);

bool get_related_codes_from_file(const std::string& csv_file_path, std::unordered_map<std::string, std::vector<std::string>>& related_codes_map);

#endif