#include <stdint.h>

#ifndef _QUOTE_FORMAT_DEFINE_H
#define _QUOTE_FORMAT_DEFINE_H

#define SCR_CODE_LEN 32
typedef struct
{
    char wind_code[32];         //600001.SH
    char ticker[32];             //原始Code
    int action_day;             //业务发生日(自然日)
    int trading_day;            //交易日
    int exch_time;				//时间(HHMMSSmmm)
    int status;				    //股票状态(见feed_status)
    uint32_t pre_close_px;				//前收盘价 * 10000
    uint32_t open_px;					//开盘价 * 10000
    uint32_t high_px;					//最高价 * 10000
    uint32_t low_px;					//最低价 * 10000
    uint32_t last_px;				//最新价 * 10000
    uint32_t ap_array[10];			//申卖价 * 10000
    uint32_t av_array[10];			//申卖量
    uint32_t bp_array[10];			//申买价 * 10000
    uint32_t bv_array[10];			//申买量
    uint32_t num_of_trades;			//成交笔数
    int64_t total_vol;				//成交总量
    int64_t total_notional;		    //成交总额准确值,Turnover
    int64_t total_bid_vol;			//委托买入总量
    int64_t total_ask_vol;			//委托卖出总量
    uint32_t weighted_avg_bp;	//加权平均委买价格 * 10000
    uint32_t weighted_avg_ap;  //加权平均委卖价格 * 10000
    int IOPV;					//IOPV净值估值  （基金） * 10000
    int yield_to_maturity;		//到期收益率	（债券） * 10000
    uint32_t upper_limit_px;			//涨停价 * 10000
    uint32_t lower_limit_px;			//跌停价 * 10000
    char prefix[4];			//证券信息前缀
    int PE1;					//市盈率1	未使用（当前值为0）
    int PE2;					//市盈率2	未使用（当前值为0）
    int change;					//升跌2（对比上一笔）	未使用（当前值为0）
} Stock_Internal_Book;

typedef struct
{
	int  feed_type; /* type of the data source */
	char symbol[64];
	int16_t exchange;
	int  int_time;  /* 090059000, HourMintueSecondMilliSeconds*/
	float pre_close_px;
	float pre_settle_px;
	double pre_open_interest;
	double open_interest;
	float open_px;
	float high_px;
	float low_px;
	float avg_px;
	float last_px;
	float bp_array[5];
	float ap_array[5];
	int  bv_array[5];
	int  av_array[5];
	uint64_t total_vol;
	double total_notional;  /* Trade Notional, Turnover */
	float upper_limit_px;
	float lower_limit_px;
	float close_px;	/* Today's close price */
	float settle_px;
	/* DCE */
	int implied_bid_size[5];  /* Implied Bid/Ask Size */
	int implied_ask_size[5];
	/* Statistics Info, DCE have these from another feed */
	int total_buy_ordsize;		/* Total Open Buy Order Size */
	int total_sell_ordsize;     /* Total Open Sell Order Size */
	float weighted_buy_px;   /* Weighted Buy Order Price */
	float weighted_sell_px;  /* Weighted Sell Order Price */
} Futures_Internal_Book;


typedef struct
{
    int int_time;
    int market;
    char symbol[SCR_CODE_LEN];
    char side[4];               // 指令交易类型 'B','S'
    int price;                  // 订单价格
    int total_numbers;          // 订单数量
    int numbers;                // 明细个数
    int qty_array[50];          // 订单明细数量 （参考宏汇接口，定义成200个整数数组）
}Stock_Queue_Internal_Book;

typedef struct
{
    int market;
    char symbol[SCR_CODE_LEN];
    int int_time;                    // 委托时间
    int order_price;                 // 委托价格
    int64_t order_id;                // 委托编号
    int64_t order_volume;            // 委托数量
    char side[4];                    // 指令交易类型 'B','S'
    char order_kind[4];              // 委托类别
}Stock_Order_Internal_Book;

typedef struct
{
    int int_time;                   // 成交时间
    int market;
    char symbol[SCR_CODE_LEN];
    int64_t record_id;              // 成交编号
    int trade_price;                // 成交价格
    int64_t trade_volume;           // 成交数量
    int64_t trade_amount;           // 成交金额
    char order_kind[4];             // 成交类别
    char side[4];                   // 指令交易类型
    char function_code[4];          // 成交代码
    int sell_id;	                // 叫卖方委托序号
    int buy_id;	                    // 叫买方委托序号
}Stock_Transaction_Internal_Book;

// 实时逐笔委托
typedef struct
{
	char order_type;
	uint8_t market;
	char bsflag;
	char symbol[9];
	int int_time;                    // 委托时间
	int64_t order_index;
	int64_t order_volume;            // 委托数量
	uint64_t orderorino;
	uint64_t biz_index;
	int order_price;                 // 委托价格
	uint16_t channel;
}Stock_Order_Internal_Book_New;

// 实时逐笔成交
typedef struct
{
	uint16_t channel;
	char symbol[9];
	uint8_t market;
	int int_time;                   // 成交时间
	int trade_price;
	char bsflag;
	char trade_type;
	int64_t trade_index;
	int64_t trade_volume;           // 成交数量
	int64_t trade_amount;           // 成交金额
	int64_t sell_id;
	int64_t buy_id;
	int64_t biz_index;
}Stock_Transaction_Internal_Book_New;

/**
 * btc 行情
 */
typedef struct {
    char exchange[20];
    char symbol[32];
    char my_symbol[32];
    double last_px;
    double last_vol;
    double bid_px[5];
    double ask_px[5];
    double bid_vol[5];
    double ask_vol[5];
    char local_time[32];
} Coin_Internal_Book;

/**
 * 外汇行情
 */
typedef struct {
    char symbol[64];
	char exchange[20];
	int  int_time;      /* 090059000, HourMintueSecondMilliSeconds*/
	float open_px;
	float high_px;
	float low_px;
	float pre_close_px;
	float last_px;
    int last_size;
	float bp_array[5];
	float ap_array[5];
	int  bv_array[5];
	int  av_array[5];
	uint64_t  volume;
} Forex_Internal_Book;

typedef struct
{
	char symbol[64];
	int  int_time;
	double open;
	double close;
	double high;
	double low;
	uint64_t volume;   // Total shares/vol
	double turnover;   // Total Traded Notional
	double upper_limit;
	double lower_limit;
	double open_interest;
	int bar_index;
} Internal_Bar;


#endif
