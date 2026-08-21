#ifndef _CHECK_HANDLER_H_
#define _CHECK_HANDLER_H_

#include "base_define.h"
#include "utils/order_hash.h"
#include "utils/utils.h"
//#include "utils/MyHash.h"
#include "utils/MyCRCHash.h"
#include "strategy_interface.h"
#include "quote_format_define.h"
#include "utils/rdtsc.h"

#define MAX_ORDER_COUNT      (256)
#define MAX_STOCK_CNT        (8192)

typedef struct {
    long exch_time;
    long local_time;
	// HACK: (by gaowang) 使用 rdtsc 代替 struct timeval，提高性能与准确性，已咨询IT部门得到确认。
	uint64_t rdtsc_num;
    // struct timeval machine_time;
} start_time_t;

// HACK: (by gaowang) 官方版本没有这个函数，这是为了不暴露start_time_t结构体的细节而新增的函数。
// 更新机器时间戳，用于计算因子发送时间与行情时间戳的差值。
inline int update_machine_time(start_time_t* t) {
	// gettimeofday(&t->machine_time, 0);
	t->rdtsc_num = sdp_rdtsc();
	return 0;
}

typedef struct {
    char        account[64];
    int         currency;                               /* cash currency type */
    double      exch_rate;                              /* currency exchange rate */
    double      cash_asset;                             /* cash for pnl counting. */
    double      cash_available;                         /* cash for order pre-sending checking. */
} c_account_t;

class SDPHandler
{
public:
	SDPHandler(int type, int length, void * cfg); // Should be called in my_st_init
	~SDPHandler(); // Should be called in my_destroy
	int on_book(int type, int length, void * book);	// Should be called in my_on_book
	int on_response(int type, int length, void * resp);	// Should be called in my_on_response
	int on_timer(int type, int length, void * info) {
		(void)type;
		(void)length;
		(void)info;
		return 0;
	} // Should be called in my_on_timer

	// Strategy can use these function below

	/**
	* Send order info to trader.
	* When your order size is too large, we will split it into some small orders
	*
	* @param instr			Contract you want to trade
	* @param exch			Enums of exchange's name
	* @param price			Price of order
	* @param size			Size of order
	* @param side			Side of order, eg. ORDER_BUY, ORDER_SELL
	* @param sig_openclose				eg. ORDER_OPEN, ORDER_CLOSE
	* @param flag_syn_cancel			Flag of whether cancel a order with synchronization
	* @param flag_close_yesterday_pos	Flag of close yesterday orders
	* @param investor_type		eg. ORDER_SPECULATOR, ORDER_HEDGER, if you don't know its meaning, just keep default value
	* @param order_type			eg. ORDER_TYPE_LIMIT, ORDER_TYPE_MARKET, if you don't know its meaning, just keep default value
	* @param time_in_force		eg. ORDER_TIF_DAY, ORDER_TIF_FAK, if you don't know its meaning, just keep default value
	* return			0		-- Success.
	*                   else	-- Failed.
	*/
	int send_single_order(Contract *instr, EXCHANGE exch,
		double price, int size, DIRECTION side,
		OPEN_CLOSE sig_openclose = ORDER_OPEN, bool flag_syn_cancel = false, bool flag_close_yesterday_pos = false,
		INVESTOR_TYPE investor_type = ORDER_SPECULATOR, ORDER_TYPE order_type = ORDER_TYPE_LIMIT, TIME_IN_FORCE time_in_force = ORDER_TIF_DAY);

	/**
	* Smart send/cancel orders to get your target position.
	* This function will consider the pending positions and yesterday positions
	*
	* @param instr				Contract you want to trade
	* @param side				Side of order, eg. SIG_DIRECTION_BUY, SIG_DIRECTION_SELL
	* @param size				Size of order
	* @param price				Price of order
	* @param flag_syn_cancel	Flag of whether use synchronization
	* return			0		-- Success.
	*                   else	-- Failed.
	*/
	int send_dma_order(Contract *instr, DIRECTION side,
		int size, double price, bool flag_syn_cancel = true);

	/**
	* Using Twap/Vwap algorithm to sent order with target size at certain times.
	*
	* @param instr				Contract you want to trade
	* @param side				Side of order, eg. SIG_DIRECTION_BUY, SIG_DIRECTION_SELL
	* @param size				Size of order
	* @param price				Price of order
	* @param start_time			Start time to trade
	* @param end_time			End time to trade
	* return			0		-- Success.
	*                   else	-- Failed.
	*/
	int send_twap_order(Contract *instr, int size, DIRECTION side, double price,
		int start_time, int end_time);
	int send_vwap_order(Contract *instr, int size, DIRECTION side, double price,
		int start_time, int end_time);

	/**
     * Send factor or position to quote hub
     *
     * @param st_id 			Strategy id
     * @param ticker            Symbol name
     * @param trading_date      Trading date
     * @param exch_time         Exchange time of 90000000 (ms)
     * @param local_time        Receive time of 90000000000 (us)
     * @param values            List of factor
     * @param volume            Shared postion size
     * @param account           Virtual account name
     */
    int send_factor_columns(std::vector<std::string> &columns);

	int send_factor(const char *ticker, start_time_t *t, std::vector<double> &factors);

	int send_position(const char *ticker, start_time_t *t, long volume, const char *account);

	/**
	 * Send factor v2 to quote hub
	 *
	 * @param st_id 			Strategy id
	 * @param ticker            Symbol name
	 * @param trading_date      Trading date
	 * @param exch_time         Exchange time of 90000000 (ms)
	 * @param local_time        Receive time of 90000000000 (us)
	 * @param values            List of factor
	 * @param volume            Shared postion size
	 * @param account           Virtual account name
	 */
	int send_factor_columns_v2(std::vector<std::string> &columns, int row_count = 1);

	int send_factor_v2(const char *ticker, start_time_t *t, std::vector<float> &factors, bool is_last = false);

	int send_factor_v2(const char *ticker, start_time_t *t, std::vector<double> &factors, bool is_last = false);

	int send_factor_bulk(const char *ticker, start_time_t *t, std::vector<float> &factors, bool is_last = false);

	int send_factor_bulk(const char *ticker, start_time_t *t, std::vector<double> &factors, bool is_last = false);

	int send_factor_matrix(std::vector<std::string> &tickers, start_time_t *t, std::vector<std::vector<float>> &factors_matrix,
	    long overwrite_elapsed_us = -1);

	int send_factor_matrix(std::vector<std::string> &tickers, start_time_t *t, std::vector<std::vector<double>> &factors_matrix,
	    long overwrite_elapsed_us = -1);

	// HACK: (by gaowang) 官方版本没有这个函数，这是为了减少拷贝、减少条件判断而新增的函数。
	// 优化版本：直接接受类型化的因子数据结构指针，消除运行时分支判断
	// 模板函数，在编译期根据 FactorStruct 的实际类型进行特化
	template<typename FactorStruct>
	int send_factor_bytes_sync(start_time_t *t, int type, int length, FactorStruct *factor_data) {
		uint64_t now_rdtsc_num = sdp_rdtsc();
		factor_data->exch_time = t->exch_time;
		factor_data->local_time = t->local_time;
		// ns单位:
		// factor_data->elapsed_time = static_cast<long>(1000 * (now_rdtsc_num - t->rdtsc_num) * m_rdtsc_scaler);
		// us单位: 因为保存的是long类型，所以会牺牲小数位数代表的ns精度
		factor_data->elapsed_time = static_cast<long>((now_rdtsc_num - t->rdtsc_num) * m_rdtsc_scaler);
		real_send_info_func(type, length, (void*)factor_data);
		return 0;
	}

	// HACK: (by gaowang) 官方版本没有这个函数，这是为了减少拷贝、减少条件判断而新增的函数。
	int send_factor_matrix_bytes_sync(start_time_t* t, int type, int item_size, int rows, char* factor_matrix) {
		if (rows == 0) {
			return 0;
		}
		uint64_t now_rdtsc_num = sdp_rdtsc();
		// ns单位:
		// long elapsed_time = static_cast<long>(1000 * (now_rdtsc_num - t->rdtsc_num) * m_rdtsc_scaler);
		// us单位: 因为保存的是long类型，所以会牺牲小数位数代表的ns精度
		long elapsed_time = static_cast<long>((now_rdtsc_num - t->rdtsc_num) * m_rdtsc_scaler);
		if (type == S_PUBLISH_FACTOR_BULK_DOUBLE_V2) {
			for (int i = 0; i < rows - 1; ++i) {
				my_factor_double_v2* factor_data = (my_factor_double_v2*)(factor_matrix + i * item_size);
				factor_data->exch_time = t->exch_time;
				factor_data->local_time = t->local_time;
				factor_data->elapsed_time = elapsed_time;
				factor_data->flag = 0;
			}
			my_factor_double_v2* factor_data = (my_factor_double_v2*)(factor_matrix + (rows - 1) * item_size);
			factor_data->exch_time = t->exch_time;
			factor_data->local_time = t->local_time;
			factor_data->elapsed_time = elapsed_time;
			factor_data->flag = 0x01;
		} else if (type == S_PUBLISH_FACTOR_BULK_V2) {
			for (int i = 0; i < rows - 1; ++i) {
				my_factor_v2* factor_data = (my_factor_v2*)(factor_matrix + i * item_size);
				factor_data->exch_time = t->exch_time;
				factor_data->local_time = t->local_time;
				factor_data->elapsed_time = elapsed_time;
				factor_data->flag = 0;
			}
			my_factor_v2* factor_data = (my_factor_v2*)(factor_matrix + (rows - 1) * item_size);
			factor_data->exch_time = t->exch_time;
			factor_data->local_time = t->local_time;
			factor_data->elapsed_time = elapsed_time;
			factor_data->flag = 0x01;
		}
		real_send_info_func(type, item_size * rows, (void*)factor_matrix);
		return 0;
	}


	/**
	* Cancel pending order functions.
	* These functions will cancel order with different situation
	*
	* @param order			A pointer to Order
	* @param instr			Contract you want to trade
	* @param side			Side of order, eg. SIG_DIRECTION_BUY, SIG_DIRECTION_SELL
	* @param sig_openclose	eg. SIG_ACTION_OPEN, SIG_ACTION_CLOSE
	* @param price			Price of order
	* return				0		-- Success.
	*						else	-- Failed.
	*/
	int cancel_single_order(Order* order);
	int cancel_orders_by_side(Contract *instr, DIRECTION side);
	int cancel_orders_by_side(Contract *instr, DIRECTION side, OPEN_CLOSE flag);
	int cancel_orders_with_dif_px(Contract *instr, DIRECTION side, double price);
	int cancel_all_orders();
	int cancel_all_orders(Contract *instr);

	/**
	 * Manage factor and history quote information
	 * void * in vector is a point to corresponding structure of data
	 * @param symbol        Code of stock
	 * @param quote_type    check the definition on wiki
	 */
	std::vector<void *> *get_tick_by_type_and_symbol(const char *symbol, int quote_type);

	Contract * find_contract(char* symbol);
	int get_pos_by_side(Contract *a_instr, DIRECTION side);
	int get_pending_size_by_side(Contract* a_contr, DIRECTION a_side);

	double get_account_cash(char* account);

	start_time_t get_quote_time(int type, void *quote);

	bool check_factor_v2_is_last(my_factor_v2 *quote);
	bool check_factor_v2_is_last(my_factor_double_v2 *quote);

	/**
	* @brief 解析二维截面因子属性
	* @param
	*       type: 数据类型
	* 		quote: 二维因子数据
	*       bulk_factors： 解析后数据
	* @return
	*/
	void get_bulk_data(int type,void *quote,bulk_factor_datas_t& bulk_factors);

	/**
	 * @brief 获取策略ID
	 * @return 策略ID
	 */
	// HACK: (by gaowang) 官方版本没有这个函数，该函数是新增的，方便外部调用，获取策略ID
	int get_st_id() const { return m_config->st_id; }

	/**
	 * @brief 获取交易日期
	 * @return 交易日期
	 */
	// HACK: (by gaowang) 官方版本没有这个函数，该函数是新增的，方便外部调用，获取交易日期
	int get_trading_date() const { return m_config->trading_date; }

private:
    int m_send_info_func(int type, int length, void *data);
	void update_yes_pos(Contract *instr, DIRECTION side, int size);
	bool process_order_resp(st_response_t* resp);
	void update_account_cash(Order* ord_resp);
	void update_pending_status(Order* a_order);
	bool update_contracts(Stock_Internal_Book* a_book);
	bool update_contracts(Futures_Internal_Book* a_book);
	bool update_contracts(Internal_Bar* a_bar);

public:
	OrderHash *m_orders = NULL;
	MyCRCHash<c_account_t> m_accounts;
	MyCRCHash<Contract> *m_contracts = NULL;
	send_data m_send_resp_func = NULL;
	send_data m_send_order_func = NULL;
//	send_data m_send_info_func = NULL;
	send_data real_send_info_func = NULL;

	/* Record orders' ids and sizes in send_single_order */
	uint64_t m_cur_ord_id_arr[MAX_ORDER_COUNT];
	int m_cur_ord_size_arr[MAX_ORDER_COUNT];
private:
	int m_strat_id = 0;
	int run_mode = 1; // 0 is live trading
	st_config_t *m_config;
	bool m_is_use_fee = true;

	// atomic_flag 是专门用于原子自旋锁的轻量级标志类型，通常只能设置（test_and_set）和清除（clear）
	std::atomic_flag slock = ATOMIC_FLAG_INIT;

	bool m_use_lock_pos = false;
	bool is_exit_mode = false;
	bool exit_mode_orders_sent = false;
	int m_new_order_times = 0;
	int m_cancelled_times = 0;
	int m_total_order_size = 0;		// Accumulated order size.
	int m_total_cancel_size = 0;	// Accumulated order cancel size.

    char *m_buf;
	char *m_factor_v2_buff;
	char *m_bulk_buff;
	char *m_factor_matrix_buff;
	int m_factor_v2_count;
	int m_factor_bulk_cursor;
	int m_factor_matrix_cursor;

	double m_cpu_mhz;
	double m_rdtsc_scaler;

    /* quote_type -> quote point list of current session */
    std::unordered_map<int, std::vector<void *> > m_quote_hist[MAX_STOCK_CNT];
};

#endif
