#include "sdp_handler.h"
#include "utils/log.h"
#include "utils/rdtsc.h"
#include <cmath>

#define SINGLE_ORDER_FUTURE_MAX_VAL 290
#define SINGLE_ORDER_STOCK_MAX_VAL  1000000
#define PAY_UP_TICKS 3
#define DEBUG_MODE -1
#define COLUMN_HEADER_SIZE (64)

extern send_data send_log;
extern int int_time;
extern bool debug_mode;
const static char STATUS[][64] = { "SUCCEED", "ENTRUSTED", "PARTED", "CANCELED", "REJECTED", "CANCEL_REJECTED", "INTRREJECTED", "UNDEFINED_STATUS" };
const static char OPEN_CLOSE_STR[][16] = { "OPEN", "CLOSE", "CLOSE_TOD", "CLOSE_YES" };
const static char BUY_SELL_STR[][8] = { "BUY", "SELL" };

static st_data_t g_st_data_t = { 0 };
static order_t g_send_order = { 0 };
static special_order_t g_special_order = { 0 };


int SDPHandler::m_send_info_func(int type, int length, void *data)
{
	// 如果确保外部是单线程调用m_send_info_func，则此处的slock可以去除
    while (slock.test_and_set(std::memory_order_acquire))  // acquire lock
            ; // spin
    real_send_info_func(type, length, data);
    slock.clear(std::memory_order_release);
	return 0;
}


SDPHandler::SDPHandler(int type, int length, void * cfg)
{
	if (length == DEBUG_MODE)
		debug_mode = true;

    run_mode = type;

	m_orders = new OrderHash();
	m_contracts = new MyCRCHash<Contract>(1024);

    m_buf = new char[COLUMN_HEADER_SIZE * FACTOR_CNT_MAX];
	m_factor_v2_buff = nullptr;
	m_bulk_buff = nullptr;
	m_factor_matrix_buff = nullptr;
	m_factor_bulk_cursor = 0;
	m_factor_matrix_cursor = 0;
	m_config = (st_config_t*)cfg;
	m_send_resp_func = m_config->pass_rsp_hdl;
	m_send_order_func = m_config->proc_order_hdl;
	real_send_info_func = m_config->send_info_hdl;
	send_log = m_config->send_info_hdl;

    LOG_LN("SDP handler init run mode %d", run_mode);

	m_strat_id = m_config->st_id;
	PRINT_INFO("Processing: %d %s", m_config->trading_date, m_config->day_night == 0 ? "Day":"Night");

	// 测量CPU主频(cpus_mhz)，注意: 动态测量会有一些误差，范围很小，在±0.10 MHz以内
	// 为了后续计时统计的稳定性，只保留各位精度（四舍五入），作为最终的CPU主频，避免因小数误差引入计时偏差
	double measured_cpu_mhz = get_cpu_mhz();
	// 这里加0.5是为了实现对measured_cpu_mhz的四舍五入，将其近似为最接近的整数MHz。
	// static_cast<int>本身是直接截断小数部分，为了实现“四舍五入”，在转换前加0.5。
	int cpu_mhz_int = static_cast<int>(measured_cpu_mhz + 0.5);
	m_cpu_mhz = static_cast<double>(cpu_mhz_int);
	m_rdtsc_scaler = 1.0 / m_cpu_mhz;
	LOG_LN("Measured CPU MHz: %.3f", measured_cpu_mhz);
	LOG_LN("Final CPU MHz used (rounded integer): %d", cpu_mhz_int);

	for (int i = 0; i < m_config->accounts.size(); i++) {
		c_account_t& l_account = m_accounts.get_next_free_node();
		strncpy(l_account.account, m_config->accounts[i].account.c_str(), sizeof(l_account.account));
		l_account.cash_available = m_config->accounts[i].cash_available;
		l_account.cash_asset = m_config->accounts[i].cash_asset;
		l_account.exch_rate = m_config->accounts[i].exch_rate;
		l_account.currency = m_config->accounts[i].currency;
		m_accounts.insert_current_node(l_account.account);

		PRINT_INFO("account: %s, cash_available: %f, cash_asset: %f, exch_rate: %f, currency: %d",
			l_account.account, l_account.cash_available, l_account.cash_asset, l_account.exch_rate,
			l_account.currency);
		LOG_LN("account: %s, cash_available: %f, cash_asset: %f, exch_rate: %f, currency: %d",
			l_account.account, l_account.cash_available, l_account.cash_asset, l_account.exch_rate,
			l_account.currency);
	}

	for (int i = 0; i < m_config->contracts.size(); i++) {
		Contract& l_instr = m_contracts->get_next_free_node();
		contract_t& l_config_instr = m_config->contracts[i];
		strncpy(l_instr.symbol, l_config_instr.symbol.c_str(), sizeof(l_instr.symbol));
		strncpy(l_instr.account, l_config_instr.account.c_str(), sizeof(l_instr.account));
		l_instr.quote_hist = &m_quote_hist[i];
		l_instr.max_pos = 1000;
		l_instr.exch = (EXCHANGE)l_config_instr.exch;
		l_instr.max_accum_open_vol = l_config_instr.max_accum_open_vol;
		l_instr.max_cancel_limit = l_config_instr.max_cancel_limit;
		l_instr.order_cum_open_vol = 0;
		l_instr.single_side_max_pos = 0;
		if (l_instr.single_side_max_pos > 0)
			m_use_lock_pos = true;
		l_instr.expiration_date = l_config_instr.expiration_date;

		l_instr.pre_long_qty_remain = l_instr.pre_long_position =  l_config_instr.yesterday_pos.long_volume;
		l_instr.pre_short_qty_remain = l_instr.pre_short_position = l_config_instr.yesterday_pos.short_volume;
		l_instr.positions[LONG_OPEN].qty = l_config_instr.today_pos.long_volume;
		l_instr.positions[LONG_OPEN].notional = l_config_instr.today_pos.long_price * l_config_instr.today_pos.long_volume;
		l_instr.positions[SHORT_OPEN].qty = l_config_instr.today_pos.short_volume;
		l_instr.positions[SHORT_OPEN].notional = l_config_instr.today_pos.short_price * l_config_instr.today_pos.short_volume;

		l_instr.fee_by_lot = l_config_instr.fee.fee_by_lot;
		l_instr.exchange_fee = l_config_instr.fee.exchange_fee;
		l_instr.yes_exchange_fee = l_config_instr.fee.yes_exchange_fee;
		l_instr.broker_fee = l_config_instr.fee.broker_fee;
		l_instr.stamp_tax = l_config_instr.fee.stamp_tax;
		l_instr.acc_transfer_fee = l_config_instr.fee.acc_transfer_fee;
		l_instr.tick_size = l_config_instr.tick_size;
		l_instr.multiplier = l_config_instr.multiple;
		m_contracts->insert_current_node(l_instr.symbol);

		PRINT_INFO("symbol: %s, account: %s, exch: %c, max_accum_open_vol: %d, max_cancel_limit: %d, order_cum_open_vol: %d, expiration_date: %d, "
			"pre_long_position: %d, pre_short_position: %d, pre_long_qty_remain: %d, pre_short_qty_remain: %d, today_long_pos: %d, today_long_price: %f, today_short_pos: %d, today_short_price: %f, "
			"fee_by_lot: %d, exchange_fee : %f, yes_exchange_fee : %f, broker_fee : %f, stamp_tax : %f, acc_transfer_fee : %f, tick_size : %f, multiplier : %f",
			l_instr.symbol, l_instr.account, l_instr.exch, l_instr.max_accum_open_vol, l_instr.max_cancel_limit, l_instr.order_cum_open_vol, l_instr.expiration_date,
			l_instr.pre_long_position, l_instr.pre_short_position, l_instr.pre_long_qty_remain, l_instr.pre_short_qty_remain, l_config_instr.today_pos.long_volume, l_config_instr.today_pos.long_price, l_config_instr.today_pos.short_volume, l_config_instr.today_pos.short_price,
			l_instr.fee_by_lot, l_instr.exchange_fee, l_instr.yes_exchange_fee, l_instr.broker_fee, l_instr.stamp_tax, l_instr.acc_transfer_fee, l_instr.tick_size, l_instr.multiplier);
		LOG_LN("symbol: %s, account: %s, exch: %c, max_accum_open_vol: %d, max_cancel_limit: %d, order_cum_open_vol: %d, expiration_date: %d, "
			"pre_long_position: %d, pre_short_position: %d, pre_long_qty_remain: %d, pre_short_qty_remain: %d, today_long_pos: %d, today_long_price: %f, today_short_pos: %d, today_short_price: %f, "
			"fee_by_lot: %d, exchange_fee : %f, yes_exchange_fee : %f, broker_fee : %f, stamp_tax : %f, acc_transfer_fee : %f, tick_size : %f, multiplier : %f",
			l_instr.symbol, l_instr.account, l_instr.exch, l_instr.max_accum_open_vol, l_instr.max_cancel_limit, l_instr.order_cum_open_vol, l_instr.expiration_date,
			l_instr.pre_long_position, l_instr.pre_short_position, l_instr.pre_long_qty_remain, l_instr.pre_short_qty_remain, l_config_instr.today_pos.long_volume, l_config_instr.today_pos.long_price, l_config_instr.today_pos.short_volume, l_config_instr.today_pos.short_price,
			l_instr.fee_by_lot, l_instr.exchange_fee, l_instr.yes_exchange_fee, l_instr.broker_fee, l_instr.stamp_tax, l_instr.acc_transfer_fee, l_instr.tick_size, l_instr.multiplier);
	}
}

SDPHandler::~SDPHandler()
{
	LOG_LN("It is over: %s", __FUNCTION__);
	delete_mem(m_orders);
	delete_mem(m_contracts);
    delete[] m_buf;
	if(m_factor_v2_buff != nullptr)
	{
		delete[] m_factor_v2_buff;
		m_factor_v2_buff = nullptr;
	}
	if(m_bulk_buff != nullptr)
	{
		delete [] m_bulk_buff;
		m_bulk_buff = nullptr;
	}
	if (m_factor_matrix_buff != nullptr)
	{
		delete[] m_factor_matrix_buff;
		m_factor_matrix_buff = nullptr;
	}
}

start_time_t SDPHandler::get_quote_time(int type, void *quote)
{
    start_time_t t;
    t.local_time = 0;
    // 原实现：switch 内每个 case 里原先均为下面这一行：
    // t.local_time = *(long *)(quote - 16);
    // HACK: (by gaowang) 标准 C++ 不允许对 void* 做指针算术；现均改为 *(long *)((char *)quote - 16)，消除 -Wpedantic。
    switch((FEED_DATA_TYPE)type) {
        case DEFAULT_FUTURE_QUOTE:
        {
            Futures_Internal_Book *fs = (Futures_Internal_Book *)quote;
            t.exch_time = fs->int_time;
			// t.local_time = *(long *)(quote - 16);
            t.local_time = *(long *)((char *)quote - 16);
            break;
        }
        case DEFAULT_STOCK_QUOTE:
        {
            Stock_Internal_Book *sq = (Stock_Internal_Book *)quote;
            t.exch_time = sq->exch_time;
			// t.local_time = *(long *)(quote - 16);
			t.local_time = *(long *)((char *)quote - 16);
            break;
        }
        case DEFAULT_BAR_QUOTE:
        {
            Internal_Bar *tb = (Internal_Bar *)quote;
            t.exch_time = tb->int_time;
			// t.local_time = *(long *)(quote - 16);
			t.local_time = *(long *)((char *)quote - 16);
            break;
        }
        case DEFAULT_FOREX:
        {
            Forex_Internal_Book *fo = (Forex_Internal_Book *)quote;
            t.exch_time = fo->int_time;
			// t.local_time = *(long *)(quote - 16);
			t.local_time = *(long *)((char *)quote - 16);
            break;
        }
        case DEFAULT_STOCK_ORDER_QUEUE:
        {
            Stock_Queue_Internal_Book *soq = (Stock_Queue_Internal_Book *)quote;
            t.exch_time = soq->int_time;
			// t.local_time = *(long *)(quote - 16);
			t.local_time = *(long *)((char *)quote - 16);
            break;
        }
        case DEFAULT_STOCK_ORDER:
        {
            Stock_Order_Internal_Book *so = (Stock_Order_Internal_Book *)quote;
            t.exch_time = so->int_time;
			// t.local_time = *(long *)(quote - 16);
			t.local_time = *(long *)((char *)quote - 16);
            break;
        }
        case DEFAULT_STOCK_TRANSACTION:
        {
            Stock_Transaction_Internal_Book *st = (Stock_Transaction_Internal_Book *)quote;
            t.exch_time = st->int_time;
			// t.local_time = *(long *)(quote - 16);
			t.local_time = *(long *)((char *)quote - 16);
            break;
        }
        case SHARE_FACTOR:
        {
            my_factor_t *sf = (my_factor_t *)quote;
            t.exch_time = sf->exch_time;
			// t.local_time = *(long *)(quote - 16);
			t.local_time = *(long *)((char *)quote - 16);
            break;
        }
        case SHARE_POSITION:
        {
            my_share_position_t *sp = (my_share_position_t *)quote;
            t.exch_time = sp->exch_time;
			// t.local_time = *(long *)(quote - 16);
			t.local_time = *(long *)((char *)quote - 16);
            break;
        }
		case SHARE_FACTOR_V2:
		{
			my_factor_v2 *f2 = (my_factor_v2*)quote;
			t.exch_time = f2->exch_time;
			// t.local_time = *(long *)(quote - 16);
			t.local_time = *(long *)((char *)quote - 16);
			break;
		}
		case SHARE_FACTOR_DOUBLE_V2:
		{
			my_factor_double_v2 *f2 = (my_factor_double_v2*)quote;
			t.exch_time = f2->exch_time;
			// t.local_time = *(long *)(quote - 16);
			t.local_time = *(long *)((char *)quote - 16);
			break;
		}
		case SHARE_FACTOR_BULK:
		{
			my_factor_v2 *f2 = (my_factor_v2*)quote;
			t.exch_time = f2->exch_time;
			// t.local_time = *(long *)(quote - 16);
			t.local_time = *(long *)((char *)quote - 16);
			break;
		}
		case SHARE_FACTOR_DOUBLE_BULK:
		{
			my_factor_v2 *f2 = (my_factor_v2*)quote;
			t.exch_time = f2->exch_time;
			// t.local_time = *(long *)(quote - 16);
			t.local_time = *(long *)((char *)quote - 16);
			break;
		}
		case STOCK_ORDER_NEW:
		{
			Stock_Order_Internal_Book_New *so = (Stock_Order_Internal_Book_New*)quote;
			t.exch_time = so->int_time;
			// t.local_time = *(long *)(quote - 16);
			t.local_time = *(long *)((char *)quote - 16);
			break;
		}
		case STOCK_TRANSACTION_NEW:
		{
			Stock_Transaction_Internal_Book_New *st = (Stock_Transaction_Internal_Book_New*)quote;
			t.exch_time = st->int_time;
			// t.local_time = *(long *)(quote - 16);
			t.local_time = *(long *)((char *)quote - 16);
			break;
		}
        default:
        {
            t.exch_time = 0;
            t.local_time = 0;
        }
    }
//    printf("the run mode is %d\n", run_mode);
    // gettimeofday(&t.machine_time, 0);
	t.rdtsc_num = sdp_rdtsc();
    return t;
}

int SDPHandler::send_factor_columns(std::vector<std::string> &columns)
{
	char *tmp_buf = m_buf;
	for (size_t i = 0; i < columns.size(); i++)
	{
		strncpy(tmp_buf, columns[i].c_str(), COLUMN_HEADER_SIZE);
		tmp_buf += COLUMN_HEADER_SIZE;
	}
	m_send_info_func(S_PUBLISH_FACTOR_COLUMNS, columns.size() * COLUMN_HEADER_SIZE, m_buf);
	return 0;
}

int SDPHandler::send_factor_columns_v2(std::vector<std::string> &columns, int row_count)
{
	if(m_factor_v2_buff == nullptr)
	{
		m_factor_v2_buff = new char[COLUMN_HEADER_SIZE * columns.size()];
	}
	if(m_bulk_buff == nullptr)
	{
		int item_size = sizeof(my_factor_v2) + columns.size() * sizeof(double);
		m_bulk_buff = new char[item_size * row_count];
	}
	if (m_factor_matrix_buff == nullptr)
	{
		int item_size = sizeof(my_factor_v2) + columns.size() * sizeof(double);
		m_factor_matrix_buff = new char[item_size * row_count];
	}
	m_factor_v2_count = columns.size();
	char *tmp_buf = m_factor_v2_buff;
	for (size_t i = 0; i < columns.size(); i++)
	{
		strncpy(tmp_buf, columns[i].c_str(), COLUMN_HEADER_SIZE);
		tmp_buf += COLUMN_HEADER_SIZE;
	}
	m_send_info_func(S_PUBLISH_FACTOR_V2_COLUMNS, columns.size() * COLUMN_HEADER_SIZE, m_factor_v2_buff);
	return 0;
}

int SDPHandler::send_factor(const char *ticker, start_time_t *start ,std::vector<double> &factors)
{
    // struct timeval tval;
    // gettimeofday(&tval, 0);
	uint64_t now_rdtsc_num = sdp_rdtsc();
//    printf("exch_time %ld\n", start->exch_time);
//    printf("local_time %ld\n", start->local_time);
    my_factor_t m_factor = {0};
	m_factor.fid = m_config->st_id;
	strncpy(m_factor.ticker, ticker, sizeof(m_factor.ticker) - 1);
    m_factor.exch_time = start->exch_time;
    m_factor.local_time = start->local_time;
    // m_factor.elapsed_time = (tval.tv_sec - start->machine_time.tv_sec) * 1000000L +
    //                      tval.tv_usec - start->machine_time.tv_usec;
	m_factor.elapsed_time = static_cast<long>((now_rdtsc_num - start->rdtsc_num) * m_rdtsc_scaler);
//    printf("elapsed_time %ld\n", m_factor.elapsed_time);
//    printf("the localtime %ld\n", m_factor.local_time);
    m_factor.date = m_config->trading_date;
    m_factor.count = factors.size();
    for (size_t i = 0; i < factors.size(); i++) {
		m_factor.value[i] = factors[i];
	}
	m_send_info_func(S_PUBLISH_FACTOR, sizeof(my_factor_t), (void *)&m_factor);
	// Contract& instr = m_contracts->at(ticker);
	// lock instr m_factor
//	std::unique_lock<std::mutex> lock(instr.mutex_);
	// LOG("send factor ticker:%s, exch_time %ld, local_time %ld, elapsed_time %ld\n",
	//      ticker, m_factor.exch_time, m_factor.local_time, m_factor.elapsed_time);
	return 0;
}

int SDPHandler::send_factor_v2(const char *ticker, start_time_t *t, std::vector<double> &factors, bool is_last /* = false */)
{
	// struct timeval tval;
	// gettimeofday(&tval, 0);
	uint64_t now_rdtsc_num = sdp_rdtsc();
	int factor_size = sizeof(my_factor_double_v2) + sizeof(double) * factors.size();
	my_factor_double_v2 *factor_data = (my_factor_double_v2*)malloc(factor_size);
	memset(factor_data, 0, factor_size);
	factor_data->flag = 0;
	if (is_last)
	{
		factor_data->flag = 0x01;
	}
	factor_data->fid = m_config->st_id;
	strncpy(factor_data->ticker, ticker, sizeof(factor_data->ticker) - 1);
	factor_data->exch_time = t->exch_time;
	factor_data->local_time = t->local_time;
	// factor_data->elapsed_time = (tval.tv_sec - t->machine_time.tv_sec) * 1000000L +
	//     tval.tv_usec - t->machine_time.tv_usec;
	factor_data->elapsed_time = static_cast<long>((now_rdtsc_num - t->rdtsc_num) * m_rdtsc_scaler);
	factor_data->date = m_config->trading_date;
	factor_data->count = factors.size();
	for (size_t i = 0; i < factors.size(); i++) {
		factor_data->value[i] = factors[i];
	}

	m_send_info_func(S_PUBLISH_FACTOR_DOUBLE_V2, factor_size, (void*)factor_data);
	// Contract& instr = m_contracts->at(ticker);
	// LOG("send factor v2 double ticker:%s, exch_time %ld, local_time %ld, elapsed_time %ld\n",
	// 	ticker, factor_data->exch_time, factor_data->local_time, factor_data->elapsed_time);
	free(factor_data);
	return 0;
}
static int size_alignment(int size)
{
	return int(ceil(size / 8.0)) * 8;
}

int SDPHandler::send_factor_v2(const char *ticker, start_time_t *t, std::vector<float> &factors, bool is_last /* = false */)
{
	// struct timeval tval;
	// gettimeofday(&tval, 0);
	uint64_t now_rdtsc_num = sdp_rdtsc();
	int factor_size = sizeof(my_factor_v2) + sizeof(float) * factors.size();
	my_factor_v2 *factor_data = (my_factor_v2*)malloc(factor_size);
	memset(factor_data, 0, factor_size);
	factor_data->flag = 0;
	if (is_last)
	{
		factor_data->flag = 0x01;
	}
	factor_data->fid = m_config->st_id;
	strncpy(factor_data->ticker, ticker, sizeof(factor_data->ticker) - 1);
	factor_data->exch_time = t->exch_time;
	factor_data->local_time = t->local_time;
	// factor_data->elapsed_time = (tval.tv_sec - t->machine_time.tv_sec) * 1000000L +
	//     tval.tv_usec - t->machine_time.tv_usec;
	factor_data->elapsed_time = static_cast<long>((now_rdtsc_num - t->rdtsc_num) * m_rdtsc_scaler);
	factor_data->date = m_config->trading_date;
	factor_data->count = factors.size();
	for (size_t i = 0; i < factors.size(); i++) {
		factor_data->value[i] = factors[i];
	}
	m_send_info_func(S_PUBLISH_FACTOR_V2, factor_size, (void*)factor_data);
	// Contract& instr = m_contracts->at(ticker);
	// LOG("send factor v2 ticker:%s, exch_time %ld, local_time %ld, elapsed_time %ld\n",
	// 	ticker, factor_data->exch_time, factor_data->local_time, factor_data->elapsed_time);
	free(factor_data);
	return 0;
}

int SDPHandler::send_factor_bulk(const char *ticker, start_time_t *t, std::vector<float> &factors, bool is_last)
{
	// struct timeval tval;
	// gettimeofday(&tval, 0);
	uint64_t now_rdtsc_num = sdp_rdtsc();
	int factor_size = size_alignment(sizeof(my_factor_v2) + sizeof(float) * factors.size());
	char* p_cur =  (char*)m_bulk_buff + factor_size * m_factor_bulk_cursor;
	m_factor_bulk_cursor++;
	memset(p_cur, 0, factor_size);
	my_factor_v2 *factor_data = (my_factor_v2*)p_cur;
	factor_data->flag = 0;
	if (is_last)
	{
		factor_data->flag = 0x01;
	}
	factor_data->fid = m_config->st_id;
	strncpy(factor_data->ticker, ticker, sizeof(factor_data->ticker) - 1);
	factor_data->exch_time = t->exch_time;
	factor_data->local_time = t->local_time;
	// factor_data->elapsed_time = (tval.tv_sec - t->machine_time.tv_sec) * 1000000L +
	//     tval.tv_usec - t->machine_time.tv_usec;
	factor_data->elapsed_time = static_cast<long>((now_rdtsc_num - t->rdtsc_num) * m_rdtsc_scaler);
	factor_data->date = m_config->trading_date;
	factor_data->count = factors.size();
	for (size_t i = 0; i < factors.size(); i++) {
		factor_data->value[i] = factors[i];
	}
	// memcpy(factor_data->value,factors.data(),sizeof(float)*factors.size());
	if (is_last)
	{
		m_send_info_func(S_PUBLISH_FACTOR_BULK_V2, factor_size * m_factor_bulk_cursor, (void*)m_bulk_buff);
		m_factor_bulk_cursor = 0;
	}
	// Contract& instr = m_contracts->at(ticker);
	// LOG("send factor v2 ticker:%s, exch_time %ld, local_time %ld, elapsed_time %ld\n",
	// 	ticker, factor_data->exch_time, factor_data->local_time, factor_data->elapsed_time);
	return 0;
}

int SDPHandler::send_factor_bulk(const char *ticker, start_time_t *t, std::vector<double> &factors, bool is_last)
{
	// struct timeval tval;
	// gettimeofday(&tval, 0);
	uint64_t now_rdtsc_num = sdp_rdtsc();
	int factor_size = sizeof(my_factor_double_v2) + sizeof(double) * factors.size();
	char* p_cur =  m_bulk_buff + factor_size * m_factor_bulk_cursor;
	m_factor_bulk_cursor++;
	memset(p_cur, 0, factor_size);
	my_factor_double_v2 *factor_data = (my_factor_double_v2*)p_cur;
	factor_data->flag = 0;
	if (is_last)
	{
		factor_data->flag = 0x01;
	}
	factor_data->fid = m_config->st_id;
	strncpy(factor_data->ticker, ticker, sizeof(factor_data->ticker) - 1);
	factor_data->exch_time = t->exch_time;
	factor_data->local_time = t->local_time;
	// factor_data->elapsed_time = (tval.tv_sec - t->machine_time.tv_sec) * 1000000L +
	//     tval.tv_usec - t->machine_time.tv_usec;
	factor_data->elapsed_time = static_cast<long>((now_rdtsc_num - t->rdtsc_num) * m_rdtsc_scaler);
	factor_data->date = m_config->trading_date;
	factor_data->count = factors.size();
	memcpy(factor_data->value,factors.data(),sizeof(double)*factors.size());
	if (is_last)
	{
		m_send_info_func(S_PUBLISH_FACTOR_BULK_DOUBLE_V2, factor_size * m_factor_bulk_cursor, (void*)m_bulk_buff);
		m_factor_bulk_cursor = 0;
	}
	// Contract& instr = m_contracts->at(ticker);
	// LOG("send factor v2 ticker:%s, exch_time %ld, local_time %ld, elapsed_time %ld\n",
	// 	ticker, factor_data->exch_time, factor_data->local_time, factor_data->elapsed_time);
	return 0;
}

int SDPHandler::send_factor_matrix(std::vector<std::string> &tickers, start_time_t *t, std::vector<std::vector<float>> &factors_matrix,
	long overwrite_elapsed_us)
{
	if (factors_matrix.size() == 0)
	{
		return 0;
	}
	// struct timeval tval;
	// gettimeofday(&tval, 0);
	uint64_t now_rdtsc_num = sdp_rdtsc();
	int factor_size = size_alignment(sizeof(my_factor_v2) + sizeof(float) * factors_matrix[0].size());

	for (int i = 0; i < tickers.size(); ++i)
	{
		std::string &ticker = tickers[i];
		std::vector<float> &factors = factors_matrix[i];
		if (factors.size() != m_factor_v2_count)
		{
			LOG("[error] send factor matrix error. The number of factor columns does not match, expected_columns:%d, actual_columns:%d\n", m_factor_v2_count, factors.size());
			std::abort();
		}

		char* p_cur =  m_factor_matrix_buff + factor_size * m_factor_matrix_cursor;
		m_factor_matrix_cursor++;
		memset(p_cur, 0, factor_size);
		my_factor_v2 *factor_data = (my_factor_v2*)p_cur;
		factor_data->flag = 0;
		if (i == tickers.size() - 1)
		{
			factor_data->flag = 0x01;
		}
		factor_data->fid = m_config->st_id;
		strncpy(factor_data->ticker, ticker.c_str(), sizeof(factor_data->ticker) - 1);
		factor_data->exch_time = t->exch_time;
		factor_data->local_time = t->local_time;
		// factor_data->elapsed_time = (tval.tv_sec - t->machine_time.tv_sec) * 1000000L +
		// 	   tval.tv_usec - t->machine_time.tv_usec;
		if (overwrite_elapsed_us >= 0) {
			factor_data->elapsed_time = overwrite_elapsed_us;
		} else {
			factor_data->elapsed_time = static_cast<long>((now_rdtsc_num - t->rdtsc_num) * m_rdtsc_scaler);
		}
		factor_data->date = m_config->trading_date;
		factor_data->count = factors.size();
		memcpy(factor_data->value, factors.data(), sizeof(float) * factors.size());

		// Contract& instr = m_contracts->at(ticker.c_str());
		// LOG("send factor matrix ticker:%s, exch_time %ld, local_time %ld, elapsed_time %ld\n",
		// 	ticker.c_str(), factor_data->exch_time, factor_data->local_time, factor_data->elapsed_time);
	}

	m_send_info_func(S_PUBLISH_FACTOR_BULK_V2, factor_size * m_factor_matrix_cursor, (void*)m_factor_matrix_buff);
	m_factor_matrix_cursor = 0;

	return 0;
}

int SDPHandler::send_factor_matrix(std::vector<std::string> &tickers, start_time_t *t, std::vector<std::vector<double>> &factors_matrix,
	long overwrite_elapsed_us)
{
	if (factors_matrix.size() == 0)
	{
		return 0;
	}
	// struct timeval tval;
	// gettimeofday(&tval, 0);
	uint64_t now_rdtsc_num = sdp_rdtsc();
	int factor_size = sizeof(my_factor_double_v2) + sizeof(double) * factors_matrix[0].size();

	for (int i = 0; i < tickers.size(); ++i)
	{
		std::string &ticker = tickers[i];
		std::vector<double> &factors = factors_matrix[i];
		if (factors.size() != m_factor_v2_count)
		{
			LOG("[error] send factor matrix error. The number of factor columns does not match, expected_columns:%d, actual_columns:%d\n", m_factor_v2_count, factors.size());
			std::abort();
		}

		char* p_cur =  m_factor_matrix_buff + factor_size * m_factor_matrix_cursor;
		m_factor_matrix_cursor++;
		memset(p_cur, 0, factor_size);
		my_factor_double_v2 *factor_data = (my_factor_double_v2*)p_cur;
		factor_data->flag = 0;
		if (i == tickers.size() - 1)
		{
			factor_data->flag = 0x01;
		}
		factor_data->fid = m_config->st_id;
		strncpy(factor_data->ticker, ticker.c_str(), sizeof(factor_data->ticker) - 1);
		factor_data->exch_time = t->exch_time;
		factor_data->local_time = t->local_time;
		// factor_data->elapsed_time = (tval.tv_sec - t->machine_time.tv_sec) * 1000000L +
		//     tval.tv_usec - t->machine_time.tv_usec;
		if (overwrite_elapsed_us >= 0) {
			factor_data->elapsed_time = overwrite_elapsed_us;
		} else {
			factor_data->elapsed_time = static_cast<long>((now_rdtsc_num - t->rdtsc_num) * m_rdtsc_scaler);
		}
		factor_data->date = m_config->trading_date;
		factor_data->count = factors.size();
		memcpy(factor_data->value, factors.data(), sizeof(double) * factors.size());

		// Contract& instr = m_contracts->at(ticker.c_str());
		// LOG("send factor matrix ticker:%s, exch_time %ld, local_time %ld, elapsed_time %ld\n",
		// 	ticker.c_str(), factor_data->exch_time, factor_data->local_time, factor_data->elapsed_time);
	}

	m_send_info_func(S_PUBLISH_FACTOR_BULK_DOUBLE_V2, factor_size * m_factor_matrix_cursor, (void*)m_factor_matrix_buff);
	m_factor_matrix_cursor = 0;

	return 0;
}

void SDPHandler::get_bulk_data(int type,void *quote,bulk_factor_datas_t& bulk_factors)
{
	bulk_data * data =  (bulk_data*)quote;
	bulk_factors.rows = data->row_count;
	bulk_factors.columns = data->column_count;

	if(type == SHARE_FACTOR_BULK)
	{
		bulk_factors.item_size  =size_alignment(sizeof(my_factor_v2) + sizeof(float) * data->column_count) + 24;
	}
	else
	{
		bulk_factors.item_size  = sizeof(my_factor_v2) + sizeof(double) * data->column_count + 24;
	}
	bulk_factors.datas = (bulk_factor_t*)data->bulk_ptr;
}

int SDPHandler::send_position(const char *ticker, start_time_t * start, long volume, const char *account)
{
    my_share_position_t m_pos = {0};
	m_pos.fid = m_config->st_id;
    m_pos.exch_time = start->exch_time;
    m_pos.local_time = start->local_time;
    m_pos.date = m_config->trading_date;
    strncpy(m_pos.ticker, ticker, sizeof(m_pos.ticker) - 1);
	strncpy(m_pos.account, account, sizeof(m_pos.account) - 1);
	m_pos.volume = volume;
	real_send_info_func(S_PUBLISH_POSITION, sizeof(my_share_position_t), (void *)&m_pos);
	LOG("send position ticker: %s, volume: %d, exch_time %ld, local_time %ld\n",
	     ticker, volume, m_pos.exch_time, m_pos.local_time);
	return 0;
}

int SDPHandler::on_book(int type, int length, void * book)
{
	(void)length;
	int ret = 0;
	switch (type) {
		case DEFAULT_STOCK_QUOTE: {
			Stock_Internal_Book *s_book = (Stock_Internal_Book *)((st_data_t*)book)->info;
			Contract &instr = m_contracts->at(s_book->ticker);
			if (instr.quote_hist->find(type) == instr.quote_hist->end()) {
			    instr.quote_hist->insert(std::make_pair(type, std::vector<void *> {book}));
			} else {
			    (*instr.quote_hist)[type].push_back(book);
			}
			int_time = s_book->exch_time;
			update_contracts(s_book);
			break;
		}
		case DEFAULT_FUTURE_QUOTE: {
			Futures_Internal_Book *f_book = (Futures_Internal_Book *)((st_data_t*)book)->info;
			int_time = f_book->int_time;
			update_contracts(f_book);
			Contract &instr = m_contracts->at(f_book->symbol);
			if (instr.quote_hist->find(type) == instr.quote_hist->end()) {
			    instr.quote_hist->insert(std::make_pair(type, std::vector<void *> {book}));
			} else {
			    (*instr.quote_hist)[type].push_back(book);
			}
			break;
		}
	}
	return ret;
}

int SDPHandler::on_response(int type, int length, void * resp)
{
	(void)type;
	(void)length;
	st_response_t* l_resp = (st_response_t*)((st_data_t*)resp)->info;

	PRINT_INFO("Order Resp: %lld %s %s %s %f %d %s %d %s", l_resp->order_id, l_resp->symbol,
		BUY_SELL_STR[l_resp->direction], OPEN_CLOSE_STR[l_resp->open_close], l_resp->exe_price, l_resp->exe_volume,
		STATUS[l_resp->status], l_resp->error_no, l_resp->error_info);
	LOG_LN("Order Resp: %lld %s %s %s %f %d %s %d %s", l_resp->order_id, l_resp->symbol,
		BUY_SELL_STR[l_resp->direction], OPEN_CLOSE_STR[l_resp->open_close], l_resp->exe_price, l_resp->exe_volume,
		STATUS[l_resp->status], l_resp->error_no, l_resp->error_info);

	process_order_resp(l_resp);

	Contract *instr = find_contract(l_resp->symbol);
	PRINT_INFO("Contract: %s pre_long_pos: %d pre_short_pos: %d pre_long_remain: %d pre_short_remain: %d long_pos: %d short_pos: %d long_open: %d long_close: %d short_open: %d short_close: %d\n"
		"pending_buy_open_size: %d pending_sell_open_size: %d pending_buy_close_size: %d pending_sell_close_size: %d\n"
		"Account: %s cash_available: %f",
		instr->symbol, instr->pre_long_position, instr->pre_short_position, instr->pre_long_qty_remain, instr->pre_short_qty_remain, long_position(instr), short_position(instr), instr->positions[LONG_OPEN].qty, instr->positions[LONG_CLOSE].qty, instr->positions[SHORT_OPEN].qty, instr->positions[SHORT_CLOSE].qty,
		instr->pending_buy_open_size, instr->pending_sell_open_size, instr->pending_buy_close_size, instr->pending_sell_close_size,
		instr->account, get_account_cash(instr->account));
	LOG_LN("Contract: %s pre_long_pos: %d pre_short_pos: %d pre_long_remain: %d pre_short_remain: %d long_pos: %d short_pos: %d long_open: %d long_close: %d short_open: %d short_close: %d\n"
		"pending_buy_open_size: %d pending_sell_open_size: %d pending_buy_close_size: %d pending_sell_close_size: %d\n"
		"Account: %s cash_available: %f",
		instr->symbol, instr->pre_long_position, instr->pre_short_position, instr->pre_long_qty_remain, instr->pre_short_qty_remain, long_position(instr), short_position(instr), instr->positions[LONG_OPEN].qty, instr->positions[LONG_CLOSE].qty, instr->positions[SHORT_OPEN].qty, instr->positions[SHORT_CLOSE].qty,
		instr->pending_buy_open_size, instr->pending_sell_open_size, instr->pending_buy_close_size, instr->pending_sell_close_size,
		instr->account, get_account_cash(instr->account));
	return 0;
}

int get_pos_by_side(Contract *a_instr, DIRECTION side)
{
	if (side == ORDER_BUY)
		return long_position(a_instr);
	else if (side == ORDER_SELL)
		return short_position(a_instr);

	return 0;
}

int SDPHandler::send_single_order(Contract * instr, EXCHANGE exch, double price, int size, DIRECTION side, OPEN_CLOSE sig_openclose, bool flag_syn_cancel, bool flag_close_yesterday_pos, INVESTOR_TYPE investor_type, ORDER_TYPE order_type, TIME_IN_FORCE time_in_force)
{
	if (size <= 0) {
		if (size < 0)
			LOG_LN("OrderSize is invalid, OrderSize < 0:%d", size);
		return -1;
	}

	if (price <= 0) {
		LOG_LN("Price is invalid, price <= 0: %.2f!", price);
		return -1;
	}

	if (flag_syn_cancel == true && m_orders->get_pending_cancel_order_size_by_instr(instr) > 0) {
		// LOG_LN("Don't send orders before receiving order cancelled.");
		// HACK: (by gaowang) -pedantic：整句字面串单独作 LOG_LN 首参、无其它实参时，
		// 展开到 printf 式的 check_log_ln 会触发 ISO C99 可变参数相关告警，
		// 故用 "%s" 与第二实参传入正文。
		LOG_LN("%s", "Don't send orders before receiving order cancelled.");
		return -1;
	}

	if (m_orders->full() == true) {
		// LOG_LN("You have too much pending orders, exit!");
		// HACK: (by gaowang) -pedantic：整句字面串单独作 LOG_LN 首参、无其它实参时，
		// 展开到 printf 式的 check_log_ln 会触发 ISO C99 可变参数相关告警，
		// 故用 "%s" 与第二实参传入正文。
		LOG_LN("%s", "You have too much pending orders, exit!");
		return -1;
	}

	g_st_data_t.info = (void*)&g_send_order;

	uint64_t l_order_id;
	int l_size, l_order_count = 0;
	int l_single_order_max_vol = SINGLE_ORDER_FUTURE_MAX_VAL;
	if (exch == SSE || exch == SZSE)
		l_single_order_max_vol = SINGLE_ORDER_STOCK_MAX_VAL;

	for (int l_remaining_size = size; l_remaining_size > 0; l_remaining_size -= l_single_order_max_vol, l_order_count++) {
		l_order_id = 0;
		l_size = (l_remaining_size > l_single_order_max_vol) ? l_single_order_max_vol : l_remaining_size; // take min of the two

		OPEN_CLOSE l_sig_openclose = sig_openclose;
		if (sig_openclose == ORDER_CLOSE && flag_close_yesterday_pos) {
			l_sig_openclose = ORDER_CLOSE_YES;
		}

		int l_compliance = 0;
		g_send_order.exch = exch;
		strcpy(g_send_order.symbol, (char *)instr->symbol);
		g_send_order.volume = l_size;
		g_send_order.price = price;
		g_send_order.direction = side;
		g_send_order.open_close = l_sig_openclose;
		g_send_order.investor_type = investor_type;
		g_send_order.order_type = order_type;
		g_send_order.time_in_force = time_in_force;
		g_send_order.st_id = m_strat_id;
		g_send_order.org_ord_id = 0;
		l_compliance = m_send_order_func(S_PLACE_ORDER_DEFAULT, sizeof(g_st_data_t), (void *)&g_st_data_t);
		l_order_id = g_send_order.order_id;
		switch(l_compliance) {
			case ERROR_REACH_MAX_ACCUMULATE_OPEN_VOL: {
				is_exit_mode = true;
				LOG_LN("%s reached max vol %d allowed to open, Strategy %d send order failed, entering exit mode", instr->symbol, instr->max_accum_open_vol, m_strat_id);
			}
			case ERROR_OPEN_POS_IS_NOT_ENOUGH:
			case ERROR_SELF_MATCHING:
			case ERROR_CASH_IS_NOT_ENOUGH:
			case ERROR_PRE_POS_IS_NOT_ENOUGH_TO_CLOSE: {
				LOG_LN("%d %s Order %d@%f %s %s compliance check failed: %d",
					m_strat_id, instr->symbol, l_size, price,
					BUY_SELL_STR[side], OPEN_CLOSE_STR[sig_openclose], l_compliance);
				l_order_id = 0;
				break;
			}
			case 0: break;
		}

		if (l_order_id != 0) {
			/* If successfully sent an order, make a record of it */
			if (l_order_count >= MAX_ORDER_COUNT - 1) {
				// LOG_LN("Too large order size, send failed!");
				// HACK: (by gaowang) -pedantic：整句字面串单独作 LOG_LN 首参、无其它实参时，
				// 展开到 printf 式的 check_log_ln 会触发 ISO C99 可变参数相关告警，
				// 故用 "%s" 与第二实参传入正文。
				LOG_LN("%s", "Too large order size, send failed!");
				return -3;
			}
			m_cur_ord_id_arr[l_order_count] = l_order_id;
			m_cur_ord_size_arr[l_order_count] = l_size;
			m_total_order_size += l_size;
		}
		else {
			LOG_LN("%s OrderID is 0. The order was sent unsuccessfully", instr->symbol);
			break;
		}
	}

	if (l_order_count > 0) {
		for (int i = 0; i < l_order_count; i++) {
            PRINT_INFO("%d OrderID: %lld Sent single order, %s %s %s %d@%f", m_strat_id, m_cur_ord_id_arr[i], instr->symbol,
                BUY_SELL_STR[side], OPEN_CLOSE_STR[sig_openclose], m_cur_ord_size_arr[i], price);
			LOG_LN("%d OrderID: %lld Sent single order, %s %s %s %d@%f", m_strat_id, m_cur_ord_id_arr[i], instr->symbol,
				BUY_SELL_STR[side], OPEN_CLOSE_STR[sig_openclose], m_cur_ord_size_arr[i], price);
			int index = reverse_index(m_cur_ord_id_arr[i]);
			Order *l_order = m_orders->update_order(index, instr, price, m_cur_ord_size_arr[i],
				side, m_cur_ord_id_arr[i], sig_openclose);
			if (sig_openclose == ORDER_CLOSE_YES || flag_close_yesterday_pos) {
				update_yes_pos(instr, side, m_cur_ord_size_arr[i]);
			}
			update_account_cash(l_order);
		}

		m_new_order_times += l_order_count;
		return 0;
	}
	else {
		return -1;
	}
}

int SDPHandler::send_dma_order(Contract * instr, DIRECTION side, int size, double price, bool flag_syn_cancel)
{
	g_st_data_t.info = (void*)&g_special_order;

	g_special_order.exch = instr->exch;
	strlcpy(g_special_order.symbol, (char *)instr->symbol, SYMBOL_LEN);
	g_special_order.volume = size;
	g_special_order.price = price;
	g_special_order.direction = side;
	g_special_order.sync_cancel = flag_syn_cancel;

	m_send_order_func(S_PLACE_ORDER_DMA, sizeof(g_st_data_t), (void *)&g_st_data_t);
	if(g_special_order.status == 0) {
		LOG_LN("Sent DMA Order, %s %s %d@%f %d", instr->symbol, (side == ORDER_BUY ? "Buy" : "Sell"), size, price, flag_syn_cancel);
	}
	return g_special_order.status;
}

int SDPHandler::send_twap_order(Contract * instr, int ttl_odrsize, DIRECTION side, double price, int start_time, int end_time)
{
	g_st_data_t.info = (void*)&g_special_order;

	g_special_order.exch = instr->exch;
	strlcpy(g_special_order.symbol, (char *)instr->symbol, SYMBOL_LEN);
	g_special_order.volume = ttl_odrsize;
	g_special_order.price = price;
	g_special_order.direction = side;
	g_special_order.start_time = start_time;
	g_special_order.end_time = end_time;

	m_send_order_func(S_PLACE_ORDER_TWAP, sizeof(g_st_data_t), (void *)&g_st_data_t);
	if (g_special_order.status == 0) {
		LOG_LN("Sent TWAP Order, %s %s %d@%f %d %d", instr->symbol, (side == ORDER_BUY ? "Buy" : "Sell"), ttl_odrsize, price, start_time, end_time);
	}
	return g_special_order.status;
}

int SDPHandler::send_vwap_order(Contract * instr, int ttl_odrsize, DIRECTION side, double price, int start_time, int end_time)
{
	g_st_data_t.info = (void*)&g_special_order;

	g_special_order.exch = instr->exch;
	strlcpy(g_special_order.symbol, (char *)instr->symbol, SYMBOL_LEN);
	g_special_order.volume = ttl_odrsize;
	g_special_order.price = price;
	g_special_order.direction = side;
	g_special_order.start_time = start_time;
	g_special_order.end_time = end_time;

	m_send_order_func(S_PLACE_ORDER_VWAP, sizeof(g_st_data_t), (void *)&g_st_data_t);
	if (g_special_order.status == 0) {
		LOG_LN("Sent VWAP Order, %s %s %d@%f %d %d", instr->symbol, (side == ORDER_BUY ? "Buy" : "Sell"), ttl_odrsize, price, start_time, end_time);
	}
	return g_special_order.status;
}

int SDPHandler::cancel_single_order(Order * a_order)
{
	if (!is_cancelable(a_order)) {
		return -1;
	}

	bool is_cancel_compliant = true;

	int l_compliance = 0;

	g_send_order.exch = a_order->exch;
	strcpy(g_send_order.symbol, (char *)a_order->symbol);
	g_send_order.volume = leaves_qty(a_order);
	g_send_order.price = a_order->price;
	g_send_order.direction = a_order->side;
	g_send_order.open_close = a_order->openclose;
	g_send_order.investor_type = a_order->invest_type;
	g_send_order.order_type = a_order->ord_type;
	g_send_order.time_in_force = a_order->time_in_force;
	g_send_order.st_id = m_strat_id;
	g_send_order.org_ord_id = a_order->signal_id;
	l_compliance = m_send_order_func(S_CANCEL_ORDER_DEFAULT, sizeof(g_st_data_t), (void *)&g_st_data_t);

	if (0 != l_compliance) {
		LOG_LN("%d OrderID: %lld Cancel order compliance check failed: %d",
			m_strat_id, a_order->signal_id, l_compliance);
		/* Not Compliant */
		is_cancel_compliant = false;

		if (ERROR_CANCEL_ORDER_REACH_LIMIT == l_compliance) {
			is_exit_mode = true;
			LOG_LN("%s orderID: %lld reached max cancel allowed, Strategy %d cancel order failed, entering exit mode", a_order->symbol, a_order->signal_id, m_strat_id);
		}
	}

	// If we are cancelling a close order, mark it flag to true

	if (is_cancel_compliant) {
		m_cancelled_times++;
		m_total_cancel_size += leaves_qty(a_order);
		a_order->pending_cancel = true;
		m_orders->minus_pending_size(a_order, leaves_qty(a_order));
        PRINT_INFO("%d OrderID: %lld Cancel single order", m_strat_id, a_order->signal_id);
		LOG_LN("%d OrderID: %lld Cancel single order", m_strat_id, a_order->signal_id);
		return 0;
	} else {
		return -1;
	}
}

int SDPHandler::cancel_orders_by_side(Contract * instr, DIRECTION side, OPEN_CLOSE flag)
{
	struct list_head *pos, *n;
	Order *l_ord;

	list_head *ord_list = m_orders->get_order_by_side(side);
	list_for_each_prev_safe(pos, n, ord_list) {
		l_ord = list_entry(pos, Order, pd_link);
		if (l_ord->contr == instr && l_ord->openclose == flag) {
			cancel_single_order(l_ord);
		}
	}

	return 0;
}

int SDPHandler::cancel_orders_with_dif_px(Contract * instr, DIRECTION side, double price)
{
	list_head *cancel_list = m_orders->get_order_by_side(side);
	struct list_head *pos, *n;
	Order *l_ord;
	int l_size = 0;

	list_for_each_prev_safe(pos, n, cancel_list) {
		l_ord = list_entry(pos, Order, pd_link);
		// The Contract Address is the same, save time do string compare
		if (l_ord->contr == instr) {
			if (double_compare(l_ord->price, price) == 0 && is_cancelable(l_ord)) {
				l_size += leaves_qty(l_ord);
			}
			else {
				cancel_single_order(l_ord);
			}
		}
	}

	return l_size;
}

int SDPHandler::cancel_orders_by_side(Contract * instr, DIRECTION side)
{
	struct list_head *pos, *n;
	Order *l_ord;

	list_head *ord_list = m_orders->get_order_by_side(side);
	list_for_each_prev_safe(pos, n, ord_list) {
		l_ord = list_entry(pos, Order, pd_link);
		// The Contract Address is the same�� Save time do string compare
		if (l_ord->contr == instr) {
			cancel_single_order(l_ord);
		}
	}

	return 0;
}

int SDPHandler::cancel_all_orders()
{
	list_t *pos, *n;
	Order *l_ord;

	list_head *buy_list = m_orders->get_order_by_side(ORDER_BUY);
	list_for_each_prev_safe(pos, n, buy_list) {
		l_ord = list_entry(pos, Order, pd_link);
		cancel_single_order(l_ord);
	}

	list_head *sell_list = m_orders->get_order_by_side(ORDER_SELL);
	list_for_each_prev_safe(pos, n, sell_list) {
		l_ord = list_entry(pos, Order, pd_link);
		cancel_single_order(l_ord);
	}

	return 0;
}

int SDPHandler::cancel_all_orders(Contract * instr)
{
	list_t *pos, *n;
	Order *l_ord;

	list_head *buy_list = m_orders->get_order_by_side(ORDER_BUY);
	list_for_each_prev_safe(pos, n, buy_list) {
		l_ord = list_entry(pos, Order, pd_link);
		// The Contract Address is the same, Save time do string compare
		if (l_ord->contr == instr)
			cancel_single_order(l_ord);
	}

	list_head *sell_list = m_orders->get_order_by_side(ORDER_SELL);
	list_for_each_prev_safe(pos, n, sell_list) {
		l_ord = list_entry(pos, Order, pd_link);
		// The Contract Address is the same, Save time do string compare
		if (l_ord->contr == instr)
			cancel_single_order(l_ord);
	}

	return 0;
}

std::vector<void *> * SDPHandler::get_tick_by_type_and_symbol(const char *symbol, int quote_type)
{
    Contract &contr = m_contracts->at(symbol);
    if (contr.quote_hist->find(quote_type) == contr.quote_hist->end()) {
        (*contr.quote_hist)[quote_type] = {};
    }
    return &((*contr.quote_hist)[quote_type]);
}

Contract * SDPHandler::find_contract(char * symbol)
{
	return &m_contracts->at(symbol);
}

int SDPHandler::get_pos_by_side(Contract * a_instr, DIRECTION side)
{
	if (side == ORDER_BUY)
		return long_position(a_instr);
	else if (side == ORDER_SELL)
		return short_position(a_instr);

	return -1;
}

int SDPHandler::get_pending_size_by_side(Contract * a_contr, DIRECTION a_side)
{
	return m_orders->open_order_size(a_contr, a_side);
}

double SDPHandler::get_account_cash(char * account)
{
	return m_accounts.at(account).cash_available;
}

void SDPHandler::update_yes_pos(Contract * instr, DIRECTION side, int size)
{
	if (side == ORDER_BUY) {
		instr->pre_short_qty_remain -= size;
	}
	else if (side == ORDER_SELL) {
		instr->pre_long_qty_remain -= size;
	}
}

bool SDPHandler::process_order_resp(st_response_t * ord_resp)
{
	// In live trading, there's an extra callback with vol=0, ignore it.
	if ((ord_resp->status == SIG_STATUS_PARTED || ord_resp->status == SIG_STATUS_SUCCEED)
		&& ord_resp->exe_volume == 0)
		return false;

	Order* l_order = m_orders->query_order(ord_resp->order_id);
	if (l_order == NULL)
		return false;

	Contract& l_instr = *l_order->contr;

	if (ord_resp->open_close == ORDER_CLOSE_YES) {
		if (ord_resp->status == SIG_STATUS_CANCELED
			|| (ord_resp->status == SIG_STATUS_REJECTED && !l_order->pending_cancel)
			|| ord_resp->status == SIG_STATUS_INTRREJECTED) {
			// Get Contract, and update this position.
			if (ord_resp->direction == ORDER_BUY) {
				l_instr.pre_short_qty_remain += leaves_qty(l_order);
			}
			if (ord_resp->direction == ORDER_SELL) {
				l_instr.pre_long_qty_remain += leaves_qty(l_order);
			}
		}
	}

	if (ord_resp->status == SIG_STATUS_PARTED || ord_resp->status == SIG_STATUS_SUCCEED) {
		l_order->cum_amount += ord_resp->exe_volume * ord_resp->exe_price;
		l_order->cum_qty += ord_resp->exe_volume;
		l_order->last_px = ord_resp->exe_price;
		l_order->last_qty = ord_resp->exe_volume;
		if (l_order->cum_qty < l_order->volume) {
			l_order->status = SIG_STATUS_PARTED;
		}
		else if (l_order->cum_qty == l_order->volume) {
			l_order->status = SIG_STATUS_SUCCEED;
		}

		double l_notional_change = ord_resp->exe_price * ord_resp->exe_volume;

		// If fee by lot, calculated fee based lot size, otherwise use notional change.
		double l_fees = 0.0;
		double l_close_fees = 0.0;
		if (m_is_use_fee) {
			l_fees = get_transaction_fee(&l_instr, ord_resp->exe_volume, ord_resp->exe_price);
			if (ord_resp->open_close == ORDER_CLOSE_YES) {
				// If it's close yesterday, revert back using today's fee.
				l_close_fees -= l_fees;
				// Add two yesterday fee back.
				l_close_fees += (2 * get_transaction_fee(&l_instr, ord_resp->exe_volume, ord_resp->exe_price, true));
			}
			else {
				l_close_fees = l_fees;
			}
		}

		/* fees are added to the notional, like a cost */
		if (ord_resp->direction == ORDER_BUY) {
			if (ord_resp->open_close == ORDER_OPEN) {
				l_instr.positions[LONG_OPEN].notional += (l_notional_change + l_fees);
				l_instr.positions[LONG_OPEN].qty += ord_resp->exe_volume;
			}
			else if (ord_resp->open_close == ORDER_CLOSE) {
				l_instr.positions[LONG_CLOSE].notional += (l_notional_change + l_close_fees);
				l_instr.positions[LONG_CLOSE].qty += ord_resp->exe_volume;
			}
			else if (ord_resp->open_close == ORDER_CLOSE_YES) {
				l_instr.positions[LONG_CLOSE].notional += (l_notional_change + l_close_fees);
				l_instr.positions[LONG_CLOSE].qty += ord_resp->exe_volume;
				l_instr.pre_short_position -= ord_resp->exe_volume;
			}
		}
		else if (ord_resp->direction == ORDER_SELL) {
			l_fees += l_notional_change * l_instr.stamp_tax; /* For stocks, there's stamp tax when selling */

			if (ord_resp->open_close == ORDER_OPEN) {
				l_instr.positions[SHORT_OPEN].notional += (l_notional_change - l_fees);
				l_instr.positions[SHORT_OPEN].qty += ord_resp->exe_volume;
			}
			else if (ord_resp->open_close == ORDER_CLOSE) {
				l_instr.positions[SHORT_CLOSE].notional += (l_notional_change - l_close_fees);
				l_instr.positions[SHORT_CLOSE].qty += ord_resp->exe_volume;
			}
			else if (ord_resp->open_close == ORDER_CLOSE_YES) {
				l_instr.positions[SHORT_CLOSE].notional += (l_notional_change - l_close_fees);
				l_instr.positions[SHORT_CLOSE].qty += ord_resp->exe_volume;
				l_instr.pre_long_position -= ord_resp->exe_volume;
			}
		}
	}

	if (l_order->status != SIG_STATUS_INIT && ord_resp->status == SIG_STATUS_ENTRUSTED) {
		/* If already entrusted, no need to call update_order_list() */
	}
	else {
		l_order->status = (ORDER_STATUS)ord_resp->status;
		update_account_cash(l_order);
	}

	update_pending_status(l_order);
	m_orders->update_order_list(l_order);
	return true;
}

void SDPHandler::update_account_cash(Order* a_order)
{
	if (a_order == NULL) {
		// LOG_LN("update_account_cash: order_t is NULL");
		// HACK: (by gaowang) -pedantic：整句字面串单独作 LOG_LN 首参、无其它实参时，
		// 展开到 printf 式的 check_log_ln 会触发 ISO C99 可变参数相关告警，
		// 故用 "%s" 与第二实参传入正文。
		LOG_LN("%s", "update_account_cash: order_t is NULL");
		return;
	}

	Contract& l_instr = *a_order->contr;
	c_account_t& l_account = m_accounts.at(l_instr.account);

	switch (a_order->status) {
	case SIG_STATUS_INIT: {/* New Order */
		if (a_order->exch == SZSE || a_order->exch == SSE) {  // Stocks, multiplier is 1
			if (a_order->side == ORDER_BUY) {
				l_account.cash_available -= a_order->price * a_order->volume;
			}
			else { /* Stock has stamp tax*/
				l_account.cash_available -= a_order->price * a_order->volume * l_instr.stamp_tax;
			}
			l_account.cash_available -= get_transaction_fee(&l_instr, a_order->volume, a_order->price);
		}
		else {  // Futures
			if (a_order->openclose == ORDER_OPEN) {  // Add back the notional if canceled or rejected. Minus the notional if it's Init.
				l_account.cash_available -= a_order->price * a_order->volume * l_instr.multiplier;
			}
			l_account.cash_available -= get_transaction_fee(&l_instr, a_order->volume, a_order->price) * l_instr.multiplier;
		}
	} break;
	case SIG_STATUS_CANCELED:
	case SIG_STATUS_REJECTED: {   /* Cancel or rejected, resume the cash */
		if (a_order->exch == SZSE || a_order->exch == SSE) {  // Stocks, multiplier is 1
			if (a_order->side == ORDER_BUY) {
				l_account.cash_available += a_order->price * leaves_qty(a_order);
			} else { /* Stock has stamp tax*/
				l_account.cash_available += a_order->price * leaves_qty(a_order) * l_instr.stamp_tax;
			}
			l_account.cash_available += get_transaction_fee(&l_instr, leaves_qty(a_order), a_order->price);
		} else {  // Futures
			if (a_order->openclose == ORDER_OPEN) {  // Add back the notional if canceled or rejected. Minus the notional if it's Init.
				l_account.cash_available += a_order->price * leaves_qty(a_order) * l_instr.multiplier;
			}
			l_account.cash_available += get_transaction_fee(&l_instr, leaves_qty(a_order), a_order->price) * l_instr.multiplier;
		}
	} break;
	case SIG_STATUS_SUCCEED:
	case SIG_STATUS_PARTED: {
		/* Add cash upon part/success sell response */
		if (a_order->exch == SZSE || a_order->exch == SSE) {  // Stocks, multiplier is 1, so we don't need to use "* l_instr->multiplier" here.
			if (a_order->side == ORDER_SELL) {
				l_account.cash_available += a_order->last_px * a_order->last_qty;
			}
		}
		else if (a_order->openclose == ORDER_CLOSE || a_order->openclose == ORDER_CLOSE_YES) {
			// When close a position, we reset the cash.
			// When a position is closed, it will release some cash for openning that account
			if (a_order->side == ORDER_BUY) {
				double l_sell_open_avg_px = l_instr.positions[SHORT_OPEN].notional / l_instr.positions[SHORT_OPEN].qty;
				l_account.cash_available += (l_sell_open_avg_px + (l_sell_open_avg_px - a_order->last_px)) * a_order->last_qty * l_instr.multiplier;
			}
			else if (a_order->side == ORDER_SELL) {
				l_account.cash_available += a_order->last_px * a_order->last_qty * l_instr.multiplier;
				//double l_buy_open_avg_px = l_instr->positions[LONG_OPEN].notional / l_instr->positions[LONG_OPEN].qty;
				//l_account.cash_available += (l_buy_open_avg_px + (a_order->last_px - l_buy_open_avg_px)) * a_order->last_qty * l_instr->multiplier;
			}
		}
		else if (a_order->openclose == ORDER_OPEN) {
			double price_gap = a_order->price - a_order->last_px; // If we send a price higher than last_px, we need to re-adjust the gap to cash.
			l_account.cash_available += price_gap * a_order->last_qty * l_instr.multiplier;
		}
	} break;
	case SIG_STATUS_ENTRUSTED:
		break;
	case SIG_STATUS_CANCEL_REJECTED:
		break;
	default:
		break;
	}
}

void SDPHandler::update_pending_status(Order * a_order) {
	/*Update pending order size*/
	switch (a_order->status) {
		case UNDEFINED_STATUS: {
			break;
		}
		case SIG_STATUS_INIT: {
			break;
		}
		case SIG_STATUS_ENTRUSTED: {
			break;
		}
		case SIG_STATUS_SUCCEED: {
			if (a_order->pending_cancel == false)
				m_orders->minus_pending_size(a_order, a_order->last_qty);
			else
				a_order->pending_cancel = false;
			break;
		}
		case SIG_STATUS_PARTED: {
			if (a_order->pending_cancel == false)
				m_orders->minus_pending_size(a_order, a_order->last_qty);
			break;
		}
		case SIG_STATUS_CANCELED: {
			if (a_order->pending_cancel == false)     //Sometimes in SHFE we will receive cancel response after we send a order. Here We recognize the cancel as rejected.
				m_orders->minus_pending_size(a_order, leaves_qty(a_order));
			else
				a_order->pending_cancel = false;
			break;
		}
		case SIG_STATUS_INTRREJECTED:
		case SIG_STATUS_REJECTED: {
			if (a_order->pending_cancel == true) { //Cancel reject
				m_orders->add_pending_size(a_order, leaves_qty(a_order));
				a_order->pending_cancel = false;
				m_cancelled_times--;
				m_total_cancel_size -= leaves_qty(a_order);
			}
			else {
				m_orders->minus_pending_size(a_order, leaves_qty(a_order));
			}
			break;
		}
		case SIG_STATUS_CANCEL_REJECTED: {
			m_orders->add_pending_size(a_order, leaves_qty(a_order));
			a_order->pending_cancel = false;
			m_cancelled_times--;
			m_total_cancel_size -= leaves_qty(a_order);
			break;
		}
	}
}

bool SDPHandler::update_contracts(Stock_Internal_Book * a_book)
{
	Contract& instr = m_contracts->at(a_book->ticker);
	instr.bp1 = a_book->bp_array[0];
	instr.ap1 = a_book->ap_array[0];
	instr.bv1 = a_book->bv_array[0];
	instr.av1 = a_book->av_array[0];
	instr.lower_limit = a_book->lower_limit_px;
	instr.upper_limit = a_book->upper_limit_px;
	instr.pre_data_time = a_book->exch_time;
	return true;
}

bool SDPHandler::update_contracts(Futures_Internal_Book * a_book)
{
	Contract& instr = m_contracts->at(a_book->symbol);
	instr.bp1 = a_book->bp_array[0];
	instr.ap1 = a_book->ap_array[0];
	instr.bv1 = a_book->bv_array[0];
	instr.av1 = a_book->av_array[0];
	instr.lower_limit = a_book->lower_limit_px;
	instr.upper_limit = a_book->upper_limit_px;
	instr.pre_data_time = a_book->int_time;
	return true;
}

bool SDPHandler::update_contracts(Internal_Bar * a_bar)
{
	Contract& instr = m_contracts->at(a_bar->symbol);
	instr.pre_data_time = a_bar->int_time;
	instr.upper_limit = a_bar->upper_limit;
	instr.lower_limit = a_bar->lower_limit;
	return true;
}

bool SDPHandler::check_factor_v2_is_last(my_factor_v2 *quote)
{
	return quote->flag && 0x01;
}

bool SDPHandler::check_factor_v2_is_last(my_factor_double_v2 *quote)
{
	return quote->flag && 0x01;
}