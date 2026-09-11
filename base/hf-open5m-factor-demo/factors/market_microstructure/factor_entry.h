#pragma once

#include <cstdint>
#include <limits>

#include "factors/_comm/factor_entry_base.h"
#include "factors/_comm/factor_entry_registry.h"
#include "factors/market_microstructure/meta_config.h"

namespace factors {
namespace market_microstructure {

class FactorEntry : public comm::FactorEntryBase {
public:
    FactorEntry(const std::string& asset, const comm::FactorMetadata& metadata,
                const comm::FactorEntryConfig& config);
    std::vector<bool> GetReadinessMask(int64_t timestamp) const override;
    std::vector<unsigned char> GetReadinessReasonCodes(int64_t timestamp) const override;

private:
    void DoOnAddQuote(const Stock_Internal_Book& quote) override;
    void DoOnAddTrans(const Stock_Transaction_Internal_Book_New&) override {}
    void DoOnAddOrder(const Stock_Order_Internal_Book_New&) override {}
    void DoOnUpdateFactors(int64_t timestamp) override;

    Stock_Internal_Book last_quote_{};
    bool has_quote_{false};
    bool has_computed_{false};
};

}  // namespace market_microstructure
}  // namespace factors

REGISTER_FACTOR_AUTO(market_microstructure, FactorEntry)
