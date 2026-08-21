#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "app_factor/comm/raw_data_reader/raw_data_types/my_stock_order_new.h"
#include "app_factor/comm/raw_data_reader/raw_data_types/my_stock_transaction_new.h"
#include "factors/demofw00/tools/market/order_synthesizer.h"

namespace market = factors::demofw00::tools::market;

namespace {

template <typename Row>
bool ReadRows(const std::string& path, std::vector<Row>& rows) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        std::cerr << "cannot open NPQ file: " << path << '\n';
        return false;
    }
    const std::streamoff bytes = input.tellg();
    if (bytes < 0 || bytes % static_cast<std::streamoff>(sizeof(Row)) != 0) {
        std::cerr << "NPQ record size mismatch: path=" << path
                  << " bytes=" << bytes << " record_size=" << sizeof(Row)
                  << '\n';
        return false;
    }
    rows.resize(static_cast<size_t>(bytes / sizeof(Row)));
    input.seekg(0);
    if (!rows.empty()) {
        input.read(reinterpret_cast<char*>(rows.data()), bytes);
    }
    if (!input) {
        std::cerr << "cannot read complete NPQ file: " << path << '\n';
        return false;
    }
    return true;
}

std::string BasenameWithoutExtension(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    const size_t begin = slash == std::string::npos ? 0 : slash + 1;
    const size_t dot = path.find_last_of('.');
    const size_t end = dot == std::string::npos || dot < begin
                           ? path.size()
                           : dot;
    return path.substr(begin, end - begin);
}

std::string FindDate(const std::string& path) {
    for (size_t i = 0; i + 8 <= path.size(); ++i) {
        bool digits = true;
        for (size_t j = 0; j < 8; ++j) {
            digits = digits && path[i + j] >= '0' && path[i + j] <= '9';
        }
        if (digits && (i == 0 || path[i - 1] == '/') &&
            (i + 8 == path.size() || path[i + 8] == '/')) {
            return path.substr(i, 8);
        }
    }
    return "unknown";
}

int ToMillis(int int_time) {
    if (int_time <= 0) {
        return 0;
    }
    const int hh = (int_time / 10000000) % 100;
    const int mm = (int_time / 100000) % 100;
    const int ss = (int_time / 1000) % 100;
    const int ms = int_time % 1000;
    return (((hh * 60) + mm) * 60 + ss) * 1000 + ms;
}

struct ReplayStats {
    uint64_t input_trans{0};
    uint64_t input_orders{0};
    uint64_t input_cancels{0};
    uint64_t confirmed{0};
    uint64_t closed{0};
    uint64_t trade_pairs{0};
    uint64_t negative_rest{0};
    uint64_t conservation_failures{0};
    uint64_t late_events{0};
    size_t max_active{0};
    size_t max_pending{0};
    std::unordered_set<int64_t> active;
    std::unordered_set<int64_t> pending;
    std::unordered_set<int64_t> completed;
    std::unordered_map<int64_t, int> completed_time;
    std::vector<std::string> late_event_samples;

    void NotePending(uint8_t market, int64_t order_id) {
        if (market == 49 && order_id > 0 && active.count(order_id) == 0 &&
            completed.count(order_id) == 0) {
            pending.insert(order_id);
            max_pending = std::max(max_pending, pending.size());
        }
    }

    void NoteInput(uint8_t market,
                   int64_t order_id,
                   const char* event_kind,
                   int int_time,
                   char bsflag = 0) {
        if (market == 49 && order_id > 0 && completed.count(order_id) != 0) {
            ++late_events;
            if (late_event_samples.size() < 10) {
                late_event_samples.push_back(
                    std::string(event_kind) + ":order_id=" +
                    std::to_string(order_id) + ":int_time=" +
                    std::to_string(int_time) + ":closed_at=" +
                    std::to_string(completed_time[order_id]) +
                    ":bsflag=" + (bsflag == 0 ? "-" :
                                      std::string(1, bsflag)));
            }
        }
    }

    void Consume(const market::BuilderOutput& output) {
        trade_pairs += output.trade_pairs.size();
        for (const market::OrderEvent& event : output.events) {
            const int64_t rest = static_cast<int64_t>(event.rest_qty);
            if (rest < 0) {
                ++negative_rest;
            }
            if (event.original_qty != event.immediate_fill_qty +
                                          event.passive_fill_qty +
                                          event.cancelled_qty + rest) {
                ++conservation_failures;
            }
            if (event.kind == market::BuilderEventKind::MotherOrderConfirmed) {
                ++confirmed;
                pending.erase(event.order_id);
                if (rest > 0) {
                    active.insert(event.order_id);
                }
            } else if (event.kind ==
                       market::BuilderEventKind::RestQtyChanged) {
                if (rest > 0) {
                    active.insert(event.order_id);
                } else {
                    active.erase(event.order_id);
                }
            } else if (event.kind ==
                       market::BuilderEventKind::MotherOrderClosed) {
                ++closed;
                active.erase(event.order_id);
                pending.erase(event.order_id);
                completed.insert(event.order_id);
                completed_time[event.order_id] = event.last_time;
            }
        }
        max_active = std::max(max_active, active.size());
        max_pending = std::max(max_pending, pending.size());
    }
};

bool TransactionComesFirst(const my_book_stock_transaction_new& trans,
                           const my_book_stock_order_new& order) {
    if (trans.local_time != order.local_time) {
        return trans.local_time < order.local_time;
    }
    if (trans.exchange_time != order.exchange_time) {
        return trans.exchange_time < order.exchange_time;
    }
    return trans.serial <= order.serial;
}

double MaxShanghaiAPredecessorArrivalLagMs(
    const std::vector<my_book_stock_transaction_new>& transactions,
    const std::vector<my_book_stock_order_new>& orders) {
    struct TradeArrival {
        uint64_t local_time{0};
        int64_t buy_id{0};
        int64_t sell_id{0};
    };
    std::unordered_map<int64_t, TradeArrival> by_biz_index;
    by_biz_index.reserve(transactions.size());
    for (const auto& row : transactions) {
        if (row.quote.market == 49 && row.quote.biz_index > 0) {
            by_biz_index[row.quote.biz_index] =
                TradeArrival{row.local_time, row.quote.buy_id,
                             row.quote.sell_id};
        }
    }
    uint64_t max_lag_us = 0;
    for (const auto& row : orders) {
        if (row.quote.market != 49 || row.quote.order_type != 'A' ||
            row.quote.biz_index == 0 || row.quote.orderorino == 0) {
            continue;
        }
        const auto it = by_biz_index.find(
            static_cast<int64_t>(row.quote.biz_index) - 1);
        if (it == by_biz_index.end()) {
            continue;
        }
        const int64_t order_id = static_cast<int64_t>(row.quote.orderorino);
        if (it->second.buy_id != order_id && it->second.sell_id != order_id) {
            continue;
        }
        if (it->second.local_time > row.local_time) {
            max_lag_us = std::max(max_lag_us,
                                  it->second.local_time - row.local_time);
        }
    }
    return static_cast<double>(max_lag_us) / 1000.0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: " << argv[0] << " <trans.npq> <order.npq>\n";
        return 2;
    }

    static_assert(sizeof(my_stock_order_new) ==
                      sizeof(Stock_Order_Internal_Book_New),
                  "order NPQ and SDP layouts diverged");
    static_assert(sizeof(my_stock_transaction_new) ==
                      sizeof(Stock_Transaction_Internal_Book_New),
                  "transaction NPQ and SDP layouts diverged");

    const std::string trans_path = argv[1];
    const std::string order_path = argv[2];
    const char* trace_env = std::getenv("ORDER_BUILDER_TRACE_ID");
    const int64_t trace_id = trace_env == nullptr ? 0 : std::atoll(trace_env);
    const char* watermark_env = std::getenv("ORDER_BUILDER_WATERMARK_MS");
    const int watermark_ms = watermark_env == nullptr
                                 ? 20000
                                 : std::max(1, std::atoi(watermark_env));
    std::vector<my_book_stock_transaction_new> transactions;
    std::vector<my_book_stock_order_new> orders;
    if (!ReadRows(trans_path, transactions) || !ReadRows(order_path, orders)) {
        return 2;
    }
    const double max_a_predecessor_arrival_lag_ms =
        MaxShanghaiAPredecessorArrivalLagMs(transactions, orders);

    market::OrderSynthesizer builder(watermark_ms);
    ReplayStats stats;
    size_t trans_index = 0;
    size_t order_index = 0;
    int last_watermark_ms = -1;
    int last_event_time = 0;
    uint8_t market_id = 0;
    std::chrono::steady_clock::duration builder_elapsed{};

    const auto consume_builder_call = [&](const auto& call) {
        const auto call_started = std::chrono::steady_clock::now();
        const market::BuilderOutput& output = call();
        builder_elapsed += std::chrono::steady_clock::now() - call_started;
        stats.Consume(output);
    };

    const auto started = std::chrono::steady_clock::now();
    while (trans_index < transactions.size() || order_index < orders.size()) {
        const bool take_trans =
            order_index == orders.size() ||
            (trans_index < transactions.size() &&
             TransactionComesFirst(transactions[trans_index],
                                   orders[order_index]));
        if (take_trans) {
            const my_book_stock_transaction_new& row =
                transactions[trans_index++];
            const auto& quote = *reinterpret_cast<
                const Stock_Transaction_Internal_Book_New*>(
                static_cast<const void*>(&row.quote));
            market_id = quote.market;
            if (trace_id > 0 &&
                (quote.buy_id == trace_id || quote.sell_id == trace_id)) {
                std::cerr << "trace=trans local_time=" << row.local_time
                          << " int_time=" << quote.int_time
                          << " biz_index=" << quote.biz_index
                          << " buy_id=" << quote.buy_id
                          << " sell_id=" << quote.sell_id
                          << " volume=" << quote.trade_volume
                          << " amount=" << quote.trade_amount
                          << " bsflag=" << quote.bsflag
                          << " trade_type=" << quote.trade_type << '\n';
            }
            ++stats.input_trans;
            if (quote.trade_type == 'C') {
                ++stats.input_cancels;
            }
            stats.NoteInput(quote.market, quote.buy_id, "trans-buy",
                            quote.int_time, quote.bsflag);
            stats.NoteInput(quote.market, quote.sell_id, "trans-sell",
                            quote.int_time, quote.bsflag);
            if (quote.trade_type != 'C') {
                stats.NotePending(quote.market, quote.buy_id);
                stats.NotePending(quote.market, quote.sell_id);
            }
            consume_builder_call([&]() -> const market::BuilderOutput& {
                return builder.OnTrans(quote);
            });
            last_event_time = quote.int_time;
        } else {
            const my_book_stock_order_new& row = orders[order_index++];
            const auto& quote =
                *reinterpret_cast<const Stock_Order_Internal_Book_New*>(
                    static_cast<const void*>(&row.quote));
            market_id = quote.market;
            const int64_t canonical_id =
                market::OrderSynthesizer::CanonicalOrderId(quote);
            if (trace_id > 0 && canonical_id == trace_id) {
                std::cerr << "trace=order local_time=" << row.local_time
                          << " int_time=" << quote.int_time
                          << " biz_index=" << quote.biz_index
                          << " order_index=" << quote.order_index
                          << " orderorino=" << quote.orderorino
                          << " volume=" << quote.order_volume
                          << " bsflag=" << quote.bsflag
                          << " order_type=" << quote.order_type << '\n';
            }
            ++stats.input_orders;
            if (quote.market == 49 && quote.order_type == 'D') {
                ++stats.input_cancels;
            }
            stats.NoteInput(quote.market, canonical_id,
                            quote.order_type == 'D' ? "order-D" : "order-A",
                            quote.int_time);
            if (quote.market == 49 && quote.order_type == 'A') {
                stats.NotePending(quote.market, canonical_id);
            }
            consume_builder_call([&]() -> const market::BuilderOutput& {
                return builder.OnOrder(quote);
            });
            last_event_time = quote.int_time;
        }

        const int current_ms = ToMillis(last_event_time);
        if (current_ms >= last_watermark_ms + 1000) {
            consume_builder_call([&]() -> const market::BuilderOutput& {
                return builder.AdvanceWatermark(last_event_time);
            });
            last_watermark_ms = current_ms;
        }
    }
    const auto close_started = std::chrono::steady_clock::now();
    const market::BuilderOutput& close_output =
        builder.FlushAtClose(150000000);
    const auto close_finished = std::chrono::steady_clock::now();
    stats.Consume(close_output);
    const auto finished = std::chrono::steady_clock::now();

    const uint64_t inputs = stats.input_trans + stats.input_orders;
    const double wall_seconds =
        std::chrono::duration<double>(finished - started).count();
    const double seconds =
        std::chrono::duration<double>(builder_elapsed).count();
    const double close_seconds =
        std::chrono::duration<double>(close_finished - close_started).count();
    const double events_per_second =
        seconds > 0.0 ? static_cast<double>(inputs) / seconds : 0.0;
    const uint64_t hard_errors = stats.negative_rest +
                                 stats.conservation_failures +
                                 stats.late_events;

    std::cout << "date=" << FindDate(trans_path)
              << " symbol=" << BasenameWithoutExtension(trans_path)
              << " market=" << static_cast<int>(market_id) << '\n'
              << "input_trans=" << stats.input_trans
              << " input_orders=" << stats.input_orders
              << " input_cancels=" << stats.input_cancels << '\n'
              << "confirmed=" << stats.confirmed
              << " closed=" << stats.closed
              << " trade_pairs=" << stats.trade_pairs << '\n'
              << "active=" << stats.active.size()
              << " pending=" << stats.pending.size()
              << " completed=" << builder.CompletedOrderCount()
              << " max_active=" << stats.max_active
              << " max_pending=" << stats.max_pending << '\n'
              << "hard_errors=" << hard_errors
              << " negative_rest=" << stats.negative_rest
              << " conservation_failures=" << stats.conservation_failures
              << " late_events=" << stats.late_events << '\n'
              << std::fixed << std::setprecision(6)
              << "elapsed_seconds=" << seconds
              << " close_seconds=" << close_seconds
              << " wall_seconds=" << wall_seconds
              << " events_per_second=" << events_per_second << '\n'
              << "max_a_predecessor_arrival_lag_ms="
              << max_a_predecessor_arrival_lag_ms
              << " watermark_ms=" << watermark_ms << '\n';

    for (const std::string& sample : stats.late_event_samples) {
        std::cerr << "late_event_sample=" << sample << '\n';
    }

    return hard_errors == 0 ? 0 : 3;
}
