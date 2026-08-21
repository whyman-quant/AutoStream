#ifndef __LOG_H__
#define __LOG_H__

// 本头依赖 strategy_interface.h（C++），仅在 C++ 工程中使用 sdp_handler 时包含即可。

#include <stdio.h>
#include "strategy_interface.h"

void check_log(const char *fmt, ...);
void check_log_ln(const char *fmt, ...);
int get_curr_time();
bool is_debug_mode();

// ---------- 原实现 ----------
// #define LOG(format,...) do{\
// 	get_curr_time() == 0 ? check_log("[SDP before_start] " format, ##__VA_ARGS__) : check_log("[SDP %d] " format, get_curr_time(), ##__VA_ARGS__);\
// }while (0)
//
// #define LOG_LN(format,...) do{\
// 	get_curr_time() == 0 ? check_log_ln("[SDP before_start] " format, ##__VA_ARGS__) : check_log_ln("[SDP %d] " format, get_curr_time(), ##__VA_ARGS__);\
// }while (0)
// ---------- 原实现 ----------
// HACK: (by gaowang) 改为 if/else + 缓存 get_curr_time()，减轻 -pedantic 下 ISO C99 对宏展开与可变参数的告警；false 分支不再重复调用 get_curr_time()。
#define LOG(format,...) do { \
		int _sdp_log_t = get_curr_time(); \
		if (_sdp_log_t == 0) \
			check_log("[SDP before_start] " format, ##__VA_ARGS__); \
		else \
			check_log("[SDP %d] " format, _sdp_log_t, ##__VA_ARGS__); \
	} while (0)

#define LOG_LN(format,...) do { \
		int _sdp_log_t = get_curr_time(); \
		if (_sdp_log_t == 0) \
			check_log_ln("[SDP before_start] " format, ##__VA_ARGS__); \
		else \
			check_log_ln("[SDP %d] " format, _sdp_log_t, ##__VA_ARGS__); \
	} while (0)
// 调用处：若只有一句固定正文、无 %d/%s 等额外实参，应写 LOG_LN("%s", "正文")；否则在 -pedantic 下整句字面串单独作首参时，经本宏展开到 printf 式的 check_log_ln 易触发 ISO C99 可变参数告警。

#ifndef _WIN32
	#define CC_RED "\033[31m"
	#define CC_GREEN "\033[32m"
	#define CC_BLUE "\033[34m"
	#define CC_YELLOW "\033[33m"
	#define CC_CYAN "\033[36m"
	#define CC_WHITE "\033[37m"
	#define CC_BLACK "\033[30m"
	#define CC_RESET "\033[0m"

	#define PRINT_DEBUG(format,...) do{\
			if(is_debug_mode()) printf("[SDP %s:%d] " format "\n", __FUNCTION__, __LINE__, ##__VA_ARGS__);\
		}while (0)

	#define PRINT_INFO(format,...) do{\
			if(is_debug_mode()) printf(CC_CYAN "[SDP %s:%d] " format CC_RESET "\n", __FUNCTION__, __LINE__, ##__VA_ARGS__);\
		}while (0)

	#define PRINT_WARN(format,...) do{\
			printf(CC_YELLOW "[SDP %s:%d] " format CC_RESET "\n", __FUNCTION__, __LINE__, ##__VA_ARGS__);\
		}while (0)

	#define PRINT_ERROR(format,...) do{\
			printf(CC_RED "[SDP %s:%d] " format CC_RESET "\n", __FUNCTION__, __LINE__, ##__VA_ARGS__);\
		}while (0)
#else
	#define PRINT_DEBUG(format,...) do{\
			printf("[SDP %s:%d] " format "\n", __FUNCTION__, __LINE__, ##__VA_ARGS__);\
		}while (0)

	#define PRINT_INFO(format,...) do{\
			printf("[SDP %s:%d] " format "\n", __FUNCTION__, __LINE__, ##__VA_ARGS__);\
		}while (0)

	#define PRINT_WARN(format,...) do{\
			printf("[SDP %s:%d] " format "\n", __FUNCTION__, __LINE__, ##__VA_ARGS__);\
		}while (0)

	#define PRINT_ERROR(format,...) do{\
			printf("[SDP %s:%d] " format "\n", __FUNCTION__, __LINE__, ##__VA_ARGS__);\
		}while (0)
#endif

#endif