#pragma once

#include "sdp_handler/quote_format_define.h"

namespace factors {

// Decide whether an order record has a valid exchange-specific mother-order
// identity before it is copied into the factor calculation queues.
//
// Shenzhen (market=48) identifies the mother order with order_index, while
// Shanghai (market=49) identifies it with orderorino.  Synthetic summary
// records (order_type='S') and unsupported markets must never enter the
// exchange-specific order state machine.
inline bool ShouldRouteOrderToFactors(
    const Stock_Order_Internal_Book_New& order) {
    if (order.order_type == 'S') {
        return false;
    }
    if (order.market == 48) {
        return order.order_index != 0;
    }
    if (order.market == 49) {
        return order.orderorino != 0;
    }
    return false;
}

}  // namespace factors
