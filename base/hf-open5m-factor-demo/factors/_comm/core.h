#pragma once

#include <memory>
#include <vector>
#include <type_traits>

#include "sdp_handler/quote_format_define.h"

#ifndef likely
// 这里使用 !!(x) 的原因是将 x 强制转换为布尔值（0 或 1），防止 x 不是严格的0或1时影响 __builtin_expect 的分支预测。
// 如果直接写 __builtin_expect(x, 1)，当 x 的类型为指针或整数时，非0即为true，但编译器并未直接识别为布尔类型。
// 因此加 !! 可以明确表达 x 是否为真，提升分支预测的准确性和安全性。
#define likely(x) __builtin_expect(!!(x), 1)
#endif

#ifndef unlikely
// 这里使用 !!(x) 的原因是将 x 强制转换为布尔值（0 或 1），防止 x 不是严格的0或1时影响 __builtin_expect 的分支预测。
// 如果直接写 __builtin_expect(x, 0)，当 x 的类型为指针或整数时，非0即为true，但编译器并未直接识别为布尔类型。
// 因此加 !! 可以明确表达 x 是否为真，提升分支预测的准确性和安全性。
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

namespace factors {

using fval_t = double;

// C++11 无 std::make_unique；C++14+ 转调标准库。调用处请写 factors::make_unique，避免与 std:: 二义。
#if __cplusplus >= 201402L || (defined(_MSVC_LANG) && _MSVC_LANG >= 201402L)
template <typename T, typename... Args>
std::unique_ptr<T> make_unique(Args&&... args) {
    return std::make_unique<T>(std::forward<Args>(args)...);
}
#else
template <typename T, typename... Args>
std::unique_ptr<T> make_unique(Args&&... args) {
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}
#endif

class IFactorEntry {
public:
    virtual ~IFactorEntry() = default;

    virtual void AddQuote(const Stock_Internal_Book& quote) = 0;
    virtual void AddTrans(const Stock_Transaction_Internal_Book_New& quote) = 0;
    virtual void AddOrder(const Stock_Order_Internal_Book_New& quote) = 0;
    virtual const std::vector<fval_t>& UpdateFactors(int64_t timestamp) = 0;
    virtual void OnGlobalTime(int exch_time) = 0;
    virtual void AfterUpdateFactors(int64_t timestamp) = 0;
    virtual const std::vector<fval_t>& GetFactorValues() const = 0;
    virtual std::vector<std::string> GetFactorNames() const = 0;
    virtual size_t GetFactorSize() const = 0;
};

}  // namespace factors