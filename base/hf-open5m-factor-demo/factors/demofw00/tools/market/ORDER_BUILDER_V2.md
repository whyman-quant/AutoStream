# 流式母单重建器 V2

## 1. 它解决什么研究问题

逐笔成交只说明“这一笔成交了多少”，不直接说明发起者原本下了多大的单。母单重建器把逐笔委托和逐笔成交按交易所规则串起来，持续维护一张订单从出现、成交、挂单、撤单到关闭的全过程。

因子不需要重新猜测订单编号、主动方向或剩余数量，而是消费统一的母单事件。这样，大小单、执行率、主动买卖力量和挂单消耗等研究都使用同一套底层口径。

## 2. 沪深输入规则

| 市场 | 母单编号 | 新增/挂单 | 正常成交 | 撤单 |
|---|---|---|---|---|
| 深市，`market=48` | `order_index` | 逐笔委托到达即建立母单 | 成交的 `buy_id`、`sell_id` 分别更新两侧 | 逐笔成交中 `trade_type='C'` |
| 沪市，`market=49` | `orderorino` | 逐笔委托中 `order_type='A'` 表示剩余单进入簿中 | 成交的 `buy_id`、`sell_id` 与 `orderorino` 关联 | 逐笔委托中 `order_type='D'` |

沪市的 `order_index` 只是原始事件编号，不能作为母单编号。`A` 和 `D` 是事件类型，也不能解释成价格指令。

主动方向优先使用成交记录的 `bsflag`：`B` 为主动买，`S` 为主动卖。只有 `bsflag` 不可用时，才使用买卖订单编号关系做回退，并把来源标记为 `OrderIdFallback`；无法判断时为 `Unknown`。

## 3. 三类母单形成过程

### 深市母单

深市委托一到达就能知道原始数量。后续正常成交扣减剩余量，`trade_type='C'` 增加撤单量。剩余量归零后关闭。

### 沪市全部即时成交母单

如果主动订单全部成交，通常没有 `A` 事件。重建器先暂存属于该主动订单的成交片段，跨过安全水位后，把累计即时成交量作为原始母单量，并以 `FullyFilledOnArrival` 关闭。

### 沪市部分成交后挂单母单

沪市 `A.biz_index` 紧接最后一笔即时成交。业务序号小于 `A.biz_index` 的片段计入 `immediate_fill_qty`，业务序号大于它的后续成交计入 `passive_fill_qty`。

母单原始量为：

```text
original_qty = immediate_fill_qty + A事件中的剩余数量
```

收到 `D` 或后续挂单成交时，只扣减仍然有效的 `rest_qty`。

## 4. 数量守恒和字段单位

每个有效母单必须始终满足：

```text
original_qty
= immediate_fill_qty
+ passive_fill_qty
+ cancelled_qty
+ rest_qty
```

字段单位如下：

- `original_qty`、`immediate_fill_qty`、`passive_fill_qty`、`cancelled_qty`、`rest_qty`、`trade_volume`：股。
- `trade_amount`、`filled_amount_yuan`：元。
- `trade_price`、`price_x1e4`、`avg_price_x1e4`：价格乘以 10,000 后的整数。

均价使用 128 位整数中间值计算，避免大金额溢出，也不引入浮点舍入漂移。

## 5. 水位不是活跃订单 TTL

水位只判断“沪市初始母单是否已经等齐可能倒置到达的 A/成交事件”。母单一旦确认，只要 `rest_qty > 0`，无论经过多少分钟都不会因为时间流逝被删除。

生产默认水位为 **20 秒**。设计初稿的 10 秒假设被真实数据推翻：2022-02-22 的 600000 样本中，`A` 与其 `biz_index-1` 即时成交的本机到达顺序最大倒置为 11.741 秒；同时全局交换所时钟可能领先本机迟到事件，15 秒仍出现提前封账。20 秒是四组真实样本中通过的最小安全档位。

15:00 的 `FlushAtClose` 是独立的日终动作。它把仍在簿中的剩余量计入会话结束数量，以 `SessionClose` 关闭，保证状态不会跨日。

## 6. 标准输出事件

- `MotherOrderConfirmed`：原始母单量已经可以使用。
- `TradePairResolved`：一笔正常成交的买卖两侧母单都已确认；通过 `TradePairEvent.buy_order` 和 `sell_order` 同时读取，不能只保留循环中的最后一侧。
- `RestQtyChanged`：挂单成交或部分撤单改变剩余量。
- `MotherOrderClosed`：订单全部成交、撤单或收盘清算后关闭。

关闭原因包括 `FullyFilledOnArrival`、`FullyFilledAfterResting`、`Cancelled` 和 `SessionClose`。

## 7. 因子接入方式

`FactorEntry` 在逐笔回调中立即消费结果：

```text
DoOnAddTrans  -> OrderSynthesizer::OnTrans  -> ConsumeBuilderOutput
DoOnAddOrder  -> OrderSynthesizer::OnOrder  -> ConsumeBuilderOutput
DoOnGlobalTime -> AdvanceWatermark / FlushAtClose -> ConsumeBuilderOutput
DoOnUpdateFactors -> 只读取已经累计的状态
```

深市 `trade_type='C'` 和沪市 `order_type='D'` 都先进入重建器，随后直接返回，不会被普通成交或新增委托分析器重复计算。

新增因子应优先使用 `TradePairEvent` 研究一笔成交的两侧规模；若研究母单生命周期，则使用 `OrderEvent`。不要在因子中再次用 `order_index`、`orderorino` 或买卖编号自行拼接。

## 8. 测试和真实回放

专项状态机测试：

```bash
make test-order-builder
```

构建真实 NPQ 回放工具：

```bash
make bench-order-builder
```

回放示例：

```bash
build-tests/order-builder/order_synthesizer_npq_replay \
  /data/256/2022/20220222/0/0/600000.npq \
  /data/257/2022/20220222/0/0/600000.npq
```

回放报告包含输入成交/委托/撤单数、确认/关闭/成交对数量、盘中最大活跃和等待状态、负剩余、数量不守恒、迟到事件、盘中吞吐和日终清算耗时。`hard_errors`、`negative_rest`、`conservation_failures` 和 `late_events` 在正常样本中必须全部为零。

诊断时可临时指定水位或跟踪母单：

```bash
ORDER_BUILDER_WATERMARK_MS=20000 \
ORDER_BUILDER_TRACE_ID=608613 \
build-tests/order-builder/order_synthesizer_npq_replay <trans.npq> <order.npq>
```

当前真实验收范围为 2022-02-22 和 2025-10-13 的 000001、600000。单线程盘中重建吞吐门槛为每秒 100 万输入事件；日终清算耗时单独报告。

## 9. 已知边界

- 一个 `OrderSynthesizer` 服务一个证券的一天；证券代码由外层 `FactorEntry` 隔离。
- 不支持的市场不会静默套用沪深规则。
- 缺失一侧母单的成交对只保留在待解析缓存，日终不会伪造不存在的母单。
- 水位参数必须由真实到达倒置审计支持，不应为了更快出结果随意缩短。
