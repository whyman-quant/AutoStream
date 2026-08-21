# Order Builder V2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 `demofw00` 的订单合成器升级为按真实沪深事件语义运行的共享流式母单 Builder，并在正确性和效率门槛通过后合并到 `main`。

**Architecture:** Builder 以市场规则解释逐笔成交与委托，深市用 `order_index`、沪市用 `orderorino` 作为规范母单 ID；它区分在途母单、活跃挂单和完成订单，并在 `OnTrans`、`OnOrder`、水位推进时输出标准事件。因子回调立即消费标准事件，`DoOnUpdateFactors` 只读取累计状态。

**Tech Stack:** C++17、现有 `Stock_Order_Internal_Book_New` / `Stock_Transaction_Internal_Book_New`、GNU Make、CMake、真实 NPQ 回放、Python 3.8 仅用于辅助审计。

---

## 文件结构

- Modify: `factors/demofw00/tools/market/order_synthesizer.h` — Builder公开类型、状态、输出与审计接口。
- Modify: `factors/demofw00/tools/market/order_synthesizer.cpp` — 沪深状态机、数量守恒、撤单、方向、价格和水位实现。
- Modify: `factors/demofw00/factor_entry.cpp` — 在逐笔回调中消费Builder输出；撤单先进入Builder。
- Create: `tests/demofw00/order_synthesizer_test.cpp` — 不依赖外部文件的状态机回归测试。
- Create: `tests/demofw00/order_synthesizer_npq_replay.cpp` — 真实NPQ两市场回放与性能报告。
- Modify: `Makefile` — `test-order-builder`和`bench-order-builder`入口，产物写入仓库内忽略的`build-tests/`。
- Create: `factors/demofw00/tools/market/ORDER_BUILDER_V2.md` — 研究人员可读的市场映射、状态流、字段单位和接入说明。
- Modify: `factors/demofw00/tools/README.md` — 链接Builder文档和测试命令。

### Task 1: 建立可重复的专项测试入口

**Files:**
- Create: `tests/demofw00/order_synthesizer_test.cpp`
- Modify: `Makefile`

- [ ] **Step 1: 写最小失败测试，定义期望的新API**

测试文件先声明以下用法并验证沪市规范母单ID：

```cpp
#include <cstdlib>
#include <iostream>
#include "factors/demofw00/tools/market/order_synthesizer.h"

using factors::demofw00::tools::market::BuilderEventKind;
using factors::demofw00::tools::market::OrderSynthesizer;

#define CHECK(expr) do { if (!(expr)) { \
    std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr << '\n'; \
    std::exit(1); \
} } while (0)

int main() {
    OrderSynthesizer builder(10000);
    Stock_Order_Internal_Book_New order{};
    order.market = 49;
    order.order_type = 'A';
    order.bsflag = 'B';
    order.order_index = 900001;
    order.orderorino = 10001;
    order.biz_index = 102;
    order.order_volume = 500;
    order.int_time = 93000000;

    const auto& out = builder.OnOrder(order);
    CHECK(out.events.empty());
    CHECK(builder.HasOrder(49, order.channel, 10001));
    CHECK(!builder.HasOrder(49, order.channel, 900001));
    return 0;
}
```

- [ ] **Step 2: 增加Makefile测试编译入口**

```make
.PHONY: test-order-builder
test-order-builder:
	@mkdir -p build-tests/order-builder
	$(CXX) -std=c++17 -O2 -I. \
		tests/demofw00/order_synthesizer_test.cpp \
		factors/demofw00/tools/market/order_synthesizer.cpp \
		-o build-tests/order-builder/order_synthesizer_test
	@build-tests/order-builder/order_synthesizer_test
```

- [ ] **Step 3: 运行测试并确认因缺少V2 API而失败**

Run: `make test-order-builder`

Expected: 编译失败，明确报告 `BuilderEventKind`、`BuilderOutput`或`HasOrder`不存在；不是路径或语法错误。

- [ ] **Step 4: 提交测试入口**

```bash
git add Makefile tests/demofw00/order_synthesizer_test.cpp
git commit -m "test: define order builder v2 contract"
```

### Task 2: 实现市场、主键、方向和价格基础契约

**Files:**
- Modify: `factors/demofw00/tools/market/order_synthesizer.h`
- Modify: `factors/demofw00/tools/market/order_synthesizer.cpp`
- Test: `tests/demofw00/order_synthesizer_test.cpp`

- [ ] **Step 1: 扩展失败测试**

新增断言：

```cpp
CHECK(OrderSynthesizer::CanonicalOrderId(order) == 10001);
order.market = 48;
CHECK(OrderSynthesizer::CanonicalOrderId(order) == 900001);

Stock_Transaction_Internal_Book_New trade{};
trade.market = 49;
trade.bsflag = 'B';
trade.buy_id = 10001;
trade.sell_id = 9001;
trade.trade_price = 85700;
trade.trade_volume = 100;
trade.trade_amount = 857;
trade.biz_index = 100;
trade.int_time = 93000000;
const auto& trade_out = builder.OnTrans(trade);
CHECK(trade_out.aggressor_side == AggressorSide::Buy);
CHECK(trade_out.aggressor_source == AggressorSource::BsFlag);
CHECK(trade_out.price_x1e4 == 85700);
CHECK(trade_out.avg_price_x1e4 == 85700);
```

- [ ] **Step 2: 运行并确认失败原因是基础契约尚未实现**

Run: `make test-order-builder`

Expected: FAIL于缺少规范ID、方向来源或价格字段。

- [ ] **Step 3: 最小实现公开类型与辅助函数**

头文件定义：

```cpp
enum class AggressorSource : uint8_t { Unknown, BsFlag, OrderIdFallback };
enum class BuilderEventKind : uint8_t {
    MotherOrderConfirmed, TradePairResolved, RestQtyChanged, MotherOrderClosed
};
enum class CloseReason : uint8_t {
    None, FullyFilledOnArrival, FullyFilledAfterResting, Cancelled, SessionClose
};

struct BuilderOutput {
    std::vector<OrderEvent> events;
    std::vector<TradePairEvent> trade_pairs;
    AggressorSide aggressor_side{AggressorSide::Unknown};
    AggressorSource aggressor_source{AggressorSource::Unknown};
    int64_t price_x1e4{0};
    int64_t avg_price_x1e4{0};
};
```

实现：

```cpp
int64_t OrderSynthesizer::CanonicalOrderId(const OrderQuote& q) {
    if (q.market == 49) return static_cast<int64_t>(q.orderorino);
    if (q.market == 48) return q.order_index;
    return 0;
}

AggressorSide ResolveAggressor(const TradeQuote& q, AggressorSource* source) {
    if (q.bsflag == 'B' || q.bsflag == 'b') { *source = AggressorSource::BsFlag; return AggressorSide::Buy; }
    if (q.bsflag == 'S' || q.bsflag == 's') { *source = AggressorSource::BsFlag; return AggressorSide::Sell; }
    if (q.buy_id > 0 && q.sell_id > 0 && q.buy_id != q.sell_id) {
        *source = AggressorSource::OrderIdFallback;
        return q.buy_id > q.sell_id ? AggressorSide::Buy : AggressorSide::Sell;
    }
    *source = AggressorSource::Unknown;
    return AggressorSide::Unknown;
}
```

`avg_price_x1e4 = filled_amount_yuan * 10000 / filled_volume`，使用长双精度中间值防溢出。

- [ ] **Step 4: 运行测试确认通过**

Run: `make test-order-builder`

Expected: PASS。

- [ ] **Step 5: 提交基础契约**

```bash
git add factors/demofw00/tools/market/order_synthesizer.* tests/demofw00/order_synthesizer_test.cpp
git commit -m "feat: add venue-aware order builder contract"
```

### Task 3: 实现深市生命周期和C撤单

**Files:**
- Modify: `factors/demofw00/tools/market/order_synthesizer.h`
- Modify: `factors/demofw00/tools/market/order_synthesizer.cpp`
- Test: `tests/demofw00/order_synthesizer_test.cpp`

- [ ] **Step 1: 写深市失败测试**

按以下序列测试数量守恒：

```cpp
// order_index=7001, original/rest=1000
// normal trade 300 -> rest=700
// trade_type C cancel 700 -> rest=0, close reason Cancelled
CHECK(confirmed.original_qty == 1000);
CHECK(after_trade.filled_qty == 300);
CHECK(after_trade.rest_qty == 700);
CHECK(closed.cancelled_qty == 700);
CHECK(closed.rest_qty == 0);
CHECK(closed.original_qty == closed.filled_qty + closed.cancelled_qty);
CHECK(!builder.HasOrder(48, channel, 7001));
CHECK(cancel_output.aggressor_side == AggressorSide::Unknown);
```

- [ ] **Step 2: 运行并确认失败**

Run: `make test-order-builder`

Expected: FAIL于撤单未更新或状态未关闭。

- [ ] **Step 3: 实现深市状态更新**

`OnOrder`对市场48保存原始`raw_order_type`，立即确认母单；`OnTrans`正常成交同时更新买卖两侧；`trade_type == 'C'`只更新非零目标ID的`cancelled_qty/rest_qty`，不生成正常成交或主动方。

完成状态满足：

```cpp
original_qty == immediate_fill_qty + passive_fill_qty + cancelled_qty + rest_qty
```

- [ ] **Step 4: 运行测试确认通过**

Run: `make test-order-builder`

Expected: PASS。

- [ ] **Step 5: 提交深市生命周期**

```bash
git add factors/demofw00/tools/market/order_synthesizer.* tests/demofw00/order_synthesizer_test.cpp
git commit -m "feat: track Shenzhen fills and cancellations"
```

### Task 4: 实现沪市即时成交、A剩余单和水位封账

**Files:**
- Modify: `factors/demofw00/tools/market/order_synthesizer.h`
- Modify: `factors/demofw00/tools/market/order_synthesizer.cpp`
- Test: `tests/demofw00/order_synthesizer_test.cpp`

- [ ] **Step 1: 写沪市部分成交失败测试**

构造两笔主动买成交（`biz=100/101`，共500股）和`A biz=102, orderorino=10001, order_volume=500`，再将水位推进到10秒之后：

```cpp
CHECK(before_watermark.events.empty());
CHECK(confirmed.order_id == 10001);
CHECK(confirmed.original_qty == 1000);
CHECK(confirmed.immediate_fill_qty == 500);
CHECK(confirmed.passive_fill_qty == 0);
CHECK(confirmed.rest_qty == 500);
CHECK(confirmed.raw_event_id == 900001);
```

- [ ] **Step 2: 写到达顺序倒置失败测试**

先调用`OnOrder(A biz=102)`，再调用`OnTrans(biz=100/101)`，推进同样水位，要求得到与业务顺序输入完全相同的母单结果。

- [ ] **Step 3: 写全部成交无A失败测试**

同一主动母单成交300和700股，无`A`，水位越过后要求：

```cpp
CHECK(closed.original_qty == 1000);
CHECK(closed.immediate_fill_qty == 1000);
CHECK(closed.rest_qty == 0);
CHECK(closed.close_reason == CloseReason::FullyFilledOnArrival);
```

- [ ] **Step 4: 运行并确认三个场景失败**

Run: `make test-order-builder`

Expected: FAIL于沪市母单未确认、倒序结果不同或无A订单未封账。

- [ ] **Step 5: 实现在途母单与10秒水位**

状态保存按`biz_index`排序的未确认成交片段。Builder内部维护`max_seen_event_time_ms`，每次逐笔事件后计算`safe_event_time = max_seen - pending_watermark_ms`。水位只确认初始母单，不删除`rest_qty > 0`的活跃挂单。

收到A后按业务序号划分：

```text
biz < A.biz_index -> immediate_fill
biz > A.biz_index -> passive_fill并扣减rest
```

无A的主动母单在水位越过后按累计即时成交量封账。

- [ ] **Step 6: 运行测试确认通过**

Run: `make test-order-builder`

Expected: PASS。

- [ ] **Step 7: 提交沪市初始母单重建**

```bash
git add factors/demofw00/tools/market/order_synthesizer.* tests/demofw00/order_synthesizer_test.cpp
git commit -m "feat: reconstruct Shanghai mother orders"
```

### Task 5: 实现沪市历史挂单、D撤单和完成索引

**Files:**
- Modify: `factors/demofw00/tools/market/order_synthesizer.h`
- Modify: `factors/demofw00/tools/market/order_synthesizer.cpp`
- Test: `tests/demofw00/order_synthesizer_test.cpp`

- [ ] **Step 1: 写后续被动成交和D撤单失败测试**

在已确认`original=1000, immediate=500, rest=500`基础上，后续被动成交200、D撤单300：

```cpp
CHECK(after_passive.passive_fill_qty == 200);
CHECK(after_passive.rest_qty == 300);
CHECK(closed.cancelled_qty == 300);
CHECK(closed.rest_qty == 0);
CHECK(closed.close_reason == CloseReason::Cancelled);
CHECK(closed.original_qty == 1000);
CHECK(builder.CompletedOrderCount() == 1);
CHECK(!builder.HasOrder(49, channel, 10001));
```

- [ ] **Step 2: 写TTL不删除活跃挂单失败测试**

确认一笔`rest_qty=500`的A挂单，将事件时间推进数分钟，要求状态仍存在；只有成交或撤单使`rest_qty=0`后才移除。

- [ ] **Step 3: 运行并确认失败**

Run: `make test-order-builder`

Expected: FAIL于D未关联`orderorino`、活跃挂单被清理或没有完成索引。

- [ ] **Step 4: 实现D撤单和紧凑完成记录**

`D`以`orderorino`查找母单，增加`cancelled_qty`并扣减`rest_qty`。归零后输出`MotherOrderClosed`，将不可变快照写入完成索引，再从活跃表删除。删除统一3秒清理路径。

- [ ] **Step 5: 运行测试确认通过**

Run: `make test-order-builder`

Expected: PASS。

- [ ] **Step 6: 提交沪市挂单生命周期**

```bash
git add factors/demofw00/tools/market/order_synthesizer.* tests/demofw00/order_synthesizer_test.cpp
git commit -m "feat: track Shanghai resting orders"
```

### Task 6: 实现买卖母单成对输出并接入FactorEntry

**Files:**
- Modify: `factors/demofw00/tools/market/order_synthesizer.h`
- Modify: `factors/demofw00/tools/market/order_synthesizer.cpp`
- Modify: `factors/demofw00/factor_entry.cpp`
- Test: `tests/demofw00/order_synthesizer_test.cpp`

- [ ] **Step 1: 写成对输出失败测试**

构造一笔买卖双方规模不同的成交，要求`TradePairResolved`同时保存：

```cpp
CHECK(pair.buy_order.order_id == buy_id);
CHECK(pair.sell_order.order_id == sell_id);
CHECK(pair.buy_order.original_qty == 1000);
CHECK(pair.sell_order.original_qty == 3000);
CHECK(pair.aggressor_side == AggressorSide::Buy);
CHECK(pair.trade_volume == 500);
```

- [ ] **Step 2: 运行并确认失败**

Run: `make test-order-builder`

Expected: FAIL于没有`TradePairResolved`或只能看到最后一侧。

- [ ] **Step 3: 实现待解析成交对**

双方母单尚未确认时缓存紧凑成交对；任一状态确认或水位推进后尝试释放双方均已确认的记录。缓存只保留未解析记录，释放后立即删除。

- [ ] **Step 4: 将FactorEntry改为逐笔消费BuilderOutput**

增加内部统一函数：

```cpp
void ConsumeBuilderOutput(FactorEntry::Impl& impl, const BuilderOutput& output) {
    for (const auto& event : output.events) {
        // 更新最近完整母单、执行率、估计/水位来源与审计计数
    }
    for (const auto& pair : output.trade_pairs) {
        // 保留双方语义，诊断列使用incoming_order而不是循环最后一侧
    }
}
```

回调顺序：

```text
DoOnAddTrans -> builder.OnTrans -> ConsumeBuilderOutput
DoOnAddOrder -> builder.OnOrder -> ConsumeBuilderOutput
DoOnGlobalTime -> builder.AdvanceWatermark -> ConsumeBuilderOutput
DoOnUpdateFactors -> 只读取状态
```

深市`C`与沪市`D`先进入Builder，撤单不进入普通成交/新增委托分析器。

- [ ] **Step 5: 运行专项测试和完整构建**

Run: `make test-order-builder`

Expected: PASS。

Run: `make build-factor BUILD_DIR=build-order-builder JOBS=4 CMAKE_CXX_STANDARD=17`

Expected: `factors_demofw00`与`app_factor/main`构建成功。

- [ ] **Step 6: 提交FactorEntry接入**

```bash
git add factors/demofw00 tests/demofw00/order_synthesizer_test.cpp
git commit -m "feat: consume resolved mother order events"
```

### Task 7: 真实NPQ回放与效率门槛

**Files:**
- Create: `tests/demofw00/order_synthesizer_npq_replay.cpp`
- Modify: `Makefile`

- [ ] **Step 1: 写真实回放工具**

工具按现有定长结构读取256/257 NPQ，按`local_time`模拟当前框架到达顺序，将记录送入同一个Builder，并输出：

```text
date/symbol/market
input_trans/input_orders/input_cancels
confirmed/closed/trade_pairs
active/pending/completed/max_active/max_pending
hard_errors/negative_rest/conservation_failures/late_events
elapsed_seconds/events_per_second
```

命令行：

```text
order_synthesizer_npq_replay <trans.npq> <order.npq>
```

- [ ] **Step 2: 增加Makefile入口**

```make
.PHONY: bench-order-builder
bench-order-builder:
	@mkdir -p build-tests/order-builder
	$(CXX) -std=c++17 -O3 -DNDEBUG -I. \
		tests/demofw00/order_synthesizer_npq_replay.cpp \
		factors/demofw00/tools/market/order_synthesizer.cpp \
		-o build-tests/order-builder/order_synthesizer_npq_replay
```

- [ ] **Step 3: 回放四个真实样本**

Run:

```bash
make bench-order-builder
build-tests/order-builder/order_synthesizer_npq_replay \
  /data/256/2022/20220222/0/0/000001.npq \
  /data/257/2022/20220222/0/0/000001.npq
build-tests/order-builder/order_synthesizer_npq_replay \
  /data/256/2022/20220222/0/0/600000.npq \
  /data/257/2022/20220222/0/0/600000.npq
build-tests/order-builder/order_synthesizer_npq_replay \
  /data/256/2025/20251013/0/0/000001.npq \
  /data/257/2025/20251013/0/0/000001.npq
build-tests/order-builder/order_synthesizer_npq_replay \
  /data/256/2025/20251013/0/0/600000.npq \
  /data/257/2025/20251013/0/0/600000.npq
```

Expected:

- `negative_rest == 0`
- `conservation_failures == 0`
- 正常数据不因统一TTL删除活跃挂单
- 深市C和沪市D撤单均被计数
- 沪市成交与A/D通过`orderorino`关联
- 单线程回放吞吐率不低于100万输入事件/秒；若低于门槛，不进入合并，先用性能分析定位。

- [ ] **Step 4: 提交回放工具**

```bash
git add Makefile tests/demofw00/order_synthesizer_npq_replay.cpp
git commit -m "test: add real npq order builder replay"
```

### Task 8: 文档、完整验证和功能分支提交

**Files:**
- Create: `factors/demofw00/tools/market/ORDER_BUILDER_V2.md`
- Modify: `factors/demofw00/tools/README.md`

- [ ] **Step 1: 编写研究人员可读说明**

文档包含：沪深输入事件映射、三类母单形成过程、标准事件回调时点、字段单位、数量守恒、TTL/水位适用范围、撤单路径、因子接入示例、测试与回放命令、已知边界。

- [ ] **Step 2: 检查文档与公开类型一致**

Run:

```bash
rg -n "orderorino|order_index|trade_type.*C|order_type.*D|price_x1e4|DoOnAddTrans|DoOnAddOrder" \
  factors/demofw00/tools/market/ORDER_BUILDER_V2.md \
  factors/demofw00/tools/market/order_synthesizer.h
```

Expected: 文档中的字段名和代码公开类型完全一致。

- [ ] **Step 3: 执行新鲜完整验证**

Run:

```bash
make test-order-builder
make build-factor BUILD_DIR=build-order-builder-final JOBS=4 CMAKE_CXX_STANDARD=17
make bench-order-builder
build-tests/order-builder/order_synthesizer_npq_replay \
  /data/256/2022/20220222/0/0/000001.npq \
  /data/257/2022/20220222/0/0/000001.npq
build-tests/order-builder/order_synthesizer_npq_replay \
  /data/256/2022/20220222/0/0/600000.npq \
  /data/257/2022/20220222/0/0/600000.npq
git diff --check
```

Expected: 所有命令退出码0，专项测试0失败，完整构建成功，真实回放无数量守恒/负剩余错误，吞吐率达到门槛。

- [ ] **Step 4: 提交文档与最终调整**

```bash
git add factors/demofw00/tools/README.md \
  factors/demofw00/tools/market/ORDER_BUILDER_V2.md
git commit -m "docs: explain streaming mother order builder"
```

### Task 9: 合并到main并复验

**Files:**
- Merge only; no uncommitted files.

- [ ] **Step 1: 确认功能分支干净且提交完整**

Run:

```bash
git status --short
git log --oneline main..codex/order-builder-v2
```

Expected: 工作区干净，提交列表包括设计、测试、实现、回放和文档。

- [ ] **Step 2: 在主检出合并功能分支**

```bash
cd /mnt/beegfs_ssd_raid91/10513_fangwei/AutoStream/base/hf-open5m-factor-demo
git checkout main
git merge --no-ff codex/order-builder-v2
```

Expected: 合并成功且无冲突。若`main`在执行期间发生变化，先停止并重新评估差异，不强制覆盖。

- [ ] **Step 3: 在合并后的main重新验证**

Run:

```bash
make test-order-builder
make build-factor BUILD_DIR=build-order-builder-main JOBS=4 CMAKE_CXX_STANDARD=17
make bench-order-builder
build-tests/order-builder/order_synthesizer_npq_replay \
  /data/256/2022/20220222/0/0/000001.npq \
  /data/257/2022/20220222/0/0/000001.npq
build-tests/order-builder/order_synthesizer_npq_replay \
  /data/256/2022/20220222/0/0/600000.npq \
  /data/257/2022/20220222/0/0/600000.npq
git status --short
```

Expected: 测试、构建、性能与真实回放再次通过；除忽略的构建产物外工作区干净。

- [ ] **Step 4: 仅在main复验通过后清理隔离worktree**

```bash
git worktree remove \
  /mnt/beegfs_ssd_raid91/10513_fangwei/AutoStream/base/hf-open5m-factor-demo/.worktrees/order-builder-v2
git worktree prune
git branch -d codex/order-builder-v2
```

Expected: 功能已保留在main，隔离worktree和已合并分支被安全移除。
