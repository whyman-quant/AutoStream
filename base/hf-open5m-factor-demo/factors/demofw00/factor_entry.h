#pragma once

#include <memory>

#include "factors/_comm/factor_entry_base.h"
#include "factors/_comm/factor_entry_registry.h"
#include "factors/demofw00/meta_config.h"

namespace factors {
namespace demofw00 {

class FactorEntry : public comm::FactorEntryBase {
public:
    FactorEntry(const std::string& asset,
                const comm::FactorMetadata& metadata,
                const comm::FactorEntryConfig& config);
    ~FactorEntry() override;

private:
    void DoOnAddQuote(const Stock_Internal_Book& quote) override;
    void DoOnAddTrans(const Stock_Transaction_Internal_Book_New& quote) override;
    void DoOnAddOrder(const Stock_Order_Internal_Book_New& quote) override;
    void DoOnGlobalTime(int exch_time) override;
    void DoOnUpdateFactors(int64_t timestamp) override;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace demofw00
}  // namespace factors

REGISTER_FACTOR_AUTO(demofw00, FactorEntry)
