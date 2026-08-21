// 多路已按时间局部有序的行情流，用败者树做 k 路归并；与 TicksData 输出的内存块配合使用。
#include <stdio.h>
#include <limits.h>
#include <time.h>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "raw_data_types/my_stock.h"
#include "raw_data_types/my_stock_order.h"
#include "raw_data_types/my_stock_transaction.h"
#include "merge_sort.h"

namespace merge_sort {
namespace {

// ---------------------------------------------------------------------------
// mi_type 357：首分钟（9:30–9:31）局部按 local_time 排序
// 来源：期货因子 demo 中 comm/raw_quote/merge_sort.cpp + sort.h；在 load_quote_c 遍历各路时，
// 若首条 common_quote_t.mi_type == 357，则先对本路缓冲区内该分钟对应的下标区间做原地排序，再进入败者树。
// local_time 单位与旧代码一致：ONE_SEC = 1_000_000，即按「整秒」截取日历日并与 9:30/9:31 边界比较。
// ---------------------------------------------------------------------------

constexpr int kMiType357PreMerge = 357;
constexpr int kLocalTimeMicrosPerSec = 1000000;
constexpr int kMinMergeRun = 32;

// 与旧 sort.h 中 data_t 布局一致：前部与 common_quote_t 对齐，后附 64 字节载荷；原地排序按此步长移动整记录。
struct merge_sort_357_record_t {
	int serial;
	int mi_type;
	uint64_t local_time;
	uint64_t exch_time;
	uint8_t data[64];
};

static_assert(sizeof(merge_sort_357_record_t) == 88,
	"357 首分钟排序记录布局须与旧 sort.h data_t 一致（88 字节）");

static void Reverse357(merge_sort_357_record_t *arr, int lo, int hi) {
	while (lo < hi) {
		merge_sort_357_record_t tmp = arr[lo];
		arr[lo] = arr[hi];
		arr[hi] = tmp;
		++lo;
		--hi;
	}
}

static int SequentialSortedCnt357(merge_sort_357_record_t *arr, int lo, int hi) {
	if (lo == hi)
		return 1;
	int idx = 0;
	for (int i = lo + 1; i <= hi; ++i) {
		if (arr[i].local_time > arr[i - 1].local_time)
			break;
		idx = i;
	}
	if (idx > 0) {
		Reverse357(arr, lo, idx);
		return idx - lo + 1;
	}
	idx = 0;
	for (int i = idx + 1; i <= hi; ++i) {
		if (arr[i].local_time < arr[i - 1].local_time)
			break;
		idx = i;
	}
	return idx - lo + 1;
}

static void InsertSort357(merge_sort_357_record_t *arr, int lo, int start, int hi) {
	if (start == lo)
		++start;
	for (; start <= hi; ++start) {
		merge_sort_357_record_t key = arr[start];
		int j = start - 1;
		while (j >= lo && arr[j].local_time > key.local_time) {
			arr[j + 1] = arr[j];
			--j;
		}
		arr[j + 1] = key;
	}
}

static void MergeSortPass357(merge_sort_357_record_t *arr, int n) {
	merge_sort_357_record_t *buffer = static_cast<merge_sort_357_record_t *>(
	    std::malloc(sizeof(merge_sort_357_record_t) * static_cast<size_t>(n)));
	assert(buffer);

	int gap = 1;
	while (gap < n) {
		for (int i = 0; i < n; i += gap * 2) {
			int j = i;
			int left_start = i;
			int left_end = i + gap - 1;
			int right_start = i + gap;
			int right_end = i + gap * 2 - 1;

			if (right_start >= n)
				break;
			if (right_end >= n)
				right_end = n - 1;

			while (left_start <= left_end && right_start <= right_end) {
				if (arr[left_start].local_time <= arr[right_start].local_time)
					buffer[j++] = arr[left_start++];
				else
					buffer[j++] = arr[right_start++];
			}
			while (left_start <= left_end)
				buffer[j++] = arr[left_start++];
			while (right_start <= right_end)
				buffer[j++] = arr[right_start++];
			std::memcpy(arr + i, buffer + i,
				    sizeof(merge_sort_357_record_t) * static_cast<size_t>(j - i));
		}
		gap *= 2;
	}
	std::free(buffer);
}

static void Sort357ByLocalTime(merge_sort_357_record_t *base, int lo, int hi) {
	const int remaining = hi - lo + 1;
	if (remaining < 2)
		return;
	if (remaining < kMinMergeRun) {
		const int cnt = SequentialSortedCnt357(base, lo, hi);
		InsertSort357(base, lo, lo + cnt, hi);
		return;
	}
	MergeSortPass357(base, remaining);
}

// 由首条 local_time 推出「当日 0 点」与 9:30–9:31 的 unix 秒区间（与旧实现一致）。
static int GetOpenCallTimeRangeSec(const channel_data_t *ch, uint64_t *start_sec_ptr, uint64_t *end_sec_ptr) {
	const uint64_t local_time = reinterpret_cast<const common_quote_t *>(ch->data)->local_time;
	const time_t local_time_sec = static_cast<time_t>(local_time / kLocalTimeMicrosPerSec);
	struct tm tm_time;
	if (localtime_r(&local_time_sec, &tm_time) == nullptr)
		return -1;
	tm_time.tm_hour = 0;
	tm_time.tm_min = 0;
	tm_time.tm_sec = 0;
	const time_t cur_date_0 = mktime(&tm_time);
	if (cur_date_0 == static_cast<time_t>(-1))
		return -1;

	const int start_sec = 9 * 3600 + 30 * 60;
	const int end_sec = start_sec + 60;
	*start_sec_ptr = static_cast<uint64_t>(cur_date_0 + start_sec);
	*end_sec_ptr = static_cast<uint64_t>(cur_date_0 + end_sec);
	return 0;
}

static void FindIndicesByLocalTimeSec(const channel_data_t *ch, uint64_t start_sec, uint64_t end_sec, int *start_idx,
				       int *end_idx) {
	char *row = static_cast<char *>(ch->data);
	int found_start = 0;
	int found_end = 0;
	for (int i = 0; i < ch->size; ++i) {
		const uint64_t time_sec =
		    reinterpret_cast<common_quote_t *>(row)->local_time / kLocalTimeMicrosPerSec;
		if (time_sec >= start_sec && !found_start) {
			*start_idx = i;
			found_start = 1;
		}
		if (time_sec > end_sec && !found_end) {
			*end_idx = i - 1;
			found_end = 1;
		}
		if (found_start && found_end)
			break;
		row += ch->itemsize;
	}
}

} // namespace

void sort_one_min_date(channel_data_t *data) {
	if (!data || data->size <= 0 || data->data == nullptr)
		return;
	if (data->itemsize != sizeof(merge_sort_357_record_t)) {
		// 与旧代码隐式假设一致：按 merge_sort_357_record_t 步长整体交换；itemsize 不匹配时不排序以免越界/错位。
		return;
	}

	uint64_t start_sec = 0;
	uint64_t end_sec = 0;
	if (GetOpenCallTimeRangeSec(data, &start_sec, &end_sec) != 0)
		return;

	int start_idx = -1;
	int end_idx = -1;
	FindIndicesByLocalTimeSec(data, start_sec, end_sec, &start_idx, &end_idx);
	if (start_idx < 0 || end_idx < 0)
		return;

	auto *base = static_cast<merge_sort_357_record_t *>(data->data);
	Sort357ByLocalTime(base + start_idx, 0, end_idx - start_idx);
}

// 败者树节点上缓存的比较键：两路时间 + 来源列行。
typedef struct quote_tick {
	uint64_t exch_time;
	uint64_t local_time;
	uint32_t column;
	uint32_t row;
} quote_tick_t;

typedef size_t loser_index_t;

typedef struct quote_data {
	int size;
	int min_type;
	int sort_method;
	// 上一次 pop 的冠军位置/时间，供 pop_tick 输出给调用方。
	uint32_t p_tick_col;
	uint32_t p_tick_row;
	uint64_t p_exch_time;
	uint64_t p_local_time;
	// q: 每一路“当前候选记录”的比较键；l: 败者树内部节点。
	// 约定 q[size] 作为哨兵槽位，初始化时所有 l[i] 先指向该槽位。
	quote_tick_t q[kMaxChannelCnt + 1];
	loser_index_t l[kMaxChannelCnt + 1];
	channel_data_t d[kMaxChannelCnt + 1];
} quote_data_t;

static quote_data_t all_data;
static quote_tick_t fill_tick;
static void *_data[kMaxChannelCnt] = {0};
static int quote_mi_type[kMaxChannelCnt] = {0};
static channel_data_t _buf[kMaxChannelCnt] = {0};

#if 0
static void debug_status() {
	int i;
	printf("tick start |q->");
	for (i = 0; i < all_data.size; i++) {
		printf("%d - %lu|", i, all_data.q[i].local_time);
	}
	printf("\n");
	printf("tick start |l->");
	for (i = 0; i < all_data.size; i++) {
		printf("%d - %lu|", i, all_data.l[i]);
	}
	printf("\n");
}
#endif

void swap(int &x, loser_index_t &y) {
	loser_index_t tmp = x;
	x = y;
	y = tmp;
}

// 同时间点、同交易所时间时用于稳定/打破对称的交换判定。
bool is_swap_idx_in_same_value(int idx1, int idx2) {
	if (idx1 <= idx2 ||
	    all_data.q[idx1].local_time != all_data.q[idx2].local_time ||
	    all_data.q[idx1].exch_time != all_data.q[idx2].exch_time) {
		return false;
	}
	return true;
}

// 以 exch_time 为主、local_time 为辅，沿败者树祖先调整。
void adjust_by_exch_time(int s) {
	int i;
	for (i = (s + all_data.size) / 2; i > 0; i /= 2) {
		if (all_data.q[s].exch_time > all_data.q[all_data.l[i]].exch_time ||
		    (all_data.q[s].exch_time == all_data.q[all_data.l[i]].exch_time &&
		     all_data.q[s].local_time > all_data.q[all_data.l[i]].local_time)) {
			swap(s, all_data.l[i]);
		} else if (is_swap_idx_in_same_value(s, all_data.l[i])) {
			swap(s, all_data.l[i]);
		}
	}
	all_data.l[0] = s;
}

// 以 local_time 为主、exch_time 为辅。
void adjust_by_local_time(int s) {
	int i;
	for (i = (s + all_data.size) / 2; i > 0; i /= 2) {
		if (all_data.q[s].local_time > all_data.q[all_data.l[i]].local_time ||
		    (all_data.q[s].local_time == all_data.q[all_data.l[i]].local_time &&
		     all_data.q[s].exch_time > all_data.q[all_data.l[i]].exch_time)) {
			swap(s, all_data.l[i]);
		} else if (is_swap_idx_in_same_value(s, all_data.l[i])) {
			swap(s, all_data.l[i]);
		}
	}
	all_data.l[0] = s;
}

// 从 channel 当前 data 指针读出一条 common_quote，写入 dquote，并把通道 data 指针前移 itemsize。
void convert_to_quote_tick(channel_data_t *c_quote, quote_tick_t *dquote, int column, int row) {
	common_quote_t *cq = (common_quote_t *)c_quote->data;
	dquote->column = column;
	dquote->row = row;
	// 保持历史实现语义：时间键 +1，避免“全 0 哨兵值”与有效首条记录发生碰撞。
	// pop_tick 返回给上层的时间也沿用这套语义（与旧 demo 行为一致）。
	dquote->local_time = cq->local_time + 1;
	dquote->exch_time = cq->exch_time + 1;
	c_quote->data = static_cast<void *>(static_cast<char *>(c_quote->data) + c_quote->itemsize);
}

// 初始化败者树：哨兵、全索引指向 size 节点，再从叶到根 adjust。
static void init_loser_tree() {
	int i;
	all_data.p_tick_row = 0;
	all_data.p_tick_col = 0;
	all_data.p_local_time = 0;
	all_data.p_exch_time = 0;
	fill_tick.exch_time = ULONG_MAX;
	fill_tick.local_time = ULONG_MAX;
	fill_tick.column = UINT_MAX;
	fill_tick.row = UINT_MAX;
	// q[size] 是“最小初值哨兵”：用于冷启动阶段参与比较与填充 l[]。
	all_data.q[all_data.size].exch_time = 0;
	all_data.q[all_data.size].local_time = 0;
	all_data.q[all_data.size].column = UINT_MAX;
	all_data.q[all_data.size].row = UINT_MAX;
	for (i = 0; i < all_data.size + 1; i++)
		all_data.l[i] = all_data.size;
	if (all_data.sort_method != BY_LOCAL_TIME) {
		for (i = all_data.size - 1; i >= 0; i--) {
			adjust_by_exch_time(i);
		}
	} else {
		for (i = all_data.size - 1; i >= 0; i--) {
			adjust_by_local_time(i);
		}
	}
}

// 弹出冠军所在列后，用该列下一条（或哨兵）替换并沿树调整。
static void update_loser_tree() {
	int row = all_data.p_tick_row;
	int col = all_data.p_tick_col;

	if (row < all_data.d[col].size - 1) {
		convert_to_quote_tick(&all_data.d[col], &all_data.q[col], col, row + 1);
	} else {
		// 该通道耗尽后放入 fill_tick(ULONG_MAX)，确保后续不会再成为冠军。
		all_data.q[col] = fill_tick;
	}
	if (all_data.sort_method != BY_LOCAL_TIME) {
		adjust_by_exch_time(col);
	} else {
		adjust_by_local_time(col);
	}
}

uint64_t pop_tick(point_t *p) {
	// 败者树根 l[0] 始终指向当前最小键（冠军）所在通道。
	all_data.p_tick_col = all_data.q[all_data.l[0]].column;
	all_data.p_tick_row = all_data.q[all_data.l[0]].row;
	all_data.p_exch_time = all_data.q[all_data.l[0]].exch_time;
	all_data.p_local_time = all_data.q[all_data.l[0]].local_time;

	update_loser_tree();

	if (all_data.p_tick_col == UINT_MAX) {
		return kReachEnd;
	}

	p->col = all_data.p_tick_col;
	p->row = all_data.p_tick_row;
	p->exch_time = all_data.p_exch_time;
	p->local_time = all_data.p_local_time;
	p->mi_type = quote_mi_type[p->col];
	return 0;
}

void load_quote(void **data, int sort_method) {
	(void)data;
	(void)sort_method;
	init_loser_tree();
}

void load_quote_c(void **data, int size, int sort_method) {
	assert(size >= 0 && static_cast<std::size_t>(size) <= kMaxChannelCnt);
	int i;
	all_data.size = size;
	all_data.sort_method = sort_method;
	for (i = 0; i < size; i++) {
		channel_data_t *ch = static_cast<channel_data_t *>(data[i]);
		// 期货因子 demo：mi_type==357 时对首分钟窗口内记录按 local_time 原地排序后再参与 k 路归并。
		// 注意：这是“按通道预处理”，不是全局排序；全局时序仍由后续败者树逐条弹出保证。
		if (ch->size > 0 && ch->data != nullptr &&
		    reinterpret_cast<common_quote_t *>(ch->data)->mi_type == kMiType357PreMerge) {
			sort_one_min_date(ch);
		}
		convert_to_quote_tick(ch, &all_data.q[i], i, 0);
		all_data.d[i] = *ch;
	}
	init_loser_tree();
}

void load_quote_c(std::vector<quote_head_t> &sort_quote, int sort_method) {
	assert(sort_quote.size() <= kMaxChannelCnt);
	unsigned int quote_size = 0;

	for (; quote_size < sort_quote.size(); ++quote_size) {
		int type = sort_quote[quote_size].mi_type;
		quote_mi_type[quote_size] = type;
		_buf[quote_size].data = sort_quote[quote_size].data;
		_buf[quote_size].itemsize = sort_quote[quote_size].itemsize;
		_buf[quote_size].size = sort_quote[quote_size].size;

		_data[quote_size] = &_buf[quote_size];
	}
	load_quote_c(_data, static_cast<int>(sort_quote.size()), sort_method);
}

} // namespace merge_sort
