#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <limits>
#include <type_traits>
#include <memory>
#include <vector>

namespace factors {
namespace comm {

/**
 * @brief 一个基于"2字符+年份+月份"格式的定制索引容器，提供 O(1) 查找。
 *
 * 支持的代码格式：前两位是字母（A-Z），第3-4 位是年份（00-99），第5-6 位是月份（01-12）。
 * 典型示例："IC2509" 表示 IC 品种、25 年、09 月。
 *
 * 适用场景：
 * - 可用于股指期货代码的哈希（如 IC、IF、IH 等，均为 2 字母前缀）。
 * - 可用于部分商品期货代码的哈希（如 CU、AL 等 2 字母前缀的商品期货）。
 * - 不适用于开头是 1 个或 3 个字母的商品期货（如 1 字母的 A、C、M 等，或 3 字母的商品期货代码），这些需要设计对应的映射规则。
 *
 * 性能优势：
 * - 传统做法使用 unordered_map<string, Value>，查找需要哈希计算 + bucket 线性扫描，单次查找约 30~40ns。
 * - Code2c4iyymmMap 将编码直接映射到唯一整数 key，内部用定长数组保存 unique_ptr<Value>，
 *   查找成本降低到纯整数运算（8~10ns），适合高频访问场景。
 *
 * 设计特点：
 * - 将编码映射到唯一整数 key，内部用定长数组保存 unique_ptr<Value>。
 * - Value 的生命周期由容器管理，外部无需再维护额外的 vector。
 * - 对于其他编码格式（如 1-3 字母前缀），需要设计对应的映射规则。
 *
 * 用法示例：
 * @code
 * Code2c4iyymmMap<MyValue> map;
 * MyValue& info = map["IC2509"]; // 若不存在则默认构造
 * if (auto* found = map.find("IC2509")) {
 *     // 使用 found 指向的值
 * }
 * for (const MyValue* ptr : map.values()) {
 *     // values() 返回稳定的裸指针列表，便于遍历
 * }
 * @endcode
 */
template<typename Value>
class Code2c4iyymmMap {
	static_assert(std::is_default_constructible<Value>::value,
		"Code2c4iyymmMap expects a default-constructible value type");

	static constexpr size_t kPrefixCardinality = 26 * 26;     // 两个大写字母
	static constexpr size_t kYearCardinality = 100;           // 00-99
	static constexpr size_t kMonthCardinality = 12;           // 01-12
	static constexpr size_t kTotalSlots = kPrefixCardinality * kYearCardinality * kMonthCardinality;

public:
	Code2c4iyymmMap() : table_(kTotalSlots), size_(0) {
		// 缓存 vector 的数据指针，避免每次访问时的间接访问开销
		// 性能优化：通过缓存指针，访问性能接近 std::array（约 1.5ns vs 1ns）
		table_ptr_ = table_.data();
	}

	void clear() {
		for (auto& slot : table_) {
			slot.reset();
		}
		values_.clear();
		size_ = 0;
	}

	size_t size() const noexcept { return size_; }
	bool empty() const noexcept { return size_ == 0; }

	// 以 map 风格返回指定代码对应的引用，必要时默认构造并插入
	// 通过 encode 将代码转换为整数 key，直接定位到 table_ptr_[key]，实现 O(1) 的访问延迟
	// 若指定代码不存在，会自动创建并插入默认构造的 Value 对象
	Value& operator[](const char* code) {
		const size_t key = encode(code);
		if (key == kInvalidKey) {
			throw std::invalid_argument("Code2c4iyymmMap::operator[] invalid code");
		}
		auto& slot = table_ptr_[key];
		if (!slot) {
			slot.reset(new Value());
			values_.push_back(slot.get());
			++size_;
		}
		return *slot;
	}

	Value& operator[](const std::string& code) {
		return operator[](code.c_str());
	}

	void insert(const char* code, const Value& value) {
		operator[](code) = value;
	}

	void insert(const std::string& code, const Value& value) {
		operator[](code) = value;
	}

	void insert(const char* code, Value&& value) {
		operator[](code) = std::move(value);
	}

	void insert(const std::string& code, Value&& value) {
		operator[](code) = std::move(value);
	}

	// 查询指定代码，若不存在返回 nullptr
	Value* find(const char* code) const noexcept {
		const size_t key = encode(code);
		if (key == kInvalidKey) {
			return nullptr;
		}
		const auto& slot = table_ptr_[key];
		return slot ? slot.get() : nullptr;
	}

	Value* find(const std::string& code) const noexcept {
		return find(code.c_str());
	}

	// 快速查找函数，假设 code 一定合法且一定存在于 map 中
	// 使用快速 encode（不做检查），直接返回引用，不做空指针检查
	// 适用于热路径优化，外部调用者必须保证 code 合法且已插入
	// 注意：如果 code 不存在，行为未定义（可能返回空指针解引用或访问无效内存）
	Value& fast_find(const char* code) const noexcept {
		const size_t key = fast_encode(code);
		return *table_ptr_[key];
	}

	Value& fast_find(const std::string& code) const noexcept {
		return fast_find(code.c_str());
	}

	const std::vector<Value*>& values() const noexcept {
		return values_;
	}

private:
	static constexpr size_t kInvalidKey = std::numeric_limits<size_t>::max();

	static size_t encode(const char* code) noexcept {
		if (code == nullptr) {
			return kInvalidKey;
		}
		const unsigned char c0 = static_cast<unsigned char>(code[0]);
		const unsigned char c1 = static_cast<unsigned char>(code[1]);
		if (c0 < 'A' || c0 > 'Z' || c1 < 'A' || c1 > 'Z') {
			return kInvalidKey;
		}
		const int y1 = code[2] - '0';
		const int y0 = code[3] - '0';
		const int m1 = code[4] - '0';
		const int m0 = code[5] - '0';
		if (y1 < 0 || y1 > 9 || y0 < 0 || y0 > 9 || m1 < 0 || m1 > 9 || m0 < 0 || m0 > 9) {
			return kInvalidKey;
		}
		const int month = m1 * 10 + m0;
		if (month < 1 || month > 12) {
			return kInvalidKey;
		}
		const int year = y1 * 10 + y0;
		const size_t prefix = (c0 - 'A') * 26 + (c1 - 'A');
		return prefix * (kYearCardinality * kMonthCardinality) + year * kMonthCardinality + (month - 1);
	}

	// 快速 encode 函数，不做任何检查，假设 code 一定合法
	// 适用于热路径优化，外部调用者必须保证 code 格式正确
	// 优化：减少中间变量，直接计算，让编译器更好地优化
	static inline size_t fast_encode(const char* code) noexcept {
		// 直接计算，减少中间变量和寄存器压力
		const size_t prefix = (static_cast<unsigned char>(code[0]) - 'A') * 26 + (static_cast<unsigned char>(code[1]) - 'A');
		const size_t year = (code[2] - '0') * 10 + (code[3] - '0');
		const size_t month = (code[4] - '0') * 10 + (code[5] - '0');
		return prefix * (kYearCardinality * kMonthCardinality) + year * kMonthCardinality + (month - 1);
	}

	// table_ 使用 unique_ptr<Value> 管理真实对象的所有权；values_ 仅存放裸指针，便于 O(1) 生成可遍历的视图。
	// 不能反过来（table_ 裸指针、values_ unique_ptr），否则容器无法在固定槽位上托管 Value 的生命周期，
	// 也无法保证插入覆盖时的自动析构。
	// 
	// 为什么使用 std::vector 而不是 std::array？
	// - table_ 预估大小：811,200 槽位 * 8 字节（unique_ptr）≈ 6.19 MB
	// - 如果使用 std::array，对象在栈上分配时容易导致栈溢出（Linux 默认栈大小通常为 8MB）
	// - 使用 std::vector 可以安全地在堆上分配，避免栈溢出风险
	// - 通过 table_ptr_ 缓存指针，访问性能接近 std::array（约 1.5ns vs 1ns），性能损失可接受
	std::vector<std::unique_ptr<Value>> table_;
	std::unique_ptr<Value>* table_ptr_;  // 缓存 vector 的数据指针，优化访问性能
	std::vector<Value*> values_;
	size_t size_;
};

/**
 * @brief 面向 6 位数字代码（0-999999）的完美索引容器，支持 int / const char* / std::string 键。
 *
 * 支持的代码格式：6 位纯数字，允许前导零。典型示例："600036"、"000001" 等。
 *
 * 适用场景：
 * - 可用于股票代码的哈希（如 "600036"、"000001" 等 6 位数字代码）。
 * - 可用于可转债代码的哈希（如 "110001"、"123456" 等）。
 * - 可用于 ETF 代码的哈希（如 "510300"、"159919" 等）。
 * - 适用于所有 6 位纯数字的证券代码哈希场景。
 *
 * 性能优势：
 * - 传统做法使用 unordered_map，需要哈希计算 + bucket 遍历，单次查找约 30ns。
 * - 借助"键空间有限且连续（0~999999）"的特点，直接映射到唯一槽位，实现 O(1) 的数组访问，单次仅需 5~8ns。
 * - 与 unordered_map 相比，不需要额外的 string 拷贝/哈希函数，也不会触发 rehash，对数字代码场景非常高效。
 *
 * 设计特点：
 * - 仅适用于 6 位纯数字代码。对于包含字母的代码，需要使用其他容器（如 Code2c4iyymmMap）。
 * - 支持多种键类型：int、const char*、std::string，使用统一的编码规则。
 *
 * 使用方式示例：
 * @code
 * Code6iMap<MyValue> map;
 * map["000123"].field = 1;      // 自动映射到 key=123
 * map[123].field = 2;           // 与 "000123" 等价
 * if (auto* val = map.find("600036")) { ... }
 * for (auto* ptr : map.values()) { ... } // 快速遍历全部已插入的值
 * @endcode
 *
 * 键解析规则（encode）：
 * - const char* / std::string：读取前 6 个字符，允许前导零，遇到非数字或空串视为无效；
 * - int：必须落在 [0, 999999]；值 1 等价于 "000001"。
 */
template<typename Value>
class Code6iMap {
	static_assert(std::is_default_constructible<Value>::value,
		"Code6iMap expects a default-constructible value type");

	static constexpr size_t kMaxKey = 999999;
	static constexpr size_t kTotalSlots = kMaxKey + 1;

public:
	Code6iMap() : table_(kTotalSlots), size_(0) {
		// 缓存 vector 的数据指针，避免每次访问时的间接访问开销
		// 性能优化：通过缓存指针，访问性能接近 std::array（约 1.5ns vs 1ns）
		table_ptr_ = table_.data();
	}

	void clear() {
		for (auto& slot : table_) {
			slot.reset();
		}
		values_.clear();
		size_ = 0;
	}

	size_t size() const noexcept { return size_; }
	bool empty() const noexcept { return size_ == 0; }

	Value& operator[](const char* key) {
		return access_slot(encode(key));
	}

	Value& operator[](const std::string& key) {
		return access_slot(encode(key));
	}

	Value& operator[](int key) {
		return access_slot(encode(key));
	}

	void insert(const char* key, const Value& value) {
		operator[](key) = value;
	}

	void insert(const std::string& key, const Value& value) {
		operator[](key) = value;
	}

	void insert(int key, const Value& value) {
		operator[](key) = value;
	}

	void insert(const char* key, Value&& value) {
		operator[](key) = std::move(value);
	}

	void insert(const std::string& key, Value&& value) {
		operator[](key) = std::move(value);
	}

	void insert(int key, Value&& value) {
		operator[](key) = std::move(value);
	}

	Value* find(const char* key) const noexcept {
		return get_ptr(encode(key));
	}

	Value* find(const std::string& key) const noexcept {
		return get_ptr(encode(key));
	}

	Value* find(int key) const noexcept {
		return get_ptr(encode(key));
	}

	// 快速查找函数，假设 key 一定合法且一定存在于 map 中
	// 使用快速 encode（不做检查），直接返回引用，不做空指针检查
	// 适用于热路径优化，外部调用者必须保证 key 合法且已插入
	// 注意：如果 key 不存在，行为未定义（可能返回空指针解引用或访问无效内存）
	Value& fast_find(const char* key) const noexcept {
		const size_t encoded_key = fast_encode(key);
		return *table_ptr_[encoded_key];
	}

	Value& fast_find(const std::string& key) const noexcept {
		return fast_find(key.c_str());
	}

	Value& fast_find(int key) const noexcept {
		const size_t encoded_key = fast_encode(key);
		return *table_ptr_[encoded_key];
	}

	const std::vector<Value*>& values() const noexcept {
		return values_;
	}

private:
	static constexpr size_t kInvalidKey = std::numeric_limits<size_t>::max();

	static size_t encode(const char* key) noexcept {
		if (key == nullptr) {
			return kInvalidKey;
		}
		size_t value = 0;
		int digits = 0;
		for (; digits < 6; ++digits) {
			char c = key[digits];
			if (c == '\0') {
				break;
			}
			if (c < '0' || c > '9') {
				return kInvalidKey;
			}
			value = value * 10 + static_cast<size_t>(c - '0');
		}
		if (digits == 0) {
			return kInvalidKey; // 没有任何数字
		}
		return value;
	}

	static size_t encode(const std::string& key) noexcept {
		return encode(key.c_str());
	}

	static size_t encode(int key) noexcept {
		if (key < 0 || key > static_cast<int>(kMaxKey)) {
			return kInvalidKey;
		}
		return static_cast<size_t>(key);
	}

	// 快速 encode 函数，不做任何检查，假设 key 一定合法
	// 适用于热路径优化，外部调用者必须保证 key 格式正确
	// 优化：循环展开，消除循环控制开销，让编译器更好地优化指令调度
	static inline size_t fast_encode(const char* key) noexcept {
		// 直接展开6次循环，假设前 6 个字符都是数字
		return static_cast<size_t>(key[0] - '0') * 100000 +
		       static_cast<size_t>(key[1] - '0') * 10000 +
		       static_cast<size_t>(key[2] - '0') * 1000 +
		       static_cast<size_t>(key[3] - '0') * 100 +
		       static_cast<size_t>(key[4] - '0') * 10 +
		       static_cast<size_t>(key[5] - '0');
	}

	static inline size_t fast_encode(int key) noexcept {
		// 直接转换，不做范围检查
		return static_cast<size_t>(key);
	}

	Value& access_slot(size_t key) {
		if (key == kInvalidKey) {
			throw std::invalid_argument("Code6iMap: invalid 6-digit code");
		}
		auto& slot = table_ptr_[key];
		if (!slot) {
			slot.reset(new Value());
			values_.push_back(slot.get());
			++size_;
		}
		return *slot;
	}

	Value* get_ptr(size_t key) const noexcept {
		if (key == kInvalidKey) {
			return nullptr;
		}
		const auto& slot = table_ptr_[key];
		return slot ? slot.get() : nullptr;
	}

	// 为什么使用 std::vector 而不是 std::array？
	// - table_ 预估大小：1,000,000 槽位 * 8 字节（unique_ptr）≈ 7.63 MB
	// - 如果使用 std::array，对象在栈上分配时容易导致栈溢出（Linux 默认栈大小通常为 8MB）
	// - 使用 std::vector 可以安全地在堆上分配，避免栈溢出风险
	// - 通过 table_ptr_ 缓存指针，访问性能接近 std::array（约 1.5ns vs 1ns），性能损失可接受
	std::vector<std::unique_ptr<Value>> table_;
	std::unique_ptr<Value>* table_ptr_;  // 缓存 vector 的数据指针，优化访问性能
	std::vector<Value*> values_;
	size_t size_;
};

} // namespace comm
} // namespace factors