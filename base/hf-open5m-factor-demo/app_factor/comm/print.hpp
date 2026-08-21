#pragma once

#include <sys/time.h>

#include <iostream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <string>
#include <vector>

#ifdef ENABLE_APP_LIVE
#include "sdp_handler/utils/log.h"
#endif // ENABLE_APP_LIVE

// 获取当前时间字符串（格式HH:MM:SS）
// 用法: std::string t = now_time_str(); std::cout << t << std::endl;
inline std::string now_time_str() {
	std::time_t t = std::time(nullptr);
	std::tm tm = *std::localtime(&t);
	char buf[20];
	std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
	return std::string(buf);
}

// 获取当前时间字符串（格式HH:MM:SS.micros）
// 用法: std::string t = now_time_us_str(); std::cout << t << std::endl;
inline std::string now_time_us_str() {
	struct timeval tval;
	gettimeofday(&tval, nullptr);

	std::time_t seconds = static_cast<std::time_t>(tval.tv_sec);
	int micros = static_cast<int>(tval.tv_usec);
	std::tm local_time = *std::localtime(&seconds);

	char buf[40];
	std::snprintf(
		buf, sizeof(buf), "%02d:%02d:%02d.%06d",
		local_time.tm_hour, local_time.tm_min, local_time.tm_sec, micros
	);
	return std::string(buf);
}

// 通用类型的打印辅助：普通对象使用 ostringstream 打印
template <typename T>
struct PrintHelper {
	static void print(std::ostream& os, const T& v) { os << v; }
};

// 针对 std::vector 的特化，超长时只显示前5+...+后5元素
template <typename T>
struct PrintHelper<std::vector<T>> {
	static void print(std::ostream& os, const std::vector<T>& v) {
		os << '[';
		size_t n = v.size();
		if (n > 15) {
			// 只打印前5个
			for (size_t i = 0; i < 5; ++i) {
				if (i) os << ", ";
				PrintHelper<T>::print(os, v[i]);
			}
			os << ", ...";
			// 打印最后5个
			for (size_t i = n - 5; i < n; ++i) {
				os << ", ";
				PrintHelper<T>::print(os, v[i]);
			}
		} else {
			for (size_t i = 0; i < n; ++i) {
				if (i) os << ", ";
				PrintHelper<T>::print(os, v[i]);
			}
		}
		os << ']';
	}
};

// 完整版本的 PrintHelper：vector 不缩减输出
template <typename T>
struct PrintHelperFull {
	static void print(std::ostream& os, const T& v) { os << v; }
};
template <typename T>
struct PrintHelperFull<std::vector<T>> {
	static void print(std::ostream& os, const std::vector<T>& v) {
		os << '[';
		size_t n = v.size();
		for (size_t i = 0; i < n; ++i) {
			if (i) os << ", ";
			PrintHelperFull<T>::print(os, v[i]);
		}
		os << ']';
	}
};

// 普通万能打印转字符串入口
template <typename T>
inline std::string to_string_helper(const T& v) {
	std::ostringstream os;
	PrintHelper<T>::print(os, v);
	return os.str();
}

// 完整打印转字符串入口
template <typename T>
inline std::string to_string_helper_full(const T& v) {
	std::ostringstream os;
	PrintHelperFull<T>::print(os, v);
	return os.str();
}

// 可变参展开递归，空参数时返回空串
inline std::string TO_STRING() {
	return "";
}

// 多参数拼接为字符串，自动加空格。
// 用法: std::string s = TO_STRING("x=", 5, "arr:", std::vector<int>{1,2});
// 示例: std::cout << TO_STRING("Hello", 123, true) << std::endl; // 输出: "Hello 123 1"
template <typename T, typename... Rest>
std::string TO_STRING(const T& first, const Rest&... rest) {
	std::string head = to_string_helper(first);
	std::string tail = TO_STRING(rest...);
	return tail.empty() ? head : head + ' ' + tail;
}

// 完整版本展开递归，空参数时返回空串
inline std::string TO_STRING_FULL() {
	return "";
}

// 多参数拼接为字符串，不缩减 vector。和 TO_STRING 用法类似。
// 示例:
//   std::vector<int> v(100, 1);
//   std::cout << TO_STRING_FULL("Vector:", v) << std::endl; // 打印全部100个元素
template <typename T, typename... Rest>
std::string TO_STRING_FULL(const T& first, const Rest&... rest) {
	std::string head = to_string_helper_full(first);
	std::string tail = TO_STRING_FULL(rest...);
	return tail.empty() ? head : head + ' ' + tail;
}

// 屏幕打印当前时间（HH:MM:SS），无正文
// 用法: SCREEN_PRINT_WITH_TIME();
// 输出例子: 16:30:25 -
inline void SCREEN_PRINT_WITH_TIME() {
	std::cout << now_time_us_str() << " - " << std::endl;
}

// 屏幕打印当前时间+正文内容
// 用法: SCREEN_PRINT_WITH_TIME("Hello", 123, true);
// 输出例子: 16:30:25 - Hello 123 1
template<typename T, typename... Rest>
void SCREEN_PRINT_WITH_TIME(const T& first, const Rest&... rest) {
	std::string body = TO_STRING(first, rest...);
	std::ostringstream os;
	os << now_time_us_str() << " - " << body;
	std::cout << os.str() << std::endl;
}

// \033[ 是 ANSI 转义码的开始，后面跟一个数字表示颜色，用于将后续文本设置为对应颜色
// 常见颜色的 ANSI 转义码：
// \033[0m   —— 重置颜色（恢复默认），避免影响后续输出
// \033[30m  —— 黑色 black   \033[31m  —— 红色 red    \033[32m  —— 绿色 green    \033[33m  —— 黄色 yellow
// \033[34m  —— 蓝色 blue   \033[35m  —— 紫色 purple   \033[36m  —— 青色 cyan   \033[37m  —— 白色 white

// 内部辅助函数：将颜色名称转换为 ANSI 转义码
// 支持的颜色：black, red, green, yellow, blue, purple, cyan, white
// 空字符串或 "empty" 表示不设置颜色，返回空字符串
// 不支持的颜色也会返回空字符串（不设置颜色）
// 参数使用 const char* 以提高效率，避免不必要的字符串构造
inline const char* GetColorCode(const char* color) {
	if (!color || color[0] == '\0') return ""; // 空字符串，不设置颜色

	// 检查是否为 "empty"
	if (color[0] == 'e' && color[1] == 'm' && color[2] == 'p' && color[3] == 't' && color[4] == 'y' && color[5] == '\0')
		return ""; // empty，不设置颜色

	// 使用简单的字符串比较，按长度和首字符快速判断
	// 按长度分组以提高效率
	switch (color[0]) {
		case 'b':
			if (color[1] == 'l' && color[2] == 'a' && color[3] == 'c' && color[4] == 'k' && color[5] == '\0')
				return "\033[30m"; // black
			if (color[1] == 'l' && color[2] == 'u' && color[3] == 'e' && color[4] == '\0')
				return "\033[34m"; // blue
			break;
		case 'r':
			if (color[1] == 'e' && color[2] == 'd' && color[3] == '\0')
				return "\033[31m"; // red
			break;
		case 'g':
			if (color[1] == 'r' && color[2] == 'e' && color[3] == 'e' && color[4] == 'n' && color[5] == '\0')
				return "\033[32m"; // green
			break;
		case 'y':
			if (color[1] == 'e' && color[2] == 'l' && color[3] == 'l' && color[4] == 'o' && color[5] == 'w' && color[6] == '\0')
				return "\033[33m"; // yellow
			break;
		case 'p':
			if (color[1] == 'u' && color[2] == 'r' && color[3] == 'p' && color[4] == 'l' && color[5] == 'e' && color[6] == '\0')
				return "\033[35m"; // purple
			break;
		case 'c':
			if (color[1] == 'y' && color[2] == 'a' && color[3] == 'n' && color[4] == '\0')
				return "\033[36m"; // cyan
			break;
		case 'w':
			if (color[1] == 'h' && color[2] == 'i' && color[3] == 't' && color[4] == 'e' && color[5] == '\0')
				return "\033[37m"; // white
			break;
	}
	// 不支持的颜色，不设置颜色
	return "";
}

// 写日志到屏幕/文件，屏幕输出自动带当前时间（HH:MM:SS - ...）
// content: 主体内容
// screen: 是否屏幕同步打印
// error: 是否走 std::cerr
// color: 颜色名称，支持 black, red, green, yellow, blue, purple, cyan, white，空字符串或 "empty" 表示不设置颜色，不支持的颜色也不设置颜色
// 用法:
//   WLOG("日志内容", true);                    // 使用默认（不设置颜色）
//   WLOG("重要日志", true, false, "red");      // 红色日志
//   WLOG("错误日志", true, true, "yellow");    // 黄色错误日志
inline void WLOG(const std::string& content, bool screen=true, bool error=false, const std::string& color="") {
	std::string log_content = now_time_us_str() + " - " + content;
	const char* color_code = GetColorCode(color.c_str());
	const char* reset_code = (color_code[0] != '\0') ? "\033[0m" : ""; // 如果有颜色码，才需要重置码
#ifdef ENABLE_APP_LIVE
	LOG_LN("%s", log_content.c_str());
	if (screen) {
		// 使用ostringstream保证单条输出，避免多线程打断
		// 将颜色转义码和内容合并，一次性输出
		std::ostringstream os;
		os << color_code << log_content << reset_code;
		if (!error) {
			// 直接添加'\n'，避免用std::endl强制flush，提高多线程日志输出的完整性与性能。
			// 用ostringstream拼接整行日志再输出，可减少多线程下日志被拆分的风险。
			os << '\n';
			std::cout << os.str();
		} else {
			// 平台会将cerr的内容导入到文件，所以这里需要用cout才能在屏幕上显示
			os << '\n';
			std::cout << os.str();
			// 平台不会将cerr的内容打印在屏幕上，所以不必设置颜色
			std::cerr << log_content << std::endl;
		}
	}
#else
	if (screen) {
		// 使用ostringstream保证单条输出，避免多线程打断
		// 将颜色转义码和内容合并，一次性输出
		std::ostringstream os;
		os << color_code << log_content << reset_code;
		if (!error) {
			os << '\n';
			std::cout << os.str();
		} else {
			os << '\n';
			std::cerr << os.str();
		}
	}
#endif // ENABLE_APP_LIVE
}

// 传入多行（vector<string>）日志，屏幕输出每行也带时间。
// content: 多行日志内容
// screen: 是否屏幕同步打印
// error: 是否走 std::cerr
// color: 颜色名称，支持 black, red, green, yellow, blue, purple, cyan, white，空字符串或 "empty" 表示不设置颜色，不支持的颜色也不设置颜色
// 用法:
//   WLOG(std::vector<std::string>{"line1", "line2"}, true);                    // 默认（不设置颜色）
//   WLOG(std::vector<std::string>{"line1", "line2"}, true, false, "green");    // 绿色日志
inline void WLOG(const std::vector<std::string>& content, bool screen=true, bool error=false, const std::string& color="") {
	std::string time_str = now_time_us_str();
	const char* color_code = GetColorCode(color.c_str());
	const char* reset_code = (color_code[0] != '\0') ? "\033[0m" : ""; // 如果有颜色码，才需要重置码
	for (const auto& line : content) {
		std::string log_content = time_str + " - " + line;
#ifdef ENABLE_APP_LIVE
		LOG_LN("%s", log_content.c_str());
		if (screen) {
			// 使用ostringstream保证单条输出，避免多线程打断
			// 将颜色转义码和内容合并，一次性输出
			std::ostringstream os;
			os << color_code << log_content << reset_code;
			if (!error) {
				os << '\n';
				std::cout << os.str();
			} else {
				// 平台会将cerr的内容导入到文件，所以这里需要用cout才能在屏幕上显示
				os << '\n';
				std::cout << os.str();
				// 平台不会将cerr的内容打印在屏幕上，所以不必设置颜色
				std::cerr << log_content << std::endl;
			}
		}
#else
		if (screen) {
			// 使用ostringstream保证单条输出，避免多线程打断
			// 将颜色转义码和内容合并，一次性输出
			std::ostringstream os;
			os << color_code << log_content << reset_code;
			if (!error) {
				os << '\n';
				std::cout << os.str();
			} else {
				os << '\n';
				std::cerr << os.str();
			}
		}
#endif // ENABLE_APP_LIVE
	}
}

// 写日志到屏幕/文件，屏幕输出自动带当前时间（HH:MM:SS - ...）
// content: 主体内容
// screen: 是否屏幕同步打印
// color: 颜色名称，支持 black, red, green, yellow, blue, purple, cyan, white，空字符串或 "empty" 表示不设置颜色，不支持的颜色也不设置颜色
// 用法:
//   WERR("错误日志", true);              // 使用默认颜色 yellow
//   WERR("严重错误", true, "red");       // 红色错误日志
inline void WERR(const std::string& content, bool screen=true, const std::string& color="yellow") {
	std::string log_content = now_time_us_str() + " - " + content;
	const char* color_code = GetColorCode(color.c_str());
	const char* reset_code = (color_code[0] != '\0') ? "\033[0m" : ""; // 如果有颜色码，才需要重置码
#ifdef ENABLE_APP_LIVE
	LOG_LN("%s", log_content.c_str());
	if (screen) {
		// 使用ostringstream保证单条输出，避免多线程打断
		// 将颜色转义码和内容合并，一次性输出
		std::ostringstream os;
		os << color_code << log_content << reset_code;
		// 平台会将cerr的内容导入到文件，所以这里需要用cout才能在屏幕上显示
		os << '\n';
		std::cout << os.str();
		// 平台不会将cerr的内容打印在屏幕上，所以不必设置颜色
		std::cerr << log_content << std::endl;
	}
#else
	if (screen) {
		// 使用ostringstream保证单条输出，避免多线程打断
		// 将颜色转义码和内容合并，一次性输出
		std::ostringstream os;
		os << color_code << log_content << reset_code;
		os << '\n';
		std::cerr << os.str();
	}
#endif // ENABLE_APP_LIVE
}

// 传入多行（vector<string>）日志，屏幕输出每行也带时间。
// content: 多行日志内容
// screen: 是否屏幕同步打印
// color: 颜色名称，支持 black, red, green, yellow, blue, purple, cyan, white，空字符串或 "empty" 表示不设置颜色，不支持的颜色也不设置颜色
// 用法:
//   WERR(std::vector<std::string>{"line1", "line2"}, true);              // 默认颜色 yellow
//   WERR(std::vector<std::string>{"line1", "line2"}, true, "red");      // 红色错误日志
inline void WERR(const std::vector<std::string>& content, bool screen=true, const std::string& color="yellow") {
	std::string time_str = now_time_us_str();
	const char* color_code = GetColorCode(color.c_str());
	const char* reset_code = (color_code[0] != '\0') ? "\033[0m" : ""; // 如果有颜色码，才需要重置码
	for (const auto& line : content) {
		std::string log_content = time_str + " - " + line;
#ifdef ENABLE_APP_LIVE
		LOG_LN("%s", log_content.c_str());
		if (screen) {
			// 使用ostringstream保证单条输出，避免多线程打断
			// 将颜色转义码和内容合并，一次性输出
			std::ostringstream os;
			os << color_code << log_content << reset_code;
			// 平台会将cerr的内容导入到文件，所以这里需要用cout才能在屏幕上显示
			os << '\n';
			std::cout << os.str();
			// 平台不会将cerr的内容打印在屏幕上，所以不必设置颜色
			std::cerr << log_content << std::endl;
		}
#else
		if (screen) {
			// 使用ostringstream保证单条输出，避免多线程打断
			// 将颜色转义码和内容合并，一次性输出
			std::ostringstream os;
			os << color_code << log_content << reset_code;
			os << '\n';
			std::cerr << os.str();
		}
#endif // ENABLE_APP_LIVE
	}
}
