#pragma once
#include <stdint.h>

#pragma pack(push)
#pragma pack(1)

typedef struct {
    int date;
    int next_date;
    char market;
    char exchange[5];
    char symbol[6];
    char product[2];
    char indentifier[2];
    char main_flag[4];
    int64_t main_level;
    double  update_time;
} my_mc_t;

typedef struct {
    int date;
    int next_date;
    char exchange[5];
    char symbol[8];
    char product[4];
    int volume;
    char main_flag[6];
    int main_level;
} my_esmc_t;

#pragma pack(pop)

#pragma pack(push)
#pragma pack(8)

typedef struct {
    int book_type;
    char symbol[64];
    uint8_t exchange;
    int int_time;
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
    int bv_array[5];
    int av_array[5];
    uint64_t total_vol;
    double total_notional;
    float upper_limit_px;
    float lower_limit_px;
    float close_px;
    float settle_px;
    int implied_bid_size[5];
    int implied_ask_size[5];
    int total_buy_ordsize;
    int total_sell_ordsize;
    float weighted_buy_px;
    float weighted_sell_px;
} _my_futures;


typedef struct {
    int serial;
	int mi_type;
	int64_t local_time;
	int64_t exchange_time;
	_my_futures quote;
} my_futures_t;

#pragma pack(pop)
