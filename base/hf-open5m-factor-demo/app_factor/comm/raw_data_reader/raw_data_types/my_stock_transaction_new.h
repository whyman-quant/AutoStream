#pragma once

#include <stdint.h>

#pragma pack(push)
#pragma pack(8)

// 实时逐笔成交 256
typedef struct stock_transaction_new {
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
}my_stock_transaction_new;


typedef struct {
	int serial;
	int mi_type;
	uint64_t local_time;
	uint64_t exchange_time;
	my_stock_transaction_new quote;
}my_book_stock_transaction_new;

#pragma pack(pop)