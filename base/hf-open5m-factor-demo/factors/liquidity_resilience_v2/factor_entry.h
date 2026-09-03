#pragma once
#include <deque>
#include "factors/_comm/factor_entry_base.h"
#include "factors/_comm/factor_entry_registry.h"
#include "factors/liquidity_resilience_v2/meta_config.h"

namespace factors { namespace liquidity_resilience_v2 {
class FactorEntry : public comm::FactorEntryBase {
public:
    FactorEntry(const std::string& asset, const comm::FactorMetadata& metadata,
                const comm::FactorEntryConfig& config);
    std::vector<bool> GetReadinessMask(int64_t timestamp) const override;
private:
    void DoOnAddQuote(const Stock_Internal_Book&) override;
    void DoOnAddTrans(const Stock_Transaction_Internal_Book_New&) override {}
    void DoOnAddOrder(const Stock_Order_Internal_Book_New&) override {}
    void DoOnUpdateFactors(int64_t) override;
    static const size_t kMaxHistoryEvents = 130;
    std::deque<double> l1_, l5_;
    bool current_valid_{false};
};
} }
REGISTER_FACTOR_AUTO(liquidity_resilience_v2, FactorEntry)
