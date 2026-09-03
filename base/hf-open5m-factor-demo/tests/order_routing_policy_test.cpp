#include <cstdlib>
#include <iostream>

#include "app_factor/engine/order_routing_policy.h"

static void Check(bool condition, const char* expression) {
    if (!condition) {
        std::cerr << "CHECK failed: " << expression << "\n";
        std::exit(1);
    }
}

#define CHECK(expr) Check((expr), #expr)

int main() {
    Stock_Order_Internal_Book_New sh_add{};
    sh_add.market = 49;
    sh_add.orderorino = 10001;
    sh_add.order_type = 'A';
    CHECK(factors::ShouldRouteOrderToFactors(sh_add));

    Stock_Order_Internal_Book_New sh_missing_id{};
    sh_missing_id.market = 49;
    sh_missing_id.order_type = 'A';
    CHECK(!factors::ShouldRouteOrderToFactors(sh_missing_id));

    Stock_Order_Internal_Book_New sz_order{};
    sz_order.market = 48;
    sz_order.order_index = 20001;
    sz_order.order_type = '0';
    CHECK(factors::ShouldRouteOrderToFactors(sz_order));

    Stock_Order_Internal_Book_New sz_missing_index{};
    sz_missing_index.market = 48;
    sz_missing_index.orderorino = 20001;
    sz_missing_index.order_type = '0';
    CHECK(!factors::ShouldRouteOrderToFactors(sz_missing_index));

    Stock_Order_Internal_Book_New unsupported{};
    unsupported.market = 50;
    unsupported.order_index = 1;
    unsupported.orderorino = 1;
    unsupported.order_type = '0';
    CHECK(!factors::ShouldRouteOrderToFactors(unsupported));

    Stock_Order_Internal_Book_New synthetic{};
    synthetic.market = 49;
    synthetic.orderorino = 10002;
    synthetic.order_type = 'S';
    CHECK(!factors::ShouldRouteOrderToFactors(synthetic));
    return 0;
}
