#pragma once

// raw_data_reader：编译期后端宏 + BackendTarget +（both 下）quote_reader 全局顺序。
// 原分散在 reader_backend.h / reader_config.* / backend_target.h；现单头收敛，无单独 .cc。

// ---------------------------------------------------------------------------
// 编译期后端 — Makefile/CMake 须且仅能定义以下三者之一：
//   -DREADER_BACKEND_CPP_ONLY    只编 .npq 直读，不链接 Python
//   -DREADER_BACKEND_PYTHON_ONLY 只编嵌入 Python
//   -DREADER_BACKEND_BOTH        两套都编；读数可省略 BackendTarget，缺省见 io::kDefaultRead
// ---------------------------------------------------------------------------

#if defined(READER_BACKEND_CPP_ONLY) && (defined(READER_BACKEND_PYTHON_ONLY) || defined(READER_BACKEND_BOTH))
#error Conflicting READER_BACKEND_* macros
#endif
#if defined(READER_BACKEND_PYTHON_ONLY) && defined(READER_BACKEND_BOTH)
#error Conflicting READER_BACKEND_* macros
#endif

#if defined(READER_BACKEND_CPP_ONLY)
#elif defined(READER_BACKEND_PYTHON_ONLY)
#elif defined(READER_BACKEND_BOTH)
#else
#error Define exactly one: READER_BACKEND_CPP_ONLY, READER_BACKEND_PYTHON_ONLY, or READER_BACKEND_BOTH
#endif

// ---------------------------------------------------------------------------
// BackendTarget（读数 / SetMydataPath）
// ---------------------------------------------------------------------------

enum class BackendTarget {
	// 调用方不关心具体后端时使用；最终会映射到 io::kDefaultRead / io::kDefaultPath。
	// 语义提示：
	// - cpp_only    : Default == Cpp
	// - python_only : Default == Python
	// - both        : Default == Both（由 quote_reader::Order 决定先后尝试）
	Default,
	Cpp,     // 仅 C++ 直读
	Python,  // 仅嵌入 Python
	// 显式请求“两套都可用并按策略尝试”。
	// 注意：
	// - 在 both 构建下，读取会进入先后回退逻辑（由 quote_reader::Order 控制）。
	// - 在 cpp_only / python_only 构建下，请求会被降级到唯一可用后端并输出告警。
	Both,
};

namespace io {
// 这两个默认值是“编译期常量”，用于函数默认参数与 IoOptions 初始化，
// 让调用方在不显式传 backend 时得到与当前构建模式一致的行为。
#if defined(READER_BACKEND_CPP_ONLY)
constexpr BackendTarget kDefaultRead = BackendTarget::Cpp;
constexpr BackendTarget kDefaultPath = BackendTarget::Cpp;
#elif defined(READER_BACKEND_PYTHON_ONLY)
constexpr BackendTarget kDefaultRead = BackendTarget::Python;
constexpr BackendTarget kDefaultPath = BackendTarget::Python;
#else
constexpr BackendTarget kDefaultRead = BackendTarget::Both;
constexpr BackendTarget kDefaultPath = BackendTarget::Both;
#endif
} // namespace io

struct IoOptions {
	// 不显式指定时，按当前构建模式选择默认后端。
	BackendTarget backend = io::kDefaultRead;
};

// ---------------------------------------------------------------------------
// both：全局「先试 C++ 直读还是先试嵌入 Python」（内联实现，无 reader_config.cc）
// cpp_only / python_only 下本段不编译。
// ---------------------------------------------------------------------------

#if defined(READER_BACKEND_BOTH)

namespace quote_reader {

enum class Order {
	CppFirst,
	PythonFirst,
};

namespace detail {
inline Order &MutableOrder() {
	static Order g = Order::CppFirst;
	return g;
}
} // namespace detail

inline Order current_order() {
	// 进程内全局读取顺序（both 模式有效）；线程间共享。
	return detail::MutableOrder();
}

inline void set_order(Order o) {
	// 运行期切换 both 模式下“先试 C++ 还是先试 Python”。
	detail::MutableOrder() = o;
}

} // namespace quote_reader

#endif
