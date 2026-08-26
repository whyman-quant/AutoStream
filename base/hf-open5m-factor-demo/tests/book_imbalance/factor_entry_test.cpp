#include "factors/book_imbalance/factor_entry.h"

#include <iostream>

int main() {
    factors::comm::FactorEntryConfig config;
    factors::book_imbalance::FactorEntry entry(
        "000001", factors::book_imbalance::GetMetadata(), config);
    Stock_Internal_Book invalid_quote{};
    entry.AddQuote(invalid_quote);
    const auto& values = entry.UpdateFactors(92700000);
    const size_t invalid_microprice_factors[] = {1, 4, 7, 10};
    for (size_t index : invalid_microprice_factors) {
        if (values[index] != 0.0) {
            std::cerr << "invalid quote factor " << index
                      << " must be zero, got " << values[index] << std::endl;
            return 1;
        }
    }

    factors::book_imbalance::FactorEntry crossed_entry(
        "000001", factors::book_imbalance::GetMetadata(), config);
    Stock_Internal_Book crossed_quote{};
    crossed_quote.bp_array[0] = 200;
    crossed_quote.ap_array[0] = 100;
    crossed_quote.bv_array[0] = 100;
    crossed_quote.av_array[0] = 100;
    crossed_entry.AddQuote(crossed_quote);
    const auto& crossed_values = crossed_entry.UpdateFactors(92700000);
    const size_t invalid_spread_factors[] = {2, 5, 8, 11};
    for (size_t index : invalid_spread_factors) {
        if (crossed_values[index] != 0.0) {
            std::cerr << "crossed quote factor " << index
                      << " must be zero, got " << crossed_values[index]
                      << std::endl;
            return 1;
        }
    }
    return 0;
}
