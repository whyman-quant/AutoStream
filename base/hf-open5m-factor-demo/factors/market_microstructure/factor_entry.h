#pragma once

#include <cstdint>
#include <deque>
#include <limits>
#include <unordered_map>
#include <unordered_set>

#include "factors/_comm/factor_entry_base.h"
#include "factors/_comm/factor_entry_registry.h"
#include "factors/market_microstructure/meta_config.h"
#include "factors/demofw00/tools/market/order_synthesizer.h"

namespace factors {
namespace market_microstructure {

class FactorEntry : public comm::FactorEntryBase {
public:
    FactorEntry(const std::string& asset, const comm::FactorMetadata& metadata,
                const comm::FactorEntryConfig& config);
    std::vector<bool> GetReadinessMask(int64_t timestamp) const override;
    std::vector<unsigned char> GetReadinessReasonCodes(int64_t timestamp) const override;

private:
    struct PairSample {
        double mismatch{0.0};
        int side{0};
        int64_t buy_id{0};
        int64_t sell_id{0};
        int64_t volume{0};
    };
    struct LifeSample { int side{0}; double commitment{0.0}; double cancel{0.0}; };
    struct Shock { int time_ms{0}; int side{0}; double volume{0.0}; double depth{0.0}; };

    void DoOnAddQuote(const Stock_Internal_Book& quote) override;
    void DoOnAddTrans(const Stock_Transaction_Internal_Book_New&) override;
    void DoOnAddOrder(const Stock_Order_Internal_Book_New&) override;
    void DoOnGlobalTime(int exch_time) override;
    void DoOnUpdateFactors(int64_t timestamp) override;

    void Consume(const demofw00::tools::market::BuilderOutput& output);
    void UpdateDerivedFactors();

    Stock_Internal_Book last_quote_{};
    bool has_quote_{false};
    bool has_computed_{false};
    demofw00::tools::market::OrderSynthesizer synthesizer_{20000};
    std::deque<PairSample> pairs_;
    std::deque<LifeSample> life_;
    std::deque<Shock> shocks_;
    std::deque<int> pair_sides_;
    std::unordered_set<int64_t> parent_ids_;
    std::unordered_map<int64_t, int64_t> last_cancelled_;
    size_t exact_pairs_{0};
    size_t exact_orders_bid_{0};
    size_t exact_orders_ask_{0};
    size_t lifecycle_events_{0};
    size_t matured_shocks_{0};
    bool pending_shock_{false};
    int pending_shock_side_{0};
    double pending_shock_volume_{0.0};
    double pending_shock_depth_{0.0};
    bool shock_ready_{false};
    double shock_value_{0.0};
    int last_pair_side_{0};
    size_t current_run_{0};
    double cancel_bid_{0.0};
    double cancel_ask_{0.0};
    double exec_buy_{0.0};
    double exec_sell_{0.0};
    int last_quote_time_ms_{0};
    double last_bid_depth_{0.0};
    double last_ask_depth_{0.0};
};

}  // namespace market_microstructure
}  // namespace factors

REGISTER_FACTOR_AUTO(market_microstructure, FactorEntry)
