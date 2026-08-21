#pragma once

#include <stdint.h>

#pragma pack(push)
#pragma pack(8)


enum  FEED_STATUS {
    DEBUT = '0',             /*首日上市*/
    REISSUE = '1',           /*增发新股*/
    ONLINE_SET_PRICE = '2',  /*2 上网定价发行*/
    ONLINE_BID_PRICE = '3',  /*3 上网竞价发行*/
    CLOSE = 'A',             /*A 交易节休市*/
    HALT_FOR_TODAY = 'B',    /*B 整天停牌*/
    END_OF_DAY = 'C',        /*C 全天收市*/
    PAUSE_TRADE = 'D',       /*D 暂停交易*/
    START = 'E',             /*启动交易盘*/
    PRE_ENTER = 'F',         /*盘前处理*/
    HOLIDAY = 'H',           /*放假*/
    OPEN_AUCTION = 'I',      /*开市集合竞价*/
    IN_MARKET_AUCTION = 'J', /*盘中集合竞价*/
    OPEN_PRE_ORDER_BOOK = 'K', /*开市订单簿平衡前期*/
    IN_MARKET_PRE_ORDER_BOOK = 'L', /*盘中订单簿平衡前期*/
    OPEN_ORDER_BOOK = 'M',          /*开市订单簿平衡*/
    IN_MARKET_ORDER_BOOK = 'N',     /*盘中订单簿平衡*/
    TRADE = 'O',             /*连续撮合*/
    BREAK = 'P',             /*休市*/
    VOL_BREAK = 'Q',         /*波动性中断*/
    BETWEEN_TRADE = 'R',     /*交易间*/
    NO_TRADE_SUPPORT = 'S',  /*非交易服务支持*/
    FIX_PRICE_AUCTION = 'T', /*固定价格集合竞价*/
    POST_TRADE = 'U',        /*盘后处理*/
    END_TRADE = 'V',         /*结束交易*/
    PAUSE = 'W',             /*暂停*/
    SUSPEND = 'X',           /*停牌*/
    ADD = 'Y',               /*新增产品*/
    DEL = 'Z',               /*可删除的产品*/

    //eSunny foreign
    FRN_UNKNOWN = -1,
    FRN_MARKET_OPEN = 0,
    FRN_NO_DIV = 1,
    FRN_RACE_PRICE = 2,
    FRN_ON_HOLD = 3,
    FRN_MARKET_CLOSE = 4,
    FRN_PRE_MARKET_OPEN = 5,
    FRN_PRE_MARKET_CLOSE = 6,
    FRN_FAST_MARKET = 7
};

//股票数据
typedef struct {
    char wind_code[32];         //600001.SH
    char ticker[32];             //原始Code
    int action_day;             //业务发生日(自然日)
    int trading_day;            //交易日
    int exch_time;              //时间(HHMMSSmmm)
    int status;                 //股票状态(见feed_status)
    uint32_t pre_close_px;              //前收盘价 * 10000
    uint32_t open_px;                   //开盘价 * 10000
    uint32_t high_px;                   //最高价 * 10000
    uint32_t low_px;                    //最低价 * 10000
    uint32_t last_px;               //最新价 * 10000
    uint32_t ap_array[10];          //申卖价 * 10000
    uint32_t av_array[10];          //申卖量
    uint32_t bp_array[10];          //申买价 * 10000
    uint32_t bv_array[10];          //申买量
    uint32_t num_of_trades;         //成交笔数
    int64_t total_vol;              //成交总量
    int64_t total_notional;         //成交总额准确值,Turnover
    int64_t total_bid_vol;          //委托买入总量
    int64_t total_ask_vol;          //委托卖出总量
    uint32_t weighted_avg_bp;   //加权平均委买价格 * 10000
    uint32_t weighted_avg_ap;  //加权平均委卖价格 * 10000
    int IOPV;                   //IOPV净值估值  （基金） * 10000
    int yield_to_maturity;      //到期收益率    （债券） * 10000
    uint32_t upper_limit_px;            //涨停价 * 10000
    uint32_t lower_limit_px;            //跌停价 * 10000
    char prefix[4];         //证券信息前缀
    int PE1;                    //市盈率1   未使用（当前值为0）
    int PE2;                    //市盈率2   未使用（当前值为0）
    int change;                 //升跌2（对比上一笔）   未使用（当前值为0）
}my_stock_t;

typedef struct {
	int serial;
	int mi_type;
	uint64_t local_time;
	uint64_t exchange_time;
	my_stock_t quote;
}my_book_stock;

#pragma pack(pop)
