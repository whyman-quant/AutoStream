#pragma once

#include <stdint.h>
#include "constants.h"

#pragma pack(push)
#pragma pack(8)

// 实时订单队列
typedef struct stock_order_queue {
    int int_time;
    int market;
    char symbol[kScrCodeLen];
    char side[4];               // 指令交易类型 'B','S'
    int price;                  // 订单价格
    int total_numbers;          // 订单数量
    int numbers;                // 明细个数
    int qty_array[50];          // 订单明细数量（整数数组，历史字段布局）
}my_stock_order_queue;

typedef struct {
	int serial;
	int mi_type;
	uint64_t local_time;
	uint64_t exchange_time;
	my_stock_order_queue quote;
}my_book_stock_order_queue;

#pragma pack(pop)