# raw_data_types 与 sdp_handler 实盘结构对照

`raw_data_reader/raw_data_types` 定义的是 **本地落盘** 的行情结构。

`sdp_handler/quote_format_define.h` 定义的是 **策略/引擎侧（实盘接口风格）** 的行情结构。二者多数字段一致，但命名与个别字段类型/数组长度可能不同。

下文中的「**严格一致**」指：在逐项比对下，成员顺序、类型、含义、维度一致（允许仅结构体标签名不同）。

---

## 1. `my_book_*` 与 `my_futures_t` 的 `quote` 内层类型

- **`my_book_stock`**
  - `quote` 内层：`my_stock_t`
  - sdp 对应：`Stock_Internal_Book`
  - 一致性：**严格一致**
  - 说明：布局相同但 C++ 类型名不同；对接 sdp 用 **`reinterpret_cast<Stock_Internal_Book*>(static_cast<void*>(&quote))`**（零拷贝），见 §2.1。

- **`my_book_stock_order_new`**
  - `quote`：`my_stock_order_new`
  - sdp：`Stock_Order_Internal_Book_New`
  - 一致性：**严格一致**
  - 说明：布局相同但 C++ 类型名不同；对接 sdp 用 **`reinterpret_cast<Stock_Order_Internal_Book_New*>(static_cast<void*>(&quote))`**（零拷贝），见 §2.1。

- **`my_book_stock_transaction_new`**
  - `quote`：`my_stock_transaction_new`
  - sdp：`Stock_Transaction_Internal_Book_New`
  - 一致性：**严格一致**
  - 说明：布局相同但 C++ 类型名不同；对接 sdp 用 **`reinterpret_cast<Stock_Transaction_Internal_Book_New*>(static_cast<void*>(&quote))`**（零拷贝），见 §2.1。

- **`my_book_stock_order`**
  - `quote`：`my_stock_order`
  - sdp：`Stock_Order_Internal_Book`
  - 一致性：**极度相似**
  - 说明：主要差在 `symbol`：`kScrCodeLen`（9）与 `SCR_CODE_LEN`（32）。需 **逐字段拷贝** 到 `Stock_Order_Internal_Book`，见 §2.2。

- **`my_book_stock_order_queue`**
  - `quote`：`my_stock_order_queue`
  - sdp：`Stock_Queue_Internal_Book`
  - 一致性：**极度相似**
  - 说明：主要差在 `symbol`：`kScrCodeLen`（9）与 `SCR_CODE_LEN`（32）。需 **逐字段拷贝** 到 `Stock_Queue_Internal_Book`，见 §2.2。

- **`my_book_stock_transaction`**
  - `quote`：`my_stock_transaction`
  - sdp：`Stock_Transaction_Internal_Book`
  - 一致性：**极度相似**
  - 说明：主要差在 `symbol`：`kScrCodeLen`（9）与 `SCR_CODE_LEN`（32）。需 **逐字段拷贝** 到 `Stock_Transaction_Internal_Book`，见 §2.2。

- **`my_book_stock_order_trans_new`**
  - `quote`：`my_stock_order_trans_new`（231 组合等用途，见 raw 头文件）
  - sdp：**策略/sdp 侧通常没有与之一一对应的结构体**
  - 说明：见 §2.4。

- **`my_futures_t`**
  - `quote`：`_my_futures`
  - sdp：`Futures_Internal_Book`
  - 一致性：**极度相似**
  - 说明：`book_type` vs `feed_type`；`exchange` 为 `uint8_t` vs `int16_t`；其余字段基本一一对应，见 §2.3。

---

## 2. 按一致性程度对接 sdp 侧

以下按 §1 的归类说明 **处理办法**，不绑定某一种读盘组件或业务回调；消费侧在拿到 `my_book_*` / `my_futures_t` 后按需选用。

### 2.1 严格一致（布局相同，仅 C++ 类型标签不同）

适用：§1 中标注为 **严格一致** 的股票类（如 `my_stock_t` ↔ `Stock_Internal_Book`，以及 `my_stock_order_new` / `my_stock_transaction_new` 等与对应 `*_New` 结构）。

- `&record.quote` 与 sdp 目标类型 **内存布局一致**，但二者指针类型不同，**不能**依赖隐式转换。
- **零拷贝**：经 `void*` 中转后 **`reinterpret_cast`** 为 sdp 侧指针即可。
- 建议在包含对接代码的翻译单元内增加 **`static_assert(sizeof(raw_inner) == sizeof(sdp))`**，任一侧头文件变更导致布局漂移时会编译失败。
- **不要**为了「再传指针」而对整颗 `quote` 做 `memcpy`；只有需要 **与读入缓冲解耦的独立副本** 时才整颗拷贝。

```cpp
// 示例：内层已与 sdp 逐项核对为严格一致
static_assert(sizeof(my_stock_t) == sizeof(Stock_Internal_Book), "layout mismatch");
auto* quote = reinterpret_cast<Stock_Internal_Book*>(
    static_cast<void*>(&row.quote));

static_assert(sizeof(my_stock_order_new) == sizeof(Stock_Order_Internal_Book_New), "layout mismatch");
auto* order = reinterpret_cast<Stock_Order_Internal_Book_New*>(
    static_cast<void*>(&row.quote));

static_assert(sizeof(my_stock_transaction_new) == sizeof(Stock_Transaction_Internal_Book_New), "layout mismatch");
auto* trans = reinterpret_cast<Stock_Transaction_Internal_Book_New*>(
    static_cast<void*>(&row.quote));
```

### 2.2 极度相似：比如几乎一样，但是 `symbol` 长度不同（`9` vs `32`）

适用：`my_stock_order`、`my_stock_order_queue`、`my_stock_transaction` 等与 `Stock_Order_Internal_Book` / `Stock_Queue_Internal_Book` / `Stock_Transaction_Internal_Book`（**非** New）对应的情形。

- **不要**对整颗 inner 做 `reinterpret_cast`。
- 在栈或堆上 **构造 sdp 目标结构体**：`symbol` 先 **`memset` 为 0**，再 **`memcpy` 较短一侧长度**（或等价地逐字节写入），其余字段 **逐字段赋值**。若除 `symbol` 外仍有顺序/对齐差异，仍 **禁止** 对整结构 `memcpy`，按字段表逐项拷贝。

```cpp
#include <cstring>

void map_order(const my_stock_order& src, Stock_Order_Internal_Book& dst) {
    std::memset(dst.symbol, 0, sizeof(dst.symbol));
    std::memcpy(dst.symbol, src.symbol, sizeof(src.symbol));  // kScrCodeLen
    dst.market       = src.market;
    dst.int_time     = src.int_time;
    dst.order_price  = src.order_price;
    dst.order_id     = src.order_id;
    dst.order_volume = src.order_volume;
    std::memcpy(dst.side, src.side, sizeof(dst.side));
    std::memcpy(dst.order_kind, src.order_kind, sizeof(dst.order_kind));
}
```

队列、成交旧结构同理：先处理 `symbol[SCR_CODE_LEN]`，再按 `quote_format_define.h` 与 raw 头文件对齐其余成员。

### 2.3 极度相似：期货 `_my_futures` ↔ `Futures_Internal_Book`

- 存在 **字段重命名**（如 raw 的 `book_type` ↔ sdp 的 `feed_type`）与 **整型变宽**（如 `exchange`：`uint8_t` → `int16_t`）。
- 在 **`Futures_Internal_Book` 实例上显式赋值**；布局一致的大块数组可用 `memcpy` 或逐元素拷贝。

```cpp
#include <cstring>

void map_futures(const my_futures_t& row, Futures_Internal_Book& out) {
    out.feed_type = row.quote.book_type;
    out.exchange  = static_cast<int16_t>(row.quote.exchange);
    std::memcpy(out.symbol, row.quote.symbol, sizeof(out.symbol));
    out.int_time = row.quote.int_time;
    // 其余字段按两侧头文件逐项对齐后赋值，勿假设与 row.quote 二进制布局完全相同
}
```

若消费侧还需时间戳、会话类型等，在业务层按平台约定另行组装即可。

### 2.4 sdp 侧无同名、同布局类型

例如 `my_stock_order_trans_new`：raw 可定义并落盘，但常见 sdp 包中 **没有** 与之 **一一对应** 的标准入参结构。读盘侧若必须使用，应在 **自有结构体** 中承接字段，或 **先扩展** sdp/策略侧头文件再对接；**没有** 通用的零拷贝 `reinterpret_cast` 路径。

---

## 3. 维护注意

- **单一事实来源**：若希望彻底避免「两套命名」，应在工程层约定：要么以 `sdp_handler` 为准 typedef，要么以 `raw_data_types` 为准并在对接 sdp/策略前统一转换。
- **与上游同步**：`tick-data-reader` 与 `sdp_handler` 任一方字段变更时，应重新做字段级 diff（`sizeof` / `offsetof` 或脚本比对）。
