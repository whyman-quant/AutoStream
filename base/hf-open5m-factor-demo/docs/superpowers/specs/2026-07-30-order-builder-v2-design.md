# 沪深母单流式重建 Builder V2 设计

## 1. 目标

将 `factors/demofw00/tools/market/order_synthesizer.*` 从演示状态机升级为可供因子共享的母单重建组件。组件负责解释沪深两市不同的逐笔事件语义，形成完整母单、维护剩余挂单和撤单状态，并在逐笔回调中输出标准研究事件。

母单重建与具体因子表达式分离。500 个以上候选因子共享一次 Builder 状态，因子只消费标准事件，不各自复制订单状态。

## 2. 已核验的数据事实

设计依据为 `/data` 下 2018、2022、2024、2025 年的真实 NPQ 样本，覆盖 `000001` 与 `600000`，并扩展检查了 2022 年的 `600519`、`601318`。

- 市场编码：深市为 `48`，沪市为 `49`。
- 深市委托的 `order_index == orderorino`；正常成交的 `buy_id/sell_id` 可以直接关联该母单。
- 沪市成交的 `buy_id/sell_id` 关联委托记录的 `orderorino`，不能使用 `order_index` 作为母单 ID。
- 沪市 `A` 表示剩余委托进入订单簿，`D` 表示撤单；二者不是限价/市价类型。
- 沪市部分成交订单中，`A.biz_index` 等于最后一笔即时成交的 `biz_index + 1`。
- 深市撤单存在于成交流，编码为 `trade_type == 'C'`；沪市撤单存在于委托流，编码为 `order_type == 'D'`。
- 正常成交中 `bsflag` 的主动方向与订单号大小推断一致；`bsflag` 是主来源，订单号只作回退。
- `trade_price` 是价格乘以 10000，`trade_amount` 是元，`trade_volume` 是股。当前 `price` 与 `avg_price` 相差 10000 倍。
- 2022 年两路文件的本地到达顺序存在倒置，部分沪市 `A` 相对前置成交的到达延迟最高约 7.22 秒，因此不能将统一 3 秒 TTL 当作业务完成条件。

## 3. 研究口径

Builder 同时支持但严格区分以下口径：

1. `original_qty`：母单最初规模。沪市等于即时成交量加首次剩余挂单量；若无剩余委托，则等于到达即全部成交量。
2. `immediate_fill_qty`：母单进入市场时立即成交的数量。
3. `passive_fill_qty`：母单进入订单簿后作为被动方成交的数量。
4. `cancelled_qty`：母单撤销的数量。
5. `rest_qty`：当前仍在订单簿中的剩余数量。

精确状态必须满足：

```text
original_qty = immediate_fill_qty + passive_fill_qty + cancelled_qty + rest_qty
rest_qty >= 0
```

到达即全部成交的订单计入母单因子和主动成交因子，但不计入新增挂单因子。

## 4. 市场规则

### 4.1 深市

- 新委托：`order_type` 原样保存，规范母单 ID 为 `order_index`，`original_qty = rest_qty = order_volume`。
- 正常成交：按 `buy_id/sell_id` 查找双方母单，分别增加成交量并扣减 `rest_qty`。
- 撤单：`trade_type == 'C'`，非零的 `buy_id` 或 `sell_id` 是目标母单，`trade_volume` 是撤单量；该事件没有主动成交方向。
- 当 `rest_qty == 0` 时生成结束事件，并从活跃状态表移除。

### 4.2 沪市

- 规范母单 ID 始终为 `orderorino`；`order_index` 仅作为原始事件编号保留。
- 正常成交由 `bsflag` 确定进入市场的主动母单。主动方成交在母单确认前进入“在途状态”；另一方用于消耗已经存在的历史挂单。
- 收到 `A` 时，将 `biz_index < A.biz_index` 的同母单成交视为即时成交，`original_qty = immediate_fill_qty + order_volume`，`rest_qty = order_volume`。
- `biz_index > A.biz_index` 的成交属于后续被动成交，扣减 `rest_qty`。
- 没有收到 `A`且水位已越过的在途母单视为到达即全部成交，`original_qty = immediate_fill_qty`、`rest_qty = 0`。
- 收到 `D` 时，以 `orderorino` 找到目标母单，以 `order_volume` 扣减 `rest_qty`并增加 `cancelled_qty`。
- `A/D`只解释为事件动作。沪市 `price_instruction` 固定为 `Unknown`，不得将全部成交或全部沪市订单命名为市价单。

## 5. 到达乱序与完成水位

上游目前按本地时间合并成交和委托，并对成交流本地时间加 1 微秒。Builder 不假定回调顺序等于业务顺序，而是保存尚未确认母单的成交片段及其 `biz_index`。

Builder 维护单调的最大交易所事件时间，并使用可配置的 10 秒确认水位：

```text
safe_event_time = max_seen_event_time - 10 seconds
```

- 水位只用于确认“在途母单”的初始规模，不删除已有 `rest_qty` 的挂单。
- 收到 `A` 后仍保留相关即时成交片段，直到水位越过该母单的事件时间，再形成精确母单，防止较低 `biz_index` 的成交晚到。
- 在途母单在水位越过后仍无 `A`，形成 `CompletedByWatermark` 的到达即全成母单。
- 活跃挂单只在 `rest_qty == 0`、明确撤单完毕或收盘时移除，不使用静默 TTL。
- 收盘调用 `FlushAtClose()`，完成所有仍在途的母单并关闭剩余日内状态。
- 水位之后出现会改变已封账母单规模的迟到事件属于数据异常，记录计数并使当日质量门槛失败，不静默修正已输出因子。

10 秒是由已发现的 7.22 秒最大样本延迟加安全余量得到的初始值。后续 24 日数据审计可以提高该值，但不得低于实测最大延迟。

## 6. 状态与标准事件

Builder 内部区分：

- `PendingMotherOrder`：尚未完成初始规模确认的在途母单，保留按业务序号记录的成交片段。
- `ActiveRestingOrder`：已经确认 `original_qty`且 `rest_qty > 0` 的历史挂单。
- `CompletedOrder`：母单生命周期结束后的紧凑不可变记录。

Builder 向因子层输出：

1. `MotherOrderConfirmed`：完整母单规模首次确认。
2. `TradePairResolved`：一笔成交的买卖双方母单角色及规模均已确认。
3. `RestQtyChanged`：后续成交或撤单改变历史挂单。
4. `MotherOrderClosed`：`rest_qty` 归零或收盘封账。

`TradePairResolved` 同时携带买方母单、卖方母单、主动方向、本笔成交量、价格及业务序号，用于“大买吃小卖”等订单维度解耦因子。无法立即确认双方规模的成交先缓存，确认后释放。

## 7. 回调时机

完整事件在逐笔回调中产生和消费，不在因子输出回调中重建：

```text
DoOnAddTrans  -> Builder::OnTrans  -> ProcessBuilderOutput
DoOnAddOrder  -> Builder::OnOrder  -> ProcessBuilderOutput
DoOnGlobalTime/收盘 -> Builder::AdvanceWatermark/FlushAtClose
DoOnUpdateFactors -> 只读取已经累计的因子状态
```

每次进入 `OnTrans` 或 `OnOrder` 时，Builder 先判断当前事件是否延续某个在途母单，再封账不相关的旧在途母单，最后处理当前事件，避免把紧接着的 `A` 误判为“无剩余、全部成交”。

## 8. 价格与方向契约

内部保留定点价格，公开字段显式携带单位：

- `price_x1e4 = trade_price`
- `avg_price_x1e4 = filled_amount * 10000 / filled_volume`
- `filled_amount_yuan = trade_amount` 的累计值

如需要元/股，调用方显式除以 10000。现有含糊的 `price`、`avg_price` 不再作为新候选公开字段。

正常成交的主动方向：

1. `bsflag` 为 `B/S` 时直接使用；
2. `bsflag` 缺失且买卖订单号都有效时，以较大订单号为主动方；
3. 其他情况为 `Unknown`。

标准事件记录 `aggressor_source = BsFlag / OrderIdFallback / Unknown`。撤单不计算主动方向，只记录撤单方向。

## 9. 历史索引与内存

`rest_qty == 0` 后，母单从活跃表移除，但先输出 `MotherOrderClosed` 并写入紧凑的完成订单索引。完成索引保留研究所需的规模、成交、撤单、时间、方向和结束原因，不保留可变容器及临时片段。

所有因子列共享每只股票的一套 Builder。因子层根据标准事件维护滚动统计，不保存重复母单副本。

## 10. 失败处理与质量门槛

Builder 记录以下审计计数：

- 未知市场、未知事件类型；
- 沪市使用 `order_index` 无法与成交关联的错误尝试；
- `bsflag` 与订单号推断冲突；
- 迟到事件越过完成水位；
- 重复业务序号；
- `rest_qty < 0`；
- 精确状态不满足数量守恒；
- 被动成交找不到历史挂单；
- 收盘仍未解释的状态。

数量守恒、负剩余量、迟到改写和重复业务事件属于硬错误。单日出现硬错误时，技术检查失败，不允许进入历史生产。

## 11. 测试范围

专项 C++ 测试必须覆盖：

- 深市委托、正常成交、`C`撤单、`rest_qty`归零；
- 沪市纯挂单 `A`；
- 沪市一笔和多笔即时成交后收到 `A`；
- 沪市全部成交无 `A`；
- 沪市 `A` 先到但较低 `biz_index` 成交后到；
- 沪市后续被动成交与 `D`撤单；
- 沪市按 `orderorino`关联，明确证明 `order_index`不能代替；
- 买卖母单成对输出；
- `bsflag`优先、订单号回退及撤单无主动方；
- 价格与均价统一为 x1e4；
- 水位只关闭在途母单，不清理活跃挂单；
- 收盘封账和所有数量守恒不变量。

集成验证包括 `demofw00`专项测试、完整因子应用构建，以及真实 2022-02-22 的 `000001`、`600000` 单日回放统计。

## 12. 文档交付

实现完成后保留：

- 本设计文档；
- Builder API与字段单位说明；
- 沪深事件映射表；
- 因子接入示例，说明标准事件在 `DoOnAddTrans/DoOnAddOrder` 中消费；
- 测试命令、真实数据核验范围及已知边界。

## 13. 非目标

- 不把沪市全部成交订单推断为市价单；
- 不在 Builder 中实现具体的 500 个候选公式；
- 不改变研报评价门槛、股票池和收益评价工具；
- 不在本次工作中重构无关的行情、快照或Slurm逻辑。
