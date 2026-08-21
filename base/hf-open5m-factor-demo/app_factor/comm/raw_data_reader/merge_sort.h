#pragma once

// 本头及 merge_sort.cc 以 ISO C++11 为基线：不使用 C++14+ 语言特性；常量/函数保持 constexpr +（显式）inline 即可。

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

namespace merge_sort {

// 最多支持的并行行情路数（败者树规模上限）。勿在 TU 外以宏覆盖；超界由 load_quote_c 断言拦截。
constexpr std::size_t kMaxChannelCnt = (static_cast<std::size_t>(4096u) << 2);
static_assert(kMaxChannelCnt >= 64 && kMaxChannelCnt <= 262144,
	      "merge_sort::kMaxChannelCnt 须在合理败者树规模内");

// pop_tick 无更多元素时的返回值（原 REACH_END 宏）。
constexpr std::uint64_t kReachEnd = UINT64_MAX;

// 原 encode_tick / decode_tick_* 宏；头文件内联定义，无 ODR 问题。
inline constexpr std::uint64_t EncodeTick(unsigned a, unsigned b) {
	return (static_cast<std::uint64_t>(a) << 12) + b;
}
inline constexpr std::uint32_t DecodeTickRow(std::uint64_t v) {
	return static_cast<std::uint32_t>((UINT64_C(0xFFFFF000) & v) >> 12);
}
inline constexpr std::uint32_t DecodeTickCol(std::uint64_t v) {
	return static_cast<std::uint32_t>(0xFFF & v);
}

// 描述一路已加载内存：类型、条数、每条字节数、首地址。
typedef struct quote_head {
	int mi_type;
	unsigned int size;
	unsigned int itemsize;
	void *data;
} quote_head_t;

// 各路行情公共时间头：归并键依赖 local_time / exch_time（实现里 +1 避免 0 比较问题）。
typedef struct common_quote {
	int serial;
	int mi_type;
	std::uint64_t local_time;
	std::uint64_t exch_time;
} common_quote_t;

// load_quote_c(void**,...) 时每路传入的轻量描述；data 指向当前读指针，itemsize 为步进字节数。
typedef struct channel_data {
	void *data;
	int itemsize;
	int size;
} channel_data_t;

// pop_tick 输出：第几路、第几条、以及该点时间与类型。
typedef struct point_cr {
	std::uint32_t col;
	std::uint32_t row;
	std::uint32_t mi_type;
	std::uint64_t exch_time;
	std::uint64_t local_time;
} point_t;

enum SORT_METHOD {
	BY_EXCHG_TIME = 0,
	BY_LOCAL_TIME = 1,
};

// mi_type == 357（与期货因子 demo 中 raw_quote/merge_sort 约定一致）：
// 在 load_quote_c 建败者树之前，对该路 channel 在「交易日 9:30–9:31」按 local_time（微秒时间戳）做原地重排，
// 便于首分钟窗口内乱序到达的数据在参与多路归并前局部有序。
// 原地排序按固定步长 88 字节（与旧 sort.h 中 data_t 一致：前 24 字节同 common_quote_t + 64 字节载荷，见 merge_sort.cc）
// 进行；若该路 itemsize 与此不一致则跳过排序，避免将错误步长当作记录边界。
void sort_one_min_date(channel_data_t *data);

// 历史占位：当前实现未把 data 写入 all_data，仅 init_loser_tree；勿单独依赖。
void load_quote(void **data, int sort_method);

// data[i] 指向 channel_data_t，共 size 路；从每路首条建败者树。size 不得超过 kMaxChannelCnt。
void load_quote_c(void **data, int size, int sort_method);

// 将 vector<quote_head_t> 转成内部 channel_data_t 再调上一重载。
void load_quote_c(std::vector<quote_head_t> &sort_quote, int sort_method);

// 弹出当前全局最小（按 sort_method）的一条；返回 kReachEnd 表示结束。
std::uint64_t pop_tick(point_t *p);

// 演示用：整路 local_time 平移 num 微秒（或同等时间单位，与数据结构一致即可）。
template<typename T>
void inc_dec_local_time(T &quote, int num) {
	for (int i = 0; i < static_cast<int>(quote.size()); ++i) {
		quote[i].local_time += num;
	}
}

} // namespace merge_sort
