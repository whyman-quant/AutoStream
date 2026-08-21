#pragma once

#include <stdint.h>
#include "constants.h"

#pragma pack(push)
#pragma pack(8)

// 实时逐笔委托
typedef struct stock_order {
    int market;
    char symbol[kScrCodeLen];
    int int_time;                    // 委托时间
    int order_price;                 // 委托价格
    int64_t order_id;                // 委托编号
    int64_t order_volume;            // 委托数量
    char side[4];                    // 指令交易类型 'B','S'
    char order_kind[4];              // 委托类别
}my_stock_order;

typedef struct {
	int serial;
	int mi_type;
	uint64_t local_time;
	uint64_t exchange_time;
	my_stock_order quote;
}my_book_stock_order;

#pragma pack(pop)