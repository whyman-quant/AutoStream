#ifndef _LF_STRATEGY_API_H_
#define _LF_STRATEGY_API_H_

// C / C++ 使用说明（库边界）
// ------------------------
// - 源码级：本头依赖 C++ 标准库（<string>、<vector> 等），须由 C++ 编译单元 #include，不能作为纯 C 源文件直接编译。
// - 链接级：文末 extern "C" 中的 my_st_init_v3 / my_on_book_v3 等导出符号使用 C 链接名；纯 C 宿主可自持等价
//   C 声明（仅 POD 与指针，不依赖本头中的 C++ 类型）并链接同一实现，实现与 C++ 策略互操作。
// - 告警相关：正文中的 struct/typedef 与原先一致，未改字段与布局；仅增补 GCC/Clang 下对本头的 -Wpedantic 屏蔽，
//   以免柔性数组成员（如 value[0]、meta_data[0]、buf[0]）在开启 -pedantic 时刷屏告警。非 GNU 编译器不受影响。
// - HACK: (by gaowang) 在 #pragma pack(8) 之后 push、在头文件末尾（#endif 前）pop，整头生效。

#include <string>
#include <vector>

#define SYMBOL_MAX (4096)       /* 模型支持的合约最大个数 */
#define ARGUMENT_SIZE (4096)    /* 作为参数可扩展最大长度 */
#define PATH_LEN (2048)         /* 模型so/ev文件存放绝对路径最大长度 */
#define STRAT_NAME_LEN (256)    /* 策略名最大长度 */
#define SYMBOL_LEN (64)         /* 合约名最大长度 */
#define ACCOUNT_MAX (64)        /* 模型支持的最大账户个数 */
#define ACCOUNT_LEN (64)        /* 账户名最大长度 */
#define VERSION_LEN (32)        /* 版本长度 */
#define ERR_MSG_LEN (512)       /* 订单错误详情长度 */
#define API_ORDER_CODE_LEN (64) /* 成交编号长度 */

#ifndef _WIN32
#define EXPORT_SYMBOL __attribute((visibility("default")))
#else
#define EXPORT_SYMBOL __declspec(dllexport)
#endif

#pragma pack(push)
#pragma pack(8)

// HACK: (by gaowang) 与文件首说明一致：整头忽略 -Wpedantic。
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

typedef enum _CHECK_ERROR {
	/* error_no would be 0 when none check error found */
	ERROR_OPEN_POS_IS_NOT_ENOUGH = -1,
	ERROR_SELF_MATCHING = -2,
	ERROR_REACH_MAX_ACCUMULATE_OPEN_VOL = -3,
	ERROR_CANCEL_ORDER_FAIL = -4,
	ERROR_CANCEL_ORDER_REACH_LIMIT = -5,
	ERROR_CASH_IS_NOT_ENOUGH = -6,
	ERROR_PRE_POS_IS_NOT_ENOUGH_TO_CLOSE = -7,
	ERROR_TOD_POS_IS_NOT_ENOUGH_TO_CLOSE = -8,
	ERROR_OPEN_ORDER_EXCCEEDED_MAX_POS_RATIO = -9,
	ERROR_TRADE_PAUSE = -10
} CHECK_ERROR;

typedef enum _SMART_EXECUTION_ERROR {
	/* error_no would be 0 when none smart execution error found */
	ERROR_SE_FAIL_GENERAL = -101,
	/* 可能是没有回报或者说订单格式错误，比如 buy sell invalid 或 invalid volume */
	ERROR_SE_INPUT_CHECK_FAILED = -102
} SMART_EXECUTION_ERROR;

typedef enum _TUNNEL_ERROR { ERROR_TUNNEL_DISCONNECT = -201, ERROR_SEND_ORDER_FAILED = -202 } TUNNEL_ERROR;

typedef enum _DAY_NIGHT {
	DAY = 0,
	NIGHT = 1,
} DAY_NIGHT;

typedef enum _EXCHANGE {
	SZSE = '0',   /* SHENZHEN STOCK EXCHANGE */
	SSE = '1',    /* SHANGHAI STOCK EXCHANGE*/
	HKEX = '2',   /* FIXME make this consistent with trader */
	SZCB = '3',   /* shenzhen CB */
	SHCB = '4',   /* shanghai CB */
	BSE = '7',    /* Beijing EXCHANGE */
	SHFE = 'A',   // XSGE = 'A',                                        /* SHANGHAI FUTURES EXCHANGE */
	CFFEX = 'G',  // CCFX = 'G',                                       /* CHINA FINANCIAL FUTURES EXCHANGE */
	DCE = 'B',    // XDCE = 'B',                                        /* DALIAN COMMODITY EXCHANGE */
	CZCE = 'C',   // XZCE = 'C',                                       /* ZHENGZHOU COMMODITY EXCHANGE */
	SGX = 'S',    /* FIXME make this consistent with trader */
	SGE = 'D',    /* shanghai gold exchange */
	CBOT = 'F',   /* make this consistent with trader, eSunny Foreign */
	CME = 'M',
	LME = 'L',
	COMEX = 'O',
	NYMEX = 'N', /* TODO: Change to NYMX */
	ICEE = 'E',
	ICEU = 'P',
	IPE = '9',
	COINALL = '6',
	// HITBTC = '7',
	FCOIN = '8',
	GEMINI = 'K',
	ZB = 'Q',
	BX = 'H',
	COINW = 'I',
	GDAX = 'J',
	BITMEX = 'T',
	BITFINEX = 'U',
	BITSTAMP = 'V',
	KRAKEN = 'W',
	BINANCE = 'R',
	OKEX = 'Y',
	HUOBIEX = 'Z',
	BLANK_EXCH = '\0',
	UNDEFINED_EXCH = 'u'
} EXCHANGE;

typedef enum _MY_CURRENCY {
	CNY = 0,  /* Chinese Yuan */
	USD = 1,  /* US Dollar */
	HKD = 2,  /* HongKong Dollar */
	EUR = 3,  /* Euro */
	JPY = 4,  /* Japanese Yen */
	GBP = 5,  /* Puond Sterling */
	AUD = 6,  /* Australian Dollar */
	CAD = 7,  /* Canadian Dollar */
	SEK = 8,  /* Swedish Krona */
	NZD = 9,  /* New Zealand Dollar */
	MXN = 10, /* Mexican Peso */
	SGD = 11, /* Singapore Dollar */
	NOK = 12, /* Norwegian Krone */
	KRW = 13, /* South Korean Won */
	TRY = 14, /* Turkish Lira */
	RUB = 15, /* Russian Ruble */
	INR = 16, /* Indian Rupee */
	BRL = 17, /* Brazilian Real */
	ZAR = 18, /* South African Rand */
	CNH = 19, /* Offshore Chinese Yuan */
} MY_CURRENCY;

typedef enum _COMMAND_RET /* send_info, pass_resp, proc_order return */
{ SEND_CMD_FAILED = -1,
	SEND_CMD_SUCCESS = 0,
} COMMAND_RET;

typedef enum _DIRECTION {
	UNDEFINED = -1,
	ORDER_BUY = 0,
	ORDER_SELL = 1,
	RZ_BUY = 2,
	RQ_SELL = 3,
	STOCK_RETURN = 4,
	BUY_RETURN = 5,
} DIRECTION;

typedef enum _OPEN_CLOSE {
	ORDER_OPEN = 0,
	ORDER_CLOSE = 1,
	ORDER_CLOSE_TOD = 2, /* For SHFE, we have to tell tunnel close today. not used now */
	ORDER_CLOSE_YES = 3, /* For SHFE, we have to tell tunnel close yesterday. */
	UNDEFINED_OPEN_CLOSE = 4,
} OPEN_CLOSE;

typedef enum _INVESTOR_TYPE {
	ORDER_SPECULATOR = 0,
	ORDER_HEDGER = 1,
	ORDER_ARBITRAGEURS = 2,
} INVESTOR_TYPE;

typedef enum _ORDER_TYPE {
	ORDER_TYPE_LIMIT = 0,  /* limit order */
	ORDER_TYPE_MARKET = 1, /* market order */
	ORDER_TYPE_STOP = 2,
} ORDER_TYPE;

typedef enum _TIME_IN_FORCE {
	ORDER_TIF_DAY = 0, /* Nomal */
	ORDER_TIF_FAK = 1, /* Fill And Kill */
	ORDER_TIF_IOC = 2, /* Immediate Or Cancel */
	ORDER_TIF_FOK = 3, /* Fill Or Kill */
	ORDER_TIF_GTD = 4, /* Good Till Day */
	ORDER_TIF_GTC = 5, /* Good Till Cancel */
} TIME_IN_FORCE;

typedef enum _ORDER_STATUS {
	SIG_STATUS_INIT = -1, /* new order */
	SIG_STATUS_SUCCEED,
	SIG_STATUS_ENTRUSTED, /* Acknowledge by exchange */
	SIG_STATUS_PARTED,    /* Partial Fill  */
	SIG_STATUS_CANCELED,
	SIG_STATUS_REJECTED, /* Rejected by exchange or broker */
	SIG_STATUS_CANCEL_REJECTED,
	SIG_STATUS_INTRREJECTED, /* Rejected by trading system */
	UNDEFINED_STATUS
} ORDER_STATUS;

typedef enum _STRATEGY_OPT_TYPE {
	S_PLACE_ORDER_DEFAULT = 100,           /* info = order_t */
	S_CANCEL_ORDER_DEFAULT = 101,          /* info = order_t */
	S_STRATEGY_DEBUG_LOG = 102,            /* char * */
	S_STRATEGY_PASS_RSP = 103,             /* info = st_response_t */
	S_PLACE_ORDER_DMA = 104,               /* direct market access info = special_order_t */
	S_PLACE_ORDER_VWAP = 105,              /* volume weighted average price info = special_order_t  */
	S_PLACE_ORDER_TWAP = 106,              /* time weighted average price info = special_order_t */
	S_MANUAL_ORDER_HEDGE_RSP = 107,        /* info = void*, used for manual order rsp */
	S_STRATEGY_PARENT_ORDER_INFO = 108,    /* t/vwap strategy parent order information */
	S_STRATEGY_CHILD_ORDER_INFO = 109,     /* t/vwap strategy child order information */
	S_STRATEGY_ALERT_INFO = 110,           /* alert info send by strategy */
	S_MODULE_DEBUG_LOG = 111,              /* module log type */
	S_SCREEN_OUTPUT_INFO = 112,            /* info sent to redis */
	S_CHECK_ERROR_INFO = 113,              /* check module error info */
	S_PLACE_MANUAL_ORDER_TWAP = 114,       /* send TWAP manual order, info = special_manual_order_t */
	S_CANCEL_MANUAL_ORDER_TWAP = 115,      /* cancel TWAP manual order, info = special_manual_order_t */
	S_TWAP_COMMAND = 116,                  /* enable/disable TWAP execution, info = manual_order_cmd_t */
	S_TWAP_MANUAL_ORDER_INFO = 117,        /* twap manual order information, info = manual_order_info_resp_t */
	S_TWAP_COMMAND_RESP = 118,             /* twap manual order command response, info = manual_order_cmd_resp_t*/
	S_PUBLISH_FACTOR = 119,                /* generator the factor data message */
	S_PUBLISH_POSITION = 120,              /* position to rent out */
	S_PUBLISH_FACTOR_COLUMNS = 121,        /* column header of factor */
	S_PUBLISH_MERGE_ORDER = 122,           /* send merge order in send_info */
	S_PUBLISH_MERGE_RESPONSE = 123,        /* send merge position in send_info */
	S_STRATEGY_TUNNEL_LOG = 124,           /* valid on live mode */
	S_STRATEGY_POSITION = 125,             /* send normal position */
	S_HEDGE_STOCK_VALUE = 126,             /* hedge value from sm module to hedge side */
	S_PUBLISH_ST_CTL = 127,                /* strategy control message to lower layer */
	S_RAW_PLACE_ORDER = 128,               /* pass order directly */
	S_RAW_CANCEL_ORDER = 129,              /* pass cancel directly */
	S_PUBLISH_NORMAL_RESPONSE = 130,       /* send normal order response */
	S_REPORT_INT_INFO = 131,               /* strategy report internal information */
	S_PUBLISH_MERGE_ORDER_V2 = 132,        /* merge order v2 */
	S_PLACE_ACC_ORDER_VWAP = 133,          /* vwap order carried account special_order_v3_t */
	S_PLACE_ACC_ORDER_DEFAULT = 134,       /* extended default order order_v3_t */
	S_CANCEL_ACC_ORDER_DEFAULT = 135,      /* use struct order_v3_t */
	S_PUBLISH_FACTOR_V2_COLUMNS = 136,     /* column header of factor v2 */
	S_PUBLISH_FACTOR_V2 = 137,             /* generator the factor v2 data message */
	S_FLUSH_MODULE_LOG = 138,              /* strategy flush log in real time */
	S_PUBLISH_FACTOR_DOUBLE_V2 = 139,      /* generator the factor double v2 data message */
	S_PUBLISH_FACTOR_BULK_V2 = 140,        /* generator the factor bulk data message */
	S_PUBLISH_FACTOR_BULK_DOUBLE_V2 = 141, /* generator the factor double bulk v2 data message */
	S_PLACE_ORDER_SBL = 142,               /*场外约券请求*/
	S_PLACE_IMSSBL_QRY = 143,              /*场外券源查询请求*/
} STRATEGY_OPT_TYPE;

typedef enum _CONFIG_TYPE {
	DEFAULT_CONFIG,              /* st_config_t */
	SIMULATION_CONFIG = 1,       /* Simulation Strategy Config Type, st_config_t */
	SELL_STOCK_SHORT_CONFIG = 2, /* Allow stock short when have no position */
} CONFIG_TYPE;

typedef enum _FEED_DATA_TYPE {
	DEFAULT_FUTURE_QUOTE = 0,      /* info = future quote data */
	DEFAULT_STOCK_QUOTE = 1,       /* info = stock quote data */
	MANUAL_ORDER = 2,              /* info = manual order data */
	DEFAULT_BAR_QUOTE = 3,         /* info = my_bar_t quote */
	DEFAULT_COIN = 4,              /* info = my_coin_t quote */
	DEFAULT_FOREX = 5,             /* info = my_forex quote */
	HEDGE_INFO = 6,                /* info = my_hedge_info */
	DEFAULT_STOCK_ORDER_QUEUE = 7, /* info = my_stock_order_queue */
	DEFAULT_STOCK_ORDER = 8,       /* info = my_stock_order */
	DEFAULT_STOCK_TRANSACTION = 9, /* info = my_stock_transaction */
	CHECK_INFO = 10,               /* info = check_info */
	MANUAL_PLACE_ORDER_TWAP = 11,  /* info = twap_place_order_t */
	MANUAL_CANCEL_ORDER_TWAP = 12, /* info = twap_cancel_order_t */
	MANUAL_CTRL_TWAP = 13,         /* info = twap_ctrl_t */
	SHARE_FACTOR = 14,             /* info = my_factor_t */
	SHARE_POSITION = 15,           /* info = my_share_position_t */
	MERGE_ORDER = 16,              /* info = merge_order_t*/
	MERGE_RESPONSE = 17,           /* info = merge_response_t*/
	PLACE_ORDER = 18,              /* info = order_t*/
	CANCEL_ORDER = 19,             /* info = order_t*/
	MERGE_POSITION = 20,
	/* info = merge_position_t*/ /* info = my_share_position_t */
	/**
	 *  通用接口，数据格式为 json，web 端下发到策略的命令都走这个 TYPE (web -> strategy)
	 *  目前已经协定的控制命令有:
	 *  1. {"adj_available_cash": -10000} 平台下发资金调整，这个是增量动态调整
	 **/
	ST_LIVE_MSG = 21, /* info = buffer pointer */
	/**
	 * SMART_EXECUTION -> 对冲端策略，仅用于实盘调整对冲
	 **/
	HEDGE_STOCK_VALUE = 22, /* info = hedge_stockvalue_t */
	/**
	 *  通用接口，数据格式为 json，策略端下发到下层模块的控制走这个 TYPE 处理 (strategy -> SE/CHECK/NETTING)
	 *  策略端操作端对应使用 TYPE: S_PUBLISH_ST_CTL，目前已经协定的控制命令：
	 *  1. {"st_id": 100, "cmd": "finish_vwap"} 策略通知轧差/SE模块，一组 VWAP 订单已发送完毕可检查其风险值是否合法
	 **/
	ST_CTR_MSG = 23,              /* info = buffer pointer */
	MERGE_ORDER_V2 = 24,          /* only used by trading system */
	SHARE_IRREGULAR_FACTOR = 25,  /* for yihui */
	SHARE_FACTOR_V2 = 26,         /* info = my_factor_v2 */
	STOCK_ORDER_NEW = 27,         /* info = my_stock_order new*/
	STOCK_TRANSACTION_NEW = 28,   /* info = stock transaction new */
	SPECIAL_ORDER = 29,           /* info = special_order_v3_t */
	TWO_CENTER_CASH_CHANGED = 30, /* 一户两地模式, 单市场资金成交, 累加值 */
	TWO_CENTER_CASH_TRANS = 31,   /* 一户两地模式, 单市场资金划拨, 单次 */
	SHARE_FACTOR_DOUBLE_V2 = 32,  /* info = my_factor_double_v2 */
	STRATEGY_TRADE_CTRL = 33,     /* 策略交易控制(轧差) */
	SHARE_FACTOR_BULK = 34,
	SHARE_FACTOR_DOUBLE_BULK = 35,
	ORDER_RESPONSE = 36,          /*场外约券应答*/
	QRY_RESPONSE = 37,            /*场外券源查询应答*/
	STOCK_ORDER_TRANSACTION = 39, /* data is a combination of STOCK_ORDER_NEW and STOCK_TRANSACTION_NEW */
	DEFAULT_STOCK_L1_QUOTE = 79,
	REAL_ORDER = 99, /* real order match data */
} FEED_DATA_TYPE;

typedef enum _STOCK_ORDER_TRANS_TYPE { STOCK_TRANSACTION = 56, STOCK_ORDER = 57 } STOCK_ORDER_TRANS_TYPE;

typedef enum _RESPONSE_TYPE {
	STRATEGY_OWN_RSP,   /* info = st_response_t */
	STRATEGY_OTHER_RSP, /* info = st_response_t */
} RESPONSE_TYPE;

typedef enum _TIMER_TYPE {
	DEFAULT_TIMER, /* info = NULL */
} TIMER_TYPE;

typedef struct {
	char exch;               /* exchange enumeration value. */
	char symbol[SYMBOL_LEN]; /* symbol name */
	int volume;              /* order volume */
	double price;            /* order price */
	int direction;           /* see @enum direction */
	int open_close;          /* see @enum open_close */
	int investor_type;       /* see @enum investor_type */
	int order_type;          /* see @enum order_type*/
	int time_in_force;       /* see @enum time_in_force */
	long long st_id;
	unsigned long long order_id;   /* Order id filled by trading system. */
	unsigned long long org_ord_id; /* Original order_id for order cancelling. */
} order_t;

/* 为路由发单，v3 订单接口增加了账户信息 */
typedef struct {
	char exch;                 /* exchange enumeration value. */
	char symbol[SYMBOL_LEN];   /* symbol name */
	char account[ACCOUNT_LEN]; /* account name */
	int volume;                /* order volume */
	double price;              /* order price */
	int direction;             /* see @enum direction */
	int open_close;            /* see @enum open_close */
	int investor_type;         /* see @enum investor_type */
	int order_type;            /* see @enum order_type*/
	int time_in_force;         /* see @enum time_in_force */
	long long st_id;
	unsigned long long order_id;   /* Order id filled by trading system. */
	unsigned long long org_ord_id; /* Original order_id for order cancelling. */
} order_v3_t;

typedef struct {
	char* msg; /* report web nothing if msg == NULL */
} st_report_msg_t;

typedef struct {
	char exch;               /* exchange enumeration value. */
	char symbol[SYMBOL_LEN]; /* symbol name */
	int direction;           /* see @enum direction */
	int volume;              /* target order volume */
	double price;            /* order price */
	int start_time;          /* algorithm start time, TWAP/VWAP use only */
	int end_time;            /* algorithm end time, TWAP/VWAP use only */
	int status;              /* return 0 if success */
	int sync_cancel;         /* receive cancel resp and then send order flag,
	                            DMA use only, 1-yes, 0-no */
} special_order_t;

/* vwap, twap, dma 订单也增加了 account 信息*/
typedef struct {
	char exch;                 /* exchange enumeration value. */
	char symbol[SYMBOL_LEN];   /* symbol name */
	char account[ACCOUNT_LEN]; /* used account */
	int direction;             /* see @enum direction */
	int volume;                /* target order volume */
	double price;              /* order price */
	int start_time;            /* algorithm start time, TWAP/VWAP use only */
	int end_time;              /* algorithm end time, TWAP/VWAP use only */
	int status;                /* return 0 if success */
	int sync_cancel;           /* receive cancel resp and then send order flag,
	                              DMA use only, 1-yes, 0-no */
} special_order_v3_t;

typedef struct {
	char exch;                     /* exchange enumeration value. */
	char symbol[SYMBOL_LEN];       /* symbol name */
	int direction;                 /* see @enum direction */
	int volume;                    /* target order volume */
	double price;                  /* order price */
	int start_time;                /* algorithm start time, TWAP/VWAP use only */
	int end_time;                  /* algorithm end time, TWAP/VWAP use only */
	int status;                    /* return 0 if success */
	int sync_cancel;               /* receive cancel resp and then send order flag, DMA use only, 1-yes, 0-no */
	unsigned long parent_order_id; /* parent order id */
} special_manual_order_t;

/* se 到轧差的订单类型 */
typedef struct {
	long long v_id;
	unsigned long long parent_order_id;
	char exch;       /* exchange enumeration value. */
	char symbol[64]; /* symbol name */
	int direction;   /* see @enum direction */
	int volume;      /* target order volume */
	double price;    /* order price */
	int start_time;  /* algorithm start time, TWAP/VWAP use only */
	int end_time;    /* algorithm end time, TWAP/VWAP use only */
	int status;      /* return 0 if success */
	int sync_cancel; /* receive cancel resp and then send order flag, DMA use only, 1-yes, 0-no */
	double available_cash;
} merge_order_t;

/* 轧差到 me 的回报格式 */
typedef struct {
	long long v_id;
	unsigned long long order_id;
	char symbol[SYMBOL_LEN];
	char account[ACCOUNT_LEN];
	int msg_type;
	unsigned short direction;  /* see @enum direction */
	unsigned short open_close; /* see @enum open_close */
	double exe_price;
	int exe_volume;
	double order_price;
	int order_volume;
	int remain_volume;
	int order_status; /* order_status */
	int error_no;
	// new field
	char trade_code[64];
	long exchange_type;
	char trade_time[32];
	unsigned long long parent_order_id;
} merge_response_t;

/* 用于告知轧差策略的持仓情况 */
typedef struct {
	long long v_id;
	char symbol[64];   /* symbol name */
	int yest_long_pos; /* yesterday long pos */
} merge_position_t;

typedef struct {
	long long v_id;
	int flag; /* notify tunnel to upload trade log. 1-yes, 0-no */
} tradelog_cfg_t;

/* 废弃的指令 */
typedef struct {
	int id;
	long seq;
	int algorithm;
	unsigned long parent_order_id;
	int status;
} manual_order_info_t;

/* 废弃的指令 */
typedef struct {
	long seq;    /* operation seq */
	int command; /* 0 for TWAP off, 1 for TWAP on */
} manual_order_cmd_t;

/* 交易系统上报错误格式 */
typedef struct {
	order_t order;             /* Order information */
	int err_code;              /* check error type */
	char err_msg[ERR_MSG_LEN]; /* check error message */
} check_err_msg_t;

/* 交易系统上报数据头 */
typedef struct {
	unsigned int type;
	unsigned int size;
	unsigned long clock;
	unsigned long ms;
	char for_future_use[32];
} msg_head_t;

/* 交易系统指令 */
typedef struct {
	msg_head_t head;
	check_err_msg_t data;
} check_order_rsp_t;

/* 交易系统指令 */
typedef struct {
	msg_head_t head;
	manual_order_info_t data;
} manual_order_info_resp_t;

/* 交易系统指令 */
typedef struct {
	msg_head_t head;
	manual_order_cmd_t data;
} manual_order_cmd_resp_t;

#define FACTOR_CNT_MAX (300)

/* S_PUBLISH_FACTOR */
typedef struct {
	long fid; /* factor set id */
	char ticker[32];
	long exch_time;                /* exchange time of tick */
	long local_time;               /* local time of tick */
	long elapsed_time;             /* elapsed time between start and finish of the factor calculation (us) */
	int date;                      /* trading date*/
	int count;                     /* real factor count, < FACTOR_CNT_MAX */
	double value[FACTOR_CNT_MAX];  // TODO: update next version -> float
} my_factor_t;

// __extension__：标明随后 typedef 使用 GCC/Clang 认可的柔性数组等扩展，抑制 -pedantic 对 value[0] 的告警。
__extension__ typedef struct {
	long flag; /* flags identify factor type eg. IS_LAST macro return if the factor is last one */
	long fid;
	char ticker[16];
	long exch_time;    /* exchange time of tick */
	long local_time;   /* local time of tick */
	long elapsed_time; /* elapsed time between start and finish of the factor calculation (us) */
	int date;          /* trading date*/
	int count;         /* real factor count */
	float value[0];    /* a list of float values */
} my_factor_v2;

__extension__ typedef struct {
	long flag; /* flags identify factor type eg. IS_LAST macro return if the factor is last one */
	long fid;
	char ticker[16];
	long exch_time;    /* exchange time of tick */
	long local_time;   /* local time of tick */
	long elapsed_time; /* elapsed time between start and finish of the factor calculation (us) */
	int date;          /* trading date*/
	int count;         /* real factor count */
	double value[0];   /* a list of double values */
} my_factor_double_v2;

typedef struct {
	int row_count;
	int column_count;
	long bulk_ptr;
} bulk_data;

/* S_PUBLISH_POSITION */
typedef struct {
	long fid;        /* factor set id */
	long exch_time;  /* int time like 93023000 */
	long local_time; /* int time like 93023000000 */
	int date;        /* 20190404 */
	char ticker[32]; /* 000001 */
	long volume;     /* 2000 */
	char account[32];
	char reserve[64];
} my_share_position_t;

/* 对冲值，轧差交互与对冲的交互信息 */
typedef struct {
	long long v_id;     /* virtual strategy id */
	int exch_time;      /* exchange time 93000000 */
	double stock_value; /* stock value */
} hedge_stockvalue_t;

/* 查询券源应答数据结构定义 */
typedef struct {
	char account[64];  // 账号
	long serial_no;    // 流水号
	long st_id;        // 策略ID
	long exch_type;    // 交易所编码
	char symbol[32];   // 股票代码
	long lendqty;      // 出借数量
	double lendrate;   // 出借费率
	long last;         // 结束标志 1: 结束
} cicc_qrysbl_response;

/* 约券应答数据结构定义 */
typedef struct {
	char account[64];    // 账号
	long order_id;       // 委托ID，可转换st_id
	long serial_no;      // 流水号
	long exch_type;      // 交易所编码
	char symbol[32];     // 股票代码
	long orderqty;       // 约券数量
	long tradeqty;       // 成交数量
	long status;         // 委托状态
	char contract[32];   // 合同编号
	long errorcode;      // 错误码
	char err_info[128];  // 错误描述
} cicc_imssbl_order_t;

/**
 * @param type: @refer to  strategy_opt_type
 * @param length: length of data.
 * @param data: data content decided by type.
 */
typedef int (*send_data)(int type, int length, void* data);

typedef struct {
	int long_volume;
	double long_price;
	int short_volume;
	double short_price;
} position_t;

typedef struct {
	std::string account;   /* account name */
	int currency;          /* cash currency type */
	double exch_rate;      /* currency exchange rate */
	double cash_asset;     /* cash for pnl counting. */
	double cash_available; /* cash for order pre-sending checking. */
} account_t;

typedef struct {
	int fee_by_lot; /* True - Fee by Lot, False - Fee by Notional */
	double exchange_fee;
	double yes_exchange_fee;
	double broker_fee;       /* 中介费 */
	double stamp_tax;        /* 印花税 */
	double acc_transfer_fee; /* Account Transfer Fee - only for SSE */
} fee_t;

/* 新增了 rank 字段，期货可以直接获取主次力信息 */
typedef struct {
	std::string symbol;       /* 合约名称 */
	char exch;                /* see @enum exchange */
	int max_accum_open_vol;   /* max vol allowed to open in a single day */
	int max_cancel_limit;     /* max cancel order limit number */
	int expiration_date;      /* 合约到期日期，例如：20161125  */
	double tick_size;         /* smallest price movement an asset can make */
	double multiple;          /* A constant set by an exchange, which when multiplied by the futures price
	                            gives the dollar value of a stock index futures contract */
	int rank;                 /* rank for future */
	std::string account;      /* account name */
	position_t yesterday_pos; /* 昨日持仓 */
	position_t today_pos;     /* 今日持仓 */
	fee_t fee;                /* 合约费率 */
	void* reserved_data;
} contract_t;

typedef struct {
	long fid;                         /* 因子 id */
	int size;                         /* total number of factor */
	std::vector<int> indexes;         /* 订阅列位置 */
	std::vector<std::string> columns; /* 订阅列名称 */
} book_factor_desc_t;

typedef struct {
	long count;        /* 因子列个数 */
	long meta_len;     /* meta data 长度 */
	char meta_data[0]; /* meta data 数据 */
} factor_meta_head_t;

/* 扩展字段都用 string 防止长度不够 */
typedef struct {
	std::string portfolio_opt_ev_path;   /* 风控模块 ev 路径 */
	std::string portfolio_opt_version;   /* 风控模块需要的所有参数 */
	std::string smart_execution_ev_path; /* 交易执行模块 ev 路径 */
	std::string extra_arguments;         /* 保留参数，后续扩展给策略使用 */
} extend_arg_t;

typedef struct {
	long long v_id;
	unsigned long long parent_order_id;
	char exch;       /* exchange enumeration value. */
	char symbol[64]; /* symbol name */
	int direction;   /* see @enum direction */
	int volume;      /* target order volume */
	double price;    /* order price */
	int start_time;  /* algorithm start time, TWAP/VWAP use only */
	int end_time;    /* algorithm end time, TWAP/VWAP use only */
	int status;      /* return 0 if success */
	int sync_cancel; /* recieve cancel resp and then send order flag, DMA use only, 1-yes, 0-no */
	double available_cash;
	char account[64];
} merge_order_v2_t;

enum EV_TYPE {
	VARIABLE_EV = 0,  /* 易变的ev */
	PERMANENT_EV = 1, /* 不变的ev */
};

typedef enum _IRR_FACTOR_TYPE {
	PY_ARROW_FACTOR = 0,
} IRR_FACTOR_TYPE;

/* SHARE_IRREGULAR_FACTOR */
typedef struct {
	int type;          /* see @IRR_FACTOR_TYPE */
	int size;          /* buf size */
	long local_time;   /* local time of quote (for simulation) */
	long int_time;     /* int_time feed to strategy, like 94000000 */
	char comment[128]; /* comment to this type of factor */
	char buf[0];       /* buf start point */
} irregular_factor_t;

typedef struct {
	int type;
	int ev_sid;
	std::string ev_file;
} strategy_ev_file_t;

typedef struct {
	send_data proc_order_hdl; /* 发/撤单接口 */
	send_data send_info_hdl;  /* 内部消息接口，比如 debug 信息传递 */
	send_data pass_rsp_hdl;   // add 20171019           /* 通用接口，策略开发者无需使用 */

	int trading_date; /* trading date format like YYYYMMDD */
	int day_night;    /* see @enum DAY_NIGHT */

	std::string param_file_path;  /* 外部变量记录文件名，含目录 */
	std::string output_file_path; /* 策略可写路径 */
	long long st_id;              /* Virtual Strategy ID, strategy send order should carry this id */
	std::string st_name;          /* Strategy NAME */
	extend_arg_t ext;             /* 风控模块配置 */

	std::vector<book_factor_desc_t> factors;  /* 订阅因子信息 */
	std::vector<contract_t> contracts;        /* 交易合约信息配置 */
	std::vector<account_t> accounts;          /* 交易账户信息配置 */
	std::vector<strategy_ev_file_t> ev_files; /* 策略 ev 信息配置 */
} st_config_t;

typedef struct {
	char trade_code[API_ORDER_CODE_LEN]; /* 成交编号 */
	long exchange_type;                  /* 交易所 */
	char trade_time[32];                 /* 成交时间 */
	char account[32];                    /* 账户信息 */
	uint64_t parent_order_id;            /* 母单 id */
	char reserve[24];
} shannon_rsp_supplement;

/* 回报若带回账户信息，通过 reserved_data 字段实现 */
typedef struct {
	unsigned long long order_id; /* 订单编号 */
	char symbol[SYMBOL_LEN];     /* 合约 */
	unsigned short direction;    /* see @enum direction */
	unsigned short open_close;   /* see @enum open_close */
	double exe_price;            /* 执行价 */
	int exe_volume;              /* 执行量 */
	int status;                  /* order_status */
	int error_no;
	char error_info[ERR_MSG_LEN];
	void* reserved_data; /* may point to shannon_rsp_supplement */
} st_response_t;

/* Shannon 行情，回报，自定义信息接口, void * info 可以传任意行情 */
typedef struct {
	void* __trade; /* trade private use, feed into operation api */
	void* info;    /* quote data, response, user defined data */
} st_data_t;

typedef struct {
	int type; /* type == 1 代表按策略停止交易，这时轧差只会使用st_id和is_trade字段, type ==
	             2代表按策略、合约和交易方向停止交易*/
	long st_id;
	char symbol[64]; /* 每一个symbol发一次on_book指令*/
	int direction;
	bool is_trade; /* is_trade == false 代表暂停交易，is_trade == true 代表恢复交易*/
} trade_pause_config_t;

#pragma pack(pop)

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * 该初始化参数进程运行生命周期有效
 *
 * @param type:    config_type
 * @param length:  the data length
 * @param cfg:     determined by type
 * data map {
 *  DEFAULT_CONFIG <-> st_config_t
 * }
 */
EXPORT_SYMBOL int my_st_init_v3(int type, int length, void* cfg);

/**
 * 单次 my_on_book 调用完成后传入的数据无效
 *
 * @param type:    feed_data_type
 * @param length:  the data length
 * @param book:    feed_data pointer
 * data map {
 *  DEFAULT_FUTURE_QUOTE <-> st_data_t(info is my_futures_spot_t)
 *  DEFAULT_STOCK_QUOTE  <-> st_data_t(info is my_stock_t)
 * }
 */
EXPORT_SYMBOL int my_on_book_v3(int type, int length, void* book);

/**
 * 单次 my_on_response 调用完成后传入的数据无效
 *
 * @param type:     response_type
 * @param length:   the data length
 * @param resp:     determined by type
 * data map {
 *  DEFAULT_RSP <-> st_data_t(info is st_response_t)
 * }
 */
EXPORT_SYMBOL int my_on_response_v3(int type, int length, void* resp);

/**
 * 单次 my_on_timer 调用完成后传入的数据无效
 *
 * @param type:     timer_type
 * @param length:   the data length
 * @param resp:     determined by type
 * data map {
 *  DEFAULT_TIMER <-> st_data_t(info is NULL)
 * }
 */
EXPORT_SYMBOL int my_on_timer_v3(int type, int length, void* info);

/**
 * 返回 包含策略版本信息的 JSON 格式字符串
 * 指针指向线程局部静态缓冲区，应在调用后尽快拷贝内容。
 * HACK: (by gaowang) 在 strategy.cc 中实现，不依赖任何其他头文件，以便纯 C 宿主可自持等价 C 声明并链接同一实现。
 */
EXPORT_SYMBOL const char* GetStrategyVersionInfo(void);

/*
* @return:
    version: char*
*/
EXPORT_SYMBOL const char* my_version();

/**
 * release all resources allocated in strategy
 */
EXPORT_SYMBOL void my_destroy_v3(void);

#if defined(__cplusplus)
}
#endif

// HACK: (by gaowang) 与文首 diagnostic push 配对。
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#endif  // _LF_STRATEGY_API_H_
