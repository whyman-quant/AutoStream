#pragma once

#include <stdint.h>
#include "constants.h"

#pragma pack(push)
// 按 8 字节对齐，与内存/文件中的逐笔布局（及 Python my 包侧约定）一致。
#pragma pack(8)

// 实时逐笔成交
typedef struct stock_transaction {
    int int_time;                   // 成交时间
    int market;
    char symbol[kScrCodeLen];
    int64_t record_id;              // 成交编号
    int trade_price;                // 成交价格
    int64_t trade_volume;           // 成交数量
    int64_t trade_amount;           // 成交金额

    char order_kind[4];             // 成交类别
    char side[4];                   // 指令交易类型
    char function_code[4];          // 成交代码
    int sell_id;	                // 叫卖方委托序号
    int buy_id;	                    // 叫买方委托序号
}my_stock_transaction;

typedef struct {
	int serial;
	int mi_type;
	uint64_t local_time;
	uint64_t exchange_time;
	my_stock_transaction quote;
}my_book_stock_transaction;

#pragma pack(pop)