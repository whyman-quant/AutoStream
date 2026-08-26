#pragma once

#include <deque>

#include "factors/_comm/factor_entry_base.h"
#include "factors/_comm/factor_entry_registry.h"
#include "factors/flow_pressure/meta_config.h"

namespace factors {
namespace flow_pressure {

class FactorEntry : public comm::FactorEntryBase {
public:
    FactorEntry(const std::string& asset,
                const comm::FactorMetadata& metadata,
                const comm::FactorEntryConfig& config);

private:
    void DoOnAddQuote(const Stock_Internal_Book& quote) override;
    void DoOnAddTrans(const Stock_Transaction_Internal_Book_New& trade) override;
    void DoOnAddOrder(const Stock_Order_Internal_Book_New& order) override;
    void DoOnUpdateFactors(int64_t timestamp) override;

    static const size_t kMaxHistoryEvents = 130;
    std::deque<double> signed_volumes_;
};

}  // namespace flow_pressure
}  // namespace factors

REGISTER_FACTOR_AUTO(flow_pressure, FactorEntry)
