#pragma once

#include <stdint.h>
#include <iostream>

#pragma pack(push)
#pragma pack(8)

// 实时逐笔委托 257
typedef struct stock_order_new {
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
}my_stock_order_new;


typedef struct {
	int serial;
	int mi_type;
	uint64_t local_time;
	uint64_t exchange_time;
	my_stock_order_new quote;
}my_book_stock_order_new;


typedef struct {
	uint16_t channel;
	char symbol[9];
	uint8_t market;
	int int_time;                   // 成交时间
	int trade_price;
	char bsflag;
	char trade_type;
	char order_type;
	int64_t trade_index;
	int64_t trade_volume;           // 成交数量
	int64_t trade_amount;           // 成交金额
	int64_t sell_id;
	int64_t buy_id;
	int64_t biz_index;
	int64_t order_index;
	int64_t order_volume;            // 委托数量
	uint64_t orderorino;
	int order_price;                 // 委托价格
	int mi_type_quote;
}my_stock_order_trans_new;

typedef struct {
	int serial;
	int mi_type;
	uint64_t local_time;
	uint64_t exchange_time;
	my_stock_order_trans_new quote;
}my_book_stock_order_trans_new;


#pragma pack(pop)