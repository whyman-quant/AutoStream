#pragma once

#include <deque>

#include "factors/_comm/factor_entry_base.h"
#include "factors/_comm/factor_entry_registry.h"
#include "factors/liquidity_resilience/meta_config.h"

namespace factors {
namespace liquidity_resilience {

class FactorEntry : public comm::FactorEntryBase {
public:
    FactorEntry(const std::string& asset, const comm::FactorMetadata& metadata, const comm::FactorEntryConfig& config);

private:
    void DoOnAddQuote(const Stock_Internal_Book& quote) override;
    void DoOnAddTrans(const Stock_Transaction_Internal_Book_New& trade) override;
    void DoOnAddOrder(const Stock_Order_Internal_Book_New& order) override;
    void DoOnUpdateFactors(int64_t timestamp) override;

    static const size_t kWindowEvents = 16;
    std::deque<double> liquidity_history_;
    double current_liquidity_{0.0};
    bool current_valid_{false};
};

}  // namespace liquidity_resilience
}  // namespace factors

REGISTER_FACTOR_AUTO(liquidity_resilience, FactorEntry)
