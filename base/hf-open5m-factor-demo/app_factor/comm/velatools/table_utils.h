#pragma once

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace velatools {
namespace table_utils {

// 将二维字符串向量格式化为带边框的表格，返回可打印的字符串向量
// 第一行必须是列名，后续行是数据行
//
// 使用示例：
//   std::vector<std::vector<std::string>> table = {
//       {"Name", "Age", "City"},           // 列名
//       {"Alice", "25", "Beijing"},        // 数据行1
//       {"Bob", "30", "Shanghai"},         // 数据行2
//       {"Charlie", "35", "Guangzhou"}     // 数据行3
//   };
//   auto lines = GetPrintableTable(table);
//   for (const auto& line : lines) {
//       std::cout << line << std::endl;
//   }
//
// 打印输出示例：
//   ┌───────┬───┬─────────┐
//   │Name   │Age│City     │
//   ├───────┼───┼─────────┤
//   │Alice  │25 │Beijing  │
//   │Bob    │30 │Shanghai │
//   │Charlie│35 │Guangzhou│
//   └───────┴───┴─────────┘
inline std::vector<std::string> GetPrintableTable(const std::vector<std::vector<std::string>>& table) {
    std::vector<std::string> out;
    if (table.empty()) return out;

    // 第一行是列名，后续行是数据，对于每一列，需要先确定最大列宽，方便格式对齐
    // 每一列的最大列宽是该列列名和所有数据中的最大长度
    std::vector<size_t> max_width_list;
    max_width_list.resize(table[0].size(), 0);
    for (const auto& row : table) {
        for (size_t i = 0; i < row.size(); i++) {
            max_width_list[i] = std::max(max_width_list[i], row[i].size());
        }
    }

    out.reserve(table.size() + 3);

    // 构造表头的上边框
    std::string top_border = "┌";
    for (size_t i = 0; i < max_width_list.size(); ++i) {
        for (size_t j = 0; j < max_width_list[i]; ++j) {
            top_border += "─";
        }
        if (i != max_width_list.size() - 1)
            top_border += "┬";
        else
            top_border += "┐";
    }
    out.push_back(top_border);

    // 构造表头（列名）
    std::string header = "│";
    for (size_t i = 0; i < table[0].size(); ++i) {
        header += table[0][i];
        header += std::string(max_width_list[i] - table[0][i].size(), ' ');
        header += "│";
    }
    out.push_back(header);

    // 构造表头的下边框
    std::string header_border = "├";
    for (size_t i = 0; i < max_width_list.size(); ++i) {
        for (size_t j = 0; j < max_width_list[i]; ++j) {
            header_border += "─";
        }
        if (i != max_width_list.size() - 1)
            header_border += "┼";
        else
            header_border += "┤";
    }
    out.push_back(header_border);

    // 构造每一行数据
    for (size_t r = 1; r < table.size(); ++r) {
        std::string line = "│";
        for (size_t c = 0; c < table[r].size(); ++c) {
            line += table[r][c];
            line += std::string(max_width_list[c] - table[r][c].size(), ' ');
            line += "│";
        }
        out.push_back(line);
    }

    // 构造表格底边框
    std::string bottom_border = "└";
    for (size_t i = 0; i < max_width_list.size(); ++i) {
        for (size_t j = 0; j < max_width_list[i]; ++j) {
            bottom_border += "─";
        }
        if (i != max_width_list.size() - 1)
            bottom_border += "┴";
        else
            bottom_border += "┘";
    }
    out.push_back(bottom_border);

    return out;
}

// 将 string 矩阵截断为适合日志的长度：前 frozen_prefix_rows 行完整保留（列名、说明等），
// 仅对其后「数据行」在 count > head_data_rows + tail_data_rows 时保留前 head_data_rows 行与后 tail_data_rows 行，中间一行填「...」。
// frozen_prefix_rows 会按不超过 full_table.size() 进行裁剪；无数据行或无需截断时返回原表。
inline std::vector<std::vector<std::string>> TruncateTimeStatsTableForLog(
    const std::vector<std::vector<std::string>>& full_table, size_t frozen_prefix_rows, size_t head_data_rows,
    size_t tail_data_rows) {
    if (full_table.empty()) {
        return full_table;
    }
    const size_t h = std::min(frozen_prefix_rows, full_table.size());
    if (full_table.size() <= h) {
        return full_table;
    }
    if (full_table.size() - h <= head_data_rows + tail_data_rows) {
        return full_table;
    }
    const size_t ncol = full_table[0].size();
    std::vector<std::vector<std::string>> out;
    out.reserve(h + head_data_rows + 1 + tail_data_rows);
    for (size_t i = 0; i < h; ++i) {
        out.push_back(full_table[i]);
    }
    for (size_t i = 0; i < head_data_rows; ++i) {
        out.push_back(full_table[h + i]);
    }
    out.emplace_back(ncol, "...");
    const size_t tail_begin = full_table.size() - tail_data_rows;
    for (size_t i = tail_begin; i < full_table.size(); ++i) {
        out.push_back(full_table[i]);
    }
    return out;
}

inline void PrintTable(const std::vector<std::vector<std::string>>& table) {
    const auto& str_list = GetPrintableTable(table);
    for (const auto& str : str_list) {
        std::cout << str << std::endl;
    }
}

// 判断单元格内容是否为「可直接写入 CSV 数字列」的 ASCII 数字字面量（可选首尾空白；支持整数、小数、科学计数；不含千位逗号等）。
inline bool LooksLikeCsvNumericToken(const std::string& s) {
    // 去掉首尾空白，避免 to_string 等产生的前后空格影响判断。
    size_t l = 0;
    size_t r = s.size();
    while (l < r && std::isspace(static_cast<unsigned char>(s[l]))) {
        ++l;
    }
    while (r > l && std::isspace(static_cast<unsigned char>(s[r - 1]))) {
        --r;
    }
    if (l >= r) {
        return false;
    }
    size_t i = l;
    // 可选正负号。
    if (s[i] == '+' || s[i] == '-') {
        ++i;
    }
    if (i >= r) {
        return false;
    }
    bool has_digit = false;
    // 整数部分：至少一位十进制数字。
    while (i < r && std::isdigit(static_cast<unsigned char>(s[i]))) {
        has_digit = true;
        ++i;
    }
    // 可选小数点及小数部分（如 3.14、5.）。
    if (i < r && s[i] == '.') {
        ++i;
        while (i < r && std::isdigit(static_cast<unsigned char>(s[i]))) {
            has_digit = true;
            ++i;
        }
    }
    if (!has_digit) {
        return false;
    }
    // 可选指数部分（如 1e-3）。
    if (i < r && (s[i] == 'e' || s[i] == 'E')) {
        ++i;
        if (i < r && (s[i] == '+' || s[i] == '-')) {
            ++i;
        }
        bool exp_digit = false;
        while (i < r && std::isdigit(static_cast<unsigned char>(s[i]))) {
            exp_digit = true;
            ++i;
        }
        if (!exp_digit) {
            return false;
        }
    }
    // 除上述结构外不允许再有其它字符，否则视为非纯数字字面量（例如夹杂文字、千位逗号）。
    return i == r;
}

// CSV 单元格转义：含逗号、引号或换行时用双引号包裹，内部引号加倍。
// column_numeric_mode：该列在数据行（不含表头）已判定为「全为数字字面量」时，对已确认为数字的单元格可走简化写出路径。
inline std::string CsvEscapeCell(const std::string& s, bool column_numeric_mode) {
    // 数字列中的合法数字字面量：本身不含字段分隔符风险时直接写出，便于下游按数值类型打开 CSV。
    if (column_numeric_mode && LooksLikeCsvNumericToken(s)) {
        return s;
    }
    // RFC4180：字段内含逗号、双引号或换行时必须用双引号包裹，内部双引号加倍。
    if (s.find_first_of(",\"\r\n") != std::string::npos) {
        std::string out = "\"";
        for (char c : s) {
            if (c == '"') {
                out += "\"\"";
            } else {
                out += c;
            }
        }
        out += "\"";
        return out;
    }
    // 普通文本且无分隔符：无需引号。
    return s;
}

// 将二维字符串表写入 CSV（首行为列名）；用于全流程分段耗时等矩阵落盘。
inline bool SaveStringMatrixToCsv(const std::vector<std::vector<std::string>>& table, const std::string& file_path) {
    if (table.empty()) {
        return false;
    }
    const size_t ncol = table[0].size();
    // 按列扫描数据行（跳过表头）：若某列每个数据格均为数字字面量，则标记为数字列，写入时可采用数值语义路径。
    std::vector<bool> column_numeric_mode(ncol, true);
    for (size_t c = 0; c < ncol; ++c) {
        for (size_t r = 1; r < table.size(); ++r) {
            if (table[r].size() <= c) {
                column_numeric_mode[c] = false;
                break;
            }
            if (!LooksLikeCsvNumericToken(table[r][c])) {
                column_numeric_mode[c] = false;
                break;
            }
        }
    }
    std::ofstream ofs(file_path);
    if (!ofs.is_open()) {
        return false;
    }
    // 逐行写出：第 0 行为表头，始终不走数字列简化路径；数据行在数字列上使用 CsvEscapeCell(..., true)。
    for (size_t r = 0; r < table.size(); ++r) {
        for (size_t c = 0; c < table[r].size(); ++c) {
            if (c > 0) {
                ofs << ',';
            }
            const bool numeric_mode = (r > 0) && c < ncol && column_numeric_mode[c];
            ofs << CsvEscapeCell(table[r][c], numeric_mode);
        }
        ofs << '\n';
    }
    return true;
}

} // namespace table_utils
} // namespace velatools
