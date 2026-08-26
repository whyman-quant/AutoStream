#pragma once
#include <deque>
#include "factors/_comm/factor_entry_base.h"
#include "factors/_comm/factor_entry_registry.h"
#include "factors/impact_efficiency/meta_config.h"
namespace factors { namespace impact_efficiency {
class FactorEntry: public comm::FactorEntryBase {
public: FactorEntry(const std::string& asset,const comm::FactorMetadata& metadata,const comm::FactorEntryConfig& config);
 struct TradeState { double price; double signed_volume; };
private: void DoOnAddQuote(const Stock_Internal_Book&) override; void DoOnAddTrans(const Stock_Transaction_Internal_Book_New&) override; void DoOnAddOrder(const Stock_Order_Internal_Book_New&) override; void DoOnUpdateFactors(int64_t) override;
static const size_t kMaxHistoryEvents=130; std::deque<TradeState> trades_;
}; } }
REGISTER_FACTOR_AUTO(impact_efficiency, FactorEntry)
