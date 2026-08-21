#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "sdp_handler/quote_format_define.h"

namespace factors {
namespace demofw00 {
namespace tools {
namespace market {

enum class AggressorSide : int8_t {
    Unknown = 0,
    Buy = 1,
    Sell = -1,
};

enum class AggressorSource : uint8_t {
    Unknown = 0,
    BsFlag = 1,
    OrderIdFallback = 2,
};

enum class BuilderEventKind : uint8_t {
    MotherOrderConfirmed = 0,
    TradePairResolved = 1,
    RestQtyChanged = 2,
    MotherOrderClosed = 3,
};

enum class CloseReason : uint8_t {
    None = 0,
    FullyFilledOnArrival = 1,
    FullyFilledAfterResting = 2,
    Cancelled = 3,
    SessionClose = 4,
};

struct OrderEvent {
    BuilderEventKind kind{BuilderEventKind::RestQtyChanged};
    CloseReason close_reason{CloseReason::None};

    // === 基础信息 ===
    int64_t order_id{0};
    int64_t raw_event_id{0};
    uint8_t market{0};
    uint16_t channel{0};
    int side{0};                         // 0=Buy, 1=Sell
    int order_type{2};
    char raw_order_type{0};
    int first_time{0};                   // 首次出现时间
    int last_time{0};                    // 最后更新时间

    // === 核心重构字段 ===
    double original_order_volume{0.0};   // 原始委托总量（重构目标）
    double rest_qty{0.0};                // 剩余未成交量（来自 OnOrder）
    double filled_volume{0.0};           // 累计成交量
    double filled_amount{0.0};           // 累计成交金额

    // === 明确单位与生命周期字段 ===
    int64_t original_qty{0};
    int64_t immediate_fill_qty{0};
    int64_t passive_fill_qty{0};
    int64_t filled_qty{0};
    int64_t cancelled_qty{0};
    int64_t filled_amount_yuan{0};
    int64_t price_x1e4{0};
    int64_t avg_price_x1e4{0};

    // === 增量信息 ===
    double delta_volume{0.0};            // 本次成交增量
    double delta_amount{0.0};            // 本次成交金额增量
    double price{0.0};                   // 本次成交价（来自 trade_price）
    double avg_price{0.0};               // 累计成交均价

    // === 状态标记 ===
    bool has_order{false};               // 是否收到过 OnOrder
    bool is_first_fill{false};           // 是否首次成交
    bool estimated{false};               // 🆕 标记委托量是否为估计值
    AggressorSide aggressor_side{AggressorSide::Unknown};
};

struct OrderSnapshot {
    int64_t order_id{0};
    int64_t original_qty{0};
    int64_t immediate_fill_qty{0};
    int64_t passive_fill_qty{0};
    int64_t cancelled_qty{0};
    int64_t rest_qty{0};
    int64_t filled_amount_yuan{0};
    int first_time{0};
    int last_time{0};
    uint16_t channel{0};
    uint8_t market{0};
    int8_t side{0};
};

struct TradePairEvent {
    int64_t trade_index{0};
    int64_t biz_index{0};
    int64_t trade_volume{0};
    int64_t trade_amount_yuan{0};
    int64_t price_x1e4{0};
    int64_t avg_price_x1e4{0};
    int int_time{0};
    AggressorSide aggressor_side{AggressorSide::Unknown};
    AggressorSource aggressor_source{AggressorSource::Unknown};
    OrderSnapshot buy_order;
    OrderSnapshot sell_order;
};

struct BuilderOutput {
    std::vector<OrderEvent> events;
    std::vector<TradePairEvent> trade_pairs;
    AggressorSide aggressor_side{AggressorSide::Unknown};
    AggressorSource aggressor_source{AggressorSource::Unknown};
    int64_t price_x1e4{0};
    int64_t avg_price_x1e4{0};

    void Clear() {
        events.clear();
        trade_pairs.clear();
        aggressor_side = AggressorSide::Unknown;
        aggressor_source = AggressorSource::Unknown;
        price_x1e4 = 0;
        avg_price_x1e4 = 0;
    }
};

struct CompletedOrderRecord {
    int64_t order_id{0};
    int64_t original_qty{0};
    int64_t immediate_fill_qty{0};
    int64_t passive_fill_qty{0};
    int64_t cancelled_qty{0};
    int64_t filled_amount_yuan{0};
    int first_time{0};
    int last_time{0};
    uint16_t channel{0};
    uint8_t market{0};
    int8_t side{0};
    CloseReason close_reason{CloseReason::None};
};

class OrderSynthesizer {
public:
    // Real 2022 replay shows that a 15-second exchange-time watermark can run
    // ahead of locally delayed A/immediate-fill sequences. Twenty seconds is
    // the minimum validated safe tier. It never expires active rest qty.
    explicit OrderSynthesizer(int ttl_ms = 20000);

    // Returned reference is valid until the next Builder call.
    const BuilderOutput& OnOrder(const Stock_Order_Internal_Book_New& quote);
    const BuilderOutput& OnTrans(const Stock_Transaction_Internal_Book_New& quote);
    const BuilderOutput& AdvanceWatermark(int int_time);
    const BuilderOutput& FlushAtClose(int int_time);
    static int64_t CanonicalOrderId(
        const Stock_Order_Internal_Book_New& quote);
    bool HasOrder(uint8_t market, uint16_t channel, int64_t order_id) const;
    size_t CompletedOrderCount() const { return completed_order_count_; }
    void Cleanup(int int_time);
    void SnapshotStats(int64_t& seen_orders,
                       int64_t& synth_events,
                       int64_t& filtered_events) const;
    void AddFilteredEvent();

private:
    // Cleanup interval: only run O(n) cleanup every 1 second
    static constexpr int kCleanupIntervalMs = 1000;

    struct TradeFragment {
        int64_t biz_index{0};
        int64_t volume{0};
        int64_t amount_yuan{0};
        int64_t price_x1e4{0};
        int int_time{0};
        bool incoming{false};
    };

    struct OrderState {
        int64_t order_id{0};
        int64_t raw_event_id{0};
        uint8_t market{0};
        uint16_t channel{0};
        int side{0};
        int order_type{2};
        char raw_order_type{0};
        int first_time{0};
        int last_time{0};
        int last_time_ms{0};
        bool has_init{false};

        // === 核心重构字段 ===
        double original_order_volume{0.0};   // 原始委托总量（重构后锁定）
        double rest_qty{0.0};                // 剩余未成交量（来自 OnOrder）
        double filled_volume{0.0};           // 累计成交量
        double filled_amount{0.0};           // 累计成交金额
        double last_emitted_volume{0.0};     // 上次 emit 的累计成交量
        double last_emitted_amount{0.0};     // 上次 emit 的累计成交金额

        // === 状态标记 ===
        bool has_order{false};               // 是否收到过 OnOrder
        bool volume_locked{false};           // original_order_volume 是否已锁定
        bool estimated{false};               // 🆕 标记委托量是否为估计值

        int64_t original_qty{0};
        int64_t immediate_fill_qty{0};
        int64_t passive_fill_qty{0};
        int64_t cancelled_qty{0};
        int64_t filled_amount_yuan{0};
        int64_t rest_qty_exact{0};
        bool initial_confirmed{false};

        int64_t a_biz_index{0};
        int64_t a_rest_qty{0};
        int max_event_time_ms{0};
        int scheduled_event_time_ms{0};
        bool has_a{false};
        bool has_incoming_fragment{false};
        bool finalization_queued{false};
        std::vector<TradeFragment> pending_trades;
    };

    struct PendingTradePair {
        TradePairEvent event;
        int64_t buy_id{0};
        int64_t sell_id{0};
        bool has_buy{false};
        bool has_sell{false};
        bool active{false};
    };

    struct PairWaiterList {
        size_t first{0};
        bool has_first{false};
        std::vector<size_t> additional;

        void Push(size_t token) {
            if (!has_first) {
                first = token;
                has_first = true;
            } else {
                additional.push_back(token);
            }
        }
    };

    int ttl_ms_{3000};
    int last_cleanup_ms_{0};  // Last cleanup timestamp
    std::unordered_map<int64_t, OrderState> states_;
    using PendingDeadline = std::pair<int, int64_t>;
    // Lazy min-heap: only due Shanghai mothers participate in watermark work.
    // Stale entries are discarded by comparing their event time with state.
    std::priority_queue<PendingDeadline,
                        std::vector<PendingDeadline>,
                        std::greater<PendingDeadline>> pending_deadlines_;
    int64_t seen_order_count_{0};
    size_t completed_order_count_{0};
    // Stable slots avoid one heap allocation and one hash-node deletion for
    // every Shanghai trade. Resolved slots are recycled immediately.
    std::vector<PendingTradePair> pending_pairs_;
    std::vector<size_t> free_pair_slots_;
    std::unordered_map<int64_t, PairWaiterList> pair_waiters_;
    BuilderOutput output_buffer_;
    int64_t synth_events_{0};
    int64_t filtered_events_{0};

    static int ToMillis(int int_time);
    static AggressorSide ResolveAggressorSide(
        const Stock_Transaction_Internal_Book_New& quote,
        AggressorSource& source);
    static double SafeDiv(double a, double b, double def = 0.0);
    void AppendEvent(BuilderEventKind kind,
                     CloseReason close_reason,
                     const OrderState& state,
                     int64_t delta_qty,
                     int64_t delta_amount_yuan,
                     int64_t price_x1e4,
                     AggressorSide aggressor_side);
    void UpdateShenzhenTradeSide(
        int64_t order_id,
        int side,
        const Stock_Transaction_Internal_Book_New& quote,
        AggressorSide aggressor_side);
    void AddShanghaiTradeSide(
        int64_t order_id,
        int side,
        const Stock_Transaction_Internal_Book_New& quote,
        AggressorSide aggressor_side);
    void FinalizeShanghaiPending(int safe_event_time_ms);
    static OrderSnapshot MakeSnapshot(const OrderState& state);
    static OrderSnapshot MakeSnapshot(const OrderEvent& event);
    bool FindSnapshot(int64_t order_id, OrderSnapshot& snapshot) const;
    void ResolveWaitingPairs(int64_t order_id,
                             const OrderSnapshot& snapshot);
    void QueueOrEmitTradePair(
        const Stock_Transaction_Internal_Book_New& quote,
        AggressorSide aggressor_side,
        AggressorSource aggressor_source);
    void UpdateOne(int64_t order_id, int side,
                   const Stock_Transaction_Internal_Book_New& quote,
                   AggressorSide aggressor_side,
                   std::vector<OrderEvent>& events);
};

}  // namespace market
}  // namespace tools
}  // namespace demofw00
}  // namespace factors
