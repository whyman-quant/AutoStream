#pragma once

#include "factors/_comm/factor_entry_base.h"
#include "factors/_comm/factor_entry_registry.h"
#include "factors/book_imbalance/meta_config.h"

namespace factors {
namespace book_imbalance {

class FactorEntry : public comm::FactorEntryBase {
public:
    FactorEntry(const std::string& asset,
                const comm::FactorMetadata& metadata,
                const comm::FactorEntryConfig& config);

private:
    void DoOnAddQuote(const Stock_Internal_Book& quote) override;
    void DoOnAddTrans(const Stock_Transaction_Internal_Book_New& quote) override;
    void DoOnAddOrder(const Stock_Order_Internal_Book_New& quote) override;
    void DoOnUpdateFactors(int64_t timestamp) override;

    Stock_Internal_Book last_quote_{};
    int64_t quote_count_{0};
    bool has_quote_{false};
};

}  // namespace book_imbalance
}  // namespace factors

REGISTER_FACTOR_AUTO(book_imbalance, FactorEntry)
