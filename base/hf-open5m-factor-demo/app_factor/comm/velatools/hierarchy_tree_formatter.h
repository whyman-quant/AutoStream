#pragma once

#include <string>
#include <utility>
#include <vector>

namespace velatools {
namespace hierarchy_tree_formatter {

// 将“层级 + 名称”结构格式化为树形文本。
// 该函数可用于表示线程调用层次、文件目录层次或任意层级关系。
inline std::vector<std::string> FormatHierarchyTreeLines(
	const std::vector<std::pair<int, std::string>>& level_name_lines) {
	std::vector<std::string> formatted_lines;
	formatted_lines.reserve(level_name_lines.size());

	std::vector<bool> ancestor_is_last;
	ancestor_is_last.reserve(level_name_lines.size());

	for (size_t line_idx = 0; line_idx < level_name_lines.size(); ++line_idx) {
		int level = level_name_lines[line_idx].first;
		const std::string& name = level_name_lines[line_idx].second;
		if (level < 0) {
			continue;
		}

		if (level >= 1 && ancestor_is_last.size() >= static_cast<size_t>(level)) {
			ancestor_is_last.resize(static_cast<size_t>(level - 1));
		}

		bool is_last_of_level = true;
		for (size_t j = line_idx + 1; j < level_name_lines.size(); ++j) {
			int next_level = level_name_lines[j].first;
			if (next_level < level) {
				break;
			}
			if (next_level == level) {
				is_last_of_level = false;
				break;
			}
		}

		std::string prefix;
		if (level > 0) {
			prefix.reserve(static_cast<size_t>(level) * 4);
			for (size_t d = 1; d < static_cast<size_t>(level); ++d) {
				prefix += ancestor_is_last[d - 1] ? "    " : "|   ";
			}
			prefix += is_last_of_level ? "`-- " : "|-- ";
		}

		formatted_lines.push_back(prefix + name);
		ancestor_is_last.push_back(is_last_of_level);
	}

	return formatted_lines;
}

} // namespace hierarchy_tree_formatter
} // namespace velatools
