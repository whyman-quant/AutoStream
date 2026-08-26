#include "factors/flow_pressure/factor_entry.h"

#include <cmath>
#include <iostream>
#include <vector>

namespace {

Stock_Transaction_Internal_Book_New Trade(char side, int64_t volume, char type = '0') {
    Stock_Transaction_Internal_Book_New trade{};
    trade.bsflag = side;
    trade.trade_volume = volume;
    trade.trade_type = type;
    trade.trade_price = 10000;
    return trade;
}

bool Near(double actual, double expected) {
    return std::abs(actual - expected) <= 1e-12;
}

double Value(factors::flow_pressure::FactorEntry& entry, int64_t timestamp = 100000000) {
    return entry.UpdateFactors(timestamp).at(0);
}

std::vector<double> Values(
    factors::flow_pressure::FactorEntry& entry, int64_t timestamp = 100000000) {
    return entry.UpdateFactors(timestamp);
}

}  // namespace

int main() {
    factors::comm::FactorEntryConfig config;
    const std::vector<std::string> expected_names = {
        "flow_pressure_signed_trade_flow_w16",
        "flow_pressure_signed_trade_flow_w32_lag0",
        "flow_pressure_signed_trade_flow_w64_lag1",
        "flow_pressure_signed_trade_flow_w128_lag2",
        "flow_pressure_trade_flow_zscore_w16_lag0",
        "flow_pressure_trade_flow_zscore_w32_lag0",
        "flow_pressure_trade_flow_zscore_w64_lag1",
        "flow_pressure_trade_flow_zscore_w128_lag2",
        "flow_pressure_decayed_trade_flow_w16_lag0",
        "flow_pressure_decayed_trade_flow_w32_lag0",
        "flow_pressure_decayed_trade_flow_w64_lag1",
        "flow_pressure_decayed_trade_flow_w128_lag2",
    };
    if (factors::flow_pressure::GetMetadata().factor_names != expected_names) {
        std::cerr << "metadata must expose the frozen twelve-candidate order" << std::endl;
        return 1;
    }

    factors::flow_pressure::FactorEntry buy(
        "000001", factors::flow_pressure::GetMetadata(), config);
    buy.AddTrans(Trade('B', 100));
    if (!Near(Value(buy), 1.0)) {
        std::cerr << "one buy must produce +1" << std::endl;
        return 1;
    }

    factors::flow_pressure::FactorEntry sell(
        "000001", factors::flow_pressure::GetMetadata(), config);
    sell.AddTrans(Trade('S', 100));
    if (!Near(Value(sell), -1.0)) {
        std::cerr << "one sell must produce -1" << std::endl;
        return 1;
    }

    factors::flow_pressure::FactorEntry balanced(
        "000001", factors::flow_pressure::GetMetadata(), config);
    balanced.AddTrans(Trade('B', 50));
    balanced.AddTrans(Trade('S', 50));
    if (!Near(Value(balanced), 0.0)) {
        std::cerr << "balanced flow must produce zero" << std::endl;
        return 1;
    }

    factors::flow_pressure::FactorEntry lagged(
        "000001", factors::flow_pressure::GetMetadata(), config);
    lagged.AddTrans(Trade('B', 100));
    lagged.AddTrans(Trade('S', 100));
    const auto lagged_values = Values(lagged);
    if (!Near(lagged_values.at(0), 0.0) || !Near(lagged_values.at(1), 0.0) ||
        !Near(lagged_values.at(2), 1.0) || !Near(lagged_values.at(3), 0.0)) {
        std::cerr << "lagged signed-flow variants do not honor their event lag" << std::endl;
        return 1;
    }
    if (!(lagged_values.at(8) < 0.0) || !std::isfinite(lagged_values.at(4))) {
        std::cerr << "decay and zscore variants are not finite or time-directed" << std::endl;
        return 1;
    }

    factors::flow_pressure::FactorEntry ignored(
        "000001", factors::flow_pressure::GetMetadata(), config);
    ignored.AddTrans(Trade('B', 100, 'C'));
    ignored.AddTrans(Trade('B', 0));
    ignored.AddTrans(Trade('X', 100));
    if (!Near(Value(ignored), 0.0) || !std::isfinite(Value(ignored))) {
        std::cerr << "cancel, invalid side, and zero volume must be ignored" << std::endl;
        return 1;
    }

    factors::flow_pressure::FactorEntry bounded(
        "000001", factors::flow_pressure::GetMetadata(), config);
    bounded.AddTrans(Trade('S', 100));
    for (int index = 0; index < 16; ++index) bounded.AddTrans(Trade('B', 10));
    if (!Near(Value(bounded), 1.0)) {
        std::cerr << "seventeenth trade must evict the oldest trade" << std::endl;
        return 1;
    }

    factors::flow_pressure::FactorEntry left(
        "000001", factors::flow_pressure::GetMetadata(), config);
    factors::flow_pressure::FactorEntry right(
        "000001", factors::flow_pressure::GetMetadata(), config);
    std::vector<double> left_prefix;
    std::vector<double> right_prefix;
    for (int index = 0; index < 8; ++index) {
        const auto trade = Trade(index % 3 == 0 ? 'S' : 'B', 10 + index);
        left.AddTrans(trade);
        right.AddTrans(trade);
        left_prefix.push_back(Value(left, 100000000 + index));
        right_prefix.push_back(Value(right, 100000000 + index));
    }
    right.AddTrans(Trade('S', 1000000));
    if (left_prefix != right_prefix) {
        std::cerr << "future trades changed an already observed prefix" << std::endl;
        return 1;
    }

    factors::flow_pressure::FactorEntry next_day(
        "000001", factors::flow_pressure::GetMetadata(), config);
    if (!Near(Value(next_day), 0.0)) {
        std::cerr << "new trading-day instance inherited prior state" << std::endl;
        return 1;
    }
    return 0;
}
