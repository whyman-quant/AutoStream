#pragma once

// 注释约定：统一采用 // 注释，不使用 /** ... */ 格式的 docstring 注释
// 遵循 Google C++ 代码规范

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <new>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// 仅在使用 typeid / demangle 时包含，避免 -fno-rtti 下误用 typeid 导致链接错误
#if defined(__GXX_RTTI) && (defined(__GNUC__) || defined(__clang__))
#include <cxxabi.h>
#include <typeinfo>
#elif defined(_CPPRTTI) && _CPPRTTI && defined(_MSC_VER)
#include <typeinfo>
#endif

// ============================================================================
// 使用示例
// ============================================================================
//
// 示例1: 单线程申请、多线程使用版本（SAMU，推荐，性能更好）
// ----------------------------------------------------------------------------
//
// struct MyData {
//     int value;
//     double price;
// };
//
// // 创建内存池（申请只能在单线程，使用可以跨线程）
// // 无参构造：不预留内存，block_size 自动计算
// fast_soa_memory_pool::SAMUMemoryPool<MyData> pool;
// // 有参构造：预留 1000 个元素的内存（会自动向上取整到 block_size 的整数倍）
// // block_size 自动根据 Value 大小和目标块大小计算
// fast_soa_memory_pool::SAMUMemoryPool<MyData> pool_with_reserve(1000);
// // 也可以指定 block_size：预留 1000 个元素，块大小为 512
// fast_soa_memory_pool::SAMUMemoryPool<MyData> pool_with_custom_block(1000, 512);
//
// // 分配内存，设置初始使用次数
// // ptr 的类型是 fast_soa_memory_pool::PooledElement<MyData>*
// // 也可以写成：fast_soa_memory_pool::PooledElement<MyData>* ptr = pool.Allocate(3);
// auto* ptr = pool.Allocate(3);  // 初始使用次数为3
// // 显式指定元素类型：
// // fast_soa_memory_pool::SAMUMemoryPool<MyData>::PooledElementType* ptr = pool.Allocate(3);
// // 或者更通用的写法：
// // decltype(pool)::PooledElementType* ptr = pool.Allocate(3);
// ptr->data->value = 100;
//
// // 使用后减少使用次数（可以跨线程调用，推荐使用成员函数版本）
// ptr->Release(1);  // 剩余2次（无需持有 pool 引用，更方便跨线程使用）
// ptr->Release(1);  // 剩余1次
// ptr->Release(1);  // 剩余0次，自动标记为未使用
// // 或者通过 pool 调用：pool.Release(ptr, 1)（需要持有 pool 引用）
//
// // 重新设置使用次数（推荐使用成员函数版本）
// ptr->ResetUseCount(5);  // 无需持有 pool 引用
// // 或者通过 pool 调用：pool.ResetUseCount(ptr, 5)（需要持有 pool 引用）
//
// // 注意：通常不需要显式调用 RecycleBlocks()
// // 因为 Allocate() 在需要新块时会自动检查并循环利用已使用完毕的头部块
// // 只有在需要立即回收的场景下才需要显式调用
// pool.RecycleBlocks();
//
// // 析构时会输出统计：alloc_requests（调用次数）、expansions（扩展次数）、
// // expansion_rate（膨胀率）、utilization（利用率），便于调优块大小和评估内存池效果
//
// 示例2: 多线程申请、多线程使用版本（MAMU）
// ----------------------------------------------------------------------------
// // 创建内存池（申请和使用都可以跨线程）
// fast_soa_memory_pool::MAMUMemoryPool<MyData> pool;
//
// // 多个线程可以同时调用 Allocate
// // ptr 的类型是 fast_soa_memory_pool::PooledElement<MyData>*
// // 也可以写成：fast_soa_memory_pool::PooledElement<MyData>* ptr = pool.Allocate(2);
// auto* ptr = pool.Allocate(2);
//
// // 使用方式与单线程版本相同（推荐使用成员函数版本）
// ptr->Release(1);  // 无需持有 pool 引用，更方便跨线程使用
// // 或者通过 pool 调用：pool.Release(ptr, 1)（需要持有 pool 引用）

namespace velapex {
namespace fast_soa_memory_pool {

// 分支预测优化宏（避免重复定义）
// 使用 !!(x) 将 x 强制转换为布尔值，提升分支预测准确性
#ifndef likely
#define likely(x) __builtin_expect(!!(x), 1)
#endif

#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

// 获取类型名：仅在开启 RTTI 时使用 typeid；未开启时不调用 typeid，避免链接错误（undefined reference to typeinfo）
// GCC/Clang：__GXX_RTTI 表示 RTTI 开启；MSVC：_CPPRTTI 表示 RTTI 开启
template <typename U>
static inline std::string GetTypeNameFor() {
#if defined(__GXX_RTTI) && (defined(__GNUC__) || defined(__clang__))
	// RTTI 开启且为 GCC/Clang：优先 demangle，失败则退回 typeid().name()
	int status = -1;
	char* demangled = abi::__cxa_demangle(typeid(U).name(), nullptr, nullptr, &status);
	struct FreeDeleter {
		void operator()(void* p) const { std::free(p); }
	};
	std::unique_ptr<char, FreeDeleter> guard(demangled);
	if (status == 0 && demangled != nullptr) {
		return std::string(demangled);
	}
	const char* name = typeid(U).name();
	return name ? std::string(name) : std::string();
#elif defined(_CPPRTTI) && _CPPRTTI && defined(_MSC_VER)
	// MSVC 且 RTTI 开启
	const char* name = typeid(U).name();
	return name ? std::string(name) : std::string();
#else
	// RTTI 未开启或编译器未知：不调用 typeid，静默返回空串
	return std::string();
#endif
}

// 触摸一段内存，使内核提前建立物理页映射，避免首次访问时出现 μs 级 page fault
// 使用对齐步长写 0，成本可控，但能把这部分延迟提前到初始化阶段
template <size_t Alignment = 64>
inline void TouchMemory(void* ptr, size_t bytes) {
	if (ptr == nullptr || bytes == 0) {
		return;
	}
	constexpr size_t stride = Alignment;
	volatile char* data = static_cast<volatile char*>(ptr);
	for (size_t offset = 0; offset < bytes; offset += stride) {
		data[offset] = 0;
	}
	data[bytes - 1] = 0;
}

// 前向声明
template <typename Value, size_t Alignment, bool ThreadSafe>
struct MemoryBlock;

// 对齐内存分配器，确保分配的内存起始地址对齐到指定边界
// 设计原理：
// - 使用 posix_memalign 确保分配的内存起始地址对齐
// - 适用于需要缓存行对齐的场景（如 SIMD 优化、减少 False Sharing）
// - 与 std::vector 兼容，可以无缝替换默认分配器
template<typename T, size_t Alignment = 64>
class AlignedAllocator {
public:
	// 标准分配器类型定义
	typedef T value_type;
	typedef T* pointer;
	typedef const T* const_pointer;
	typedef T& reference;
	typedef const T& const_reference;
	typedef size_t size_type;
	typedef ptrdiff_t difference_type;

	// rebind 模板，用于分配不同类型的对象
	template<typename U>
	struct rebind {
		typedef AlignedAllocator<U, Alignment> other;
	};

	// 默认构造函数
	AlignedAllocator() throw() {}

	// 从其他类型的分配器构造（用于 rebind）
	template<typename U>
	AlignedAllocator(const AlignedAllocator<U, Alignment>&) throw() {}

	// 分配内存
	pointer allocate(size_type n, const void* = 0) {
		if (n == 0) {
			return nullptr;
		}

		// 计算需要分配的总字节数
		size_t total_bytes = n * sizeof(T);

		// 使用 posix_memalign 分配对齐的内存
		// posix_memalign 是 POSIX 标准，比 aligned_alloc 更通用（aligned_alloc 需要 C++17）
		void* ptr = nullptr;
		int result = posix_memalign(&ptr, Alignment, total_bytes);

		if (result != 0) {
			// posix_memalign 失败时返回错误码（不是设置 errno）
			// EINVAL: 对齐值不是 2 的幂或不是指针大小的倍数
			// ENOMEM: 内存不足
			throw std::bad_alloc();
		}

		return static_cast<pointer>(ptr);
	}

	// 释放内存
	void deallocate(pointer p, size_type) {
		if (p != nullptr) {
			// posix_memalign 分配的内存必须用 free 释放
			free(p);
		}
	}

	// 获取最大可分配大小（理论值，实际受系统限制）
	size_type max_size() const throw() {
		return size_type(-1) / sizeof(T);
	}

	// 构造对象（通用版本，支持拷贝构造、移动构造和完美转发）
	template<typename U, typename... Args>
	void construct(U* p, Args&&... args) {
		::new(static_cast<void*>(p)) U(std::forward<Args>(args)...);
	}

	// 销毁对象
	void destroy(pointer p) {
		p->~T();
	}

	// 获取地址
	pointer address(reference x) const {
		return &x;
	}

	const_pointer address(const_reference x) const {
		return &x;
	}
};

// 比较操作符（分配器比较）
template<typename T1, typename T2, size_t Alignment>
bool operator==(const AlignedAllocator<T1, Alignment>&,
				const AlignedAllocator<T2, Alignment>&) throw() {
	return true;
}

template<typename T1, typename T2, size_t Alignment>
bool operator!=(const AlignedAllocator<T1, Alignment>&,
				const AlignedAllocator<T2, Alignment>&) throw() {
	return false;
}

// 对齐的 vector 类型别名，使用对齐分配器
template<typename T, size_t Alignment = 64>
using AlignedVector = std::vector<T, AlignedAllocator<T, Alignment>>;

// Value 包装器，确保每个 Value 对象对齐到指定边界
// 目的：
// 1. 支持 SIMD 指令操作 Value（需要对齐）
// 2. 避免多线程访问时的伪共享（False Sharing）
// 3. 提升 memcpy 性能（对齐内存可以使用更宽的 SIMD 指令）
template <typename Value, size_t Alignment = 64>
struct alignas(Alignment) ValueWrapper {
	Value data;  // 不需要额外的 alignas，结构体对齐已足够

	// 提供透明访问接口，对用户完全透明
	Value* operator->() { return &data; }
	const Value* operator->() const { return &data; }
	Value& operator*() { return data; }
	const Value& operator*() const { return data; }
	operator Value&() { return data; }
	operator const Value&() const { return data; }

	// 获取原始 Value 的引用（用于兼容现有代码）
	Value& get() { return data; }
	const Value& get() const { return data; }
};

// 元素状态（SOA 布局：控制数据与用户数据分离，提高缓存友好性）
// 包含所有状态字段，尺寸小（约 16-24 字节），缓存友好
// 所有控制操作（Allocate、Release、ResetUseCount）都只访问这个小的状态块
// Alignment: 内存对齐值，默认为64字节（缓存行大小）
// 64 字节对齐确保每个 ElementStatus 独占一个缓存行，避免多线程频繁修改时的伪共享
template <typename BlockType, size_t Alignment = 64>
struct alignas(Alignment) ElementStatus {
	// 静态断言：确保对齐值 Alignment 大于0且为2的幂
	static_assert(Alignment && ((Alignment & (Alignment - 1)) == 0), "Alignment must be power of two");

	// 剩余使用次数，原子操作保证线程安全（使用是跨线程的）
	// 64 字节对齐确保每个 ElementStatus 独占一个缓存行，避免多线程频繁修改时的伪共享
	std::atomic<int> remaining_uses_;
	// 是否正在使用中，原子操作保证线程安全（使用是跨线程的）
	// 与 remaining_uses_ 在同一个缓存行，但由于整个结构体对齐，避免了与其他 ElementStatus 的伪共享
	std::atomic<bool> in_use_;
	// 指向所属的内存块（用于更新块的使用中计数，避免遍历检查）
	BlockType* owning_block_;
	// 填充到 Alignment 字节（如果需要）
	// 注意：编译器会自动填充，确保结构体大小为 Alignment 字节的倍数

	ElementStatus() : remaining_uses_(0), in_use_(false), owning_block_(nullptr) {}
};

// 内存池中存储的元素的句柄（SOA 布局：轻量级句柄，包含指向状态块和数据的指针）
// 这是用户实际使用的对象，包含指向状态块和数据的指针
// 优势：分配操作的性能与 Value 大小解耦，因为只访问小的 ElementStatus
template <typename Value, size_t Alignment = 64, typename BlockType = void>
struct alignas(Alignment) PooledElement {
	// 静态断言：BlockType 不能是 void（必须指定具体的内存块类型）
	static_assert(!std::is_same<BlockType, void>::value, "BlockType must be specified");
	// 静态断言：确保对齐值 Alignment 大于0且为2的幂
	static_assert(Alignment && ((Alignment & (Alignment - 1)) == 0), "Alignment must be power of two");

public:
	PooledElement() : data(nullptr), status_(nullptr) {}

	// 存储的实际值（用户需要访问，保持 public）
	// 注意：这是指向实际数据的指针，实际数据存储在 MemoryBlock 的 values_ 数组中
	Value* data;

	// 禁止拷贝和赋值，避免引用计数混乱
	PooledElement(const PooledElement&) = delete;
	PooledElement& operator=(const PooledElement&) = delete;

	// 减少使用次数（成员函数版本，方便跨线程使用，无需持有 pool 引用）
	// decrement: 减少的数量，默认为1
	// 返回: 减少后的剩余使用次数
	// 性能实测（典型路径）: 约 20ns（无论SAMU还是MAMU，性能稳定一致）
	// 性能对比（以 1000 字节的对象为例）：
	//   - 使用 delete: 约 60ns（取决于内存分配器、系统调用开销）
	//   - 使用 Release: 约 20ns（纯原子操作，无系统调用）
	//   - 性能提升: 约 3 倍
	// 使用示例: ptr->Release(1) 或 ptr->Release()
	// SOA 布局优势：只访问小的 ElementStatus，性能与 Value 大小解耦
	int Release(int decrement = 1) {
		if (decrement <= 0) {
			throw std::invalid_argument("decrement must be greater than 0, got: " + std::to_string(decrement));
		}
		if (unlikely(status_ == nullptr)) {
			throw std::runtime_error("status_ is nullptr in Release");
		}

		// 原子操作减少使用次数（使用是跨线程的，所以必须原子操作）
		// 使用 relaxed 内存序，因为只是计数操作，不需要与其他数据同步
		// SOA 布局：访问 status_->remaining_uses_，缓存友好
		int old_count = status_->remaining_uses_.fetch_sub(decrement, std::memory_order_relaxed);
		int new_count = old_count - decrement;

		// 先判断 new_count == 0 的情况（正常情况且为热路径）
		// 如果使用次数归零，标记为未使用并更新块的使用中计数
		// 使用 relaxed 内存序，因为只是状态标志，不需要与其他数据同步
		if (likely(new_count == 0)) {
			bool was_in_use = status_->in_use_.exchange(false, std::memory_order_relaxed);
			// 如果之前是使用中，现在变为未使用，更新块的使用中计数
			if (was_in_use && status_->owning_block_ != nullptr) {
				status_->owning_block_->UpdateInUseCount(-1);
			}
		} else if (unlikely(new_count < 0)) {
			// 正常情况下，new_count 不应该小于 0
			// 如果小于 0，说明外部使用时一开始预估的使用次数有误或者内部 Release 的次数有误
			throw std::runtime_error(
				"Release count became negative; decrement=" + std::to_string(decrement) +
				", old_count=" + std::to_string(old_count) +
				", new_count=" + std::to_string(new_count) +
				", status_ptr=" + std::to_string(reinterpret_cast<uintptr_t>(status_)) +
				", data_ptr=" + std::to_string(reinterpret_cast<uintptr_t>(data)));
		}

		return new_count;
	}

	// 重新设置使用次数（成员函数版本，方便跨线程使用，无需持有 pool 引用）
	// new_count: 新的使用次数
	// 性能实测（典型路径）: 约 20ns（与 Release 类似，纯原子操作）
	// 使用示例: ptr->ResetUseCount(5)
	// SOA 布局优势：只访问小的 ElementStatus，性能与 Value 大小解耦
	void ResetUseCount(int new_count) {
		if (new_count <= 0) {
			throw std::invalid_argument("new_count must be greater than 0, got: " + std::to_string(new_count));
		}
		if (unlikely(status_ == nullptr)) {
			throw std::runtime_error("status_ is nullptr in ResetUseCount");
		}

		// 检查之前是否未使用，如果是则更新块的使用中计数
		// 使用 relaxed 内存序，因为只是状态标志，不需要与其他数据同步
		// SOA 布局：访问 status_->in_use_，缓存友好
		bool was_in_use = status_->in_use_.exchange(true, std::memory_order_relaxed);

		// 原子操作设置新的使用次数（使用是跨线程的，所以必须原子操作）
		// 使用 relaxed 内存序，因为只是计数操作，不需要与其他数据同步
		status_->remaining_uses_.store(new_count, std::memory_order_relaxed);

		// 如果之前是未使用，现在变为使用中，更新块的使用中计数
		if (!was_in_use && status_->owning_block_ != nullptr) {
			status_->owning_block_->UpdateInUseCount(1);
		}
	}

private:
	// 指向状态块的指针（SOA 布局：控制数据与用户数据分离）
	// 状态块尺寸小（约 16-24 字节），缓存友好，分配操作的性能与 Value 大小解耦
	// 注意：必须在 data 之后声明，以匹配构造函数初始化列表的顺序
	ElementStatus<BlockType>* status_;

	// 友元声明：允许 MemoryBlock 和 MemoryPoolBase 访问私有成员
	// MemoryBlock 用于初始化 status_ 和 data，MemoryPoolBase 用于优化访问
	template <typename V, size_t A, bool T>
	friend struct MemoryBlock;
	template <typename V, bool TS, size_t Al, size_t TBB, size_t MBS>
	friend class MemoryPoolBase;
};

// 内存块（SOA 布局：控制数据与用户数据分离）
// ThreadSafe: true 表示多线程安全申请，false 表示单线程申请
// SOA 布局优势：
//   1. statuses_ 数组包含小的 ElementStatus（约 16-24 字节），缓存友好
//   2. values_ 数组包含用户数据，分配操作不访问，性能与 Value 大小解耦
//   3. 分配、释放、重置等操作只访问 statuses_，性能大幅提升
//   4. 使用 unique_ptr 而不是 vector 存储 values_，避免 std::vector 在大块内存分配时的额外开销
template <typename Value, size_t Alignment, bool ThreadSafe>
struct MemoryBlock {
	// 使用 PooledElement 时传入 MemoryBlock 类型，避免函数指针
	using PooledElementType = PooledElement<Value, Alignment, MemoryBlock>;
	// 元素状态类型（传入 Alignment 参数）
	using ElementStatusType = ElementStatus<MemoryBlock, Alignment>;
	// Value 包装器类型
	using ValueWrapperType = ValueWrapper<Value, Alignment>;

public:
	explicit MemoryBlock(size_t block_size)
			: used_count(0),
			in_use_count(0),
			next(nullptr),
			statuses_(block_size),
			pooled_elements_(block_size),
			values_(nullptr),
			values_size_(block_size),
			values_raw_memory_(nullptr) {
		// 使用对齐分配内存，对齐到 Alignment 边界（默认 64 字节缓存行）
		// 这样可以：
		// 1. 支持 SIMD 指令操作 Value（需要对齐）
		// 2. 避免多线程访问时的伪共享（False Sharing）
		// 3. 提升缓存性能
		// 使用手动对齐分配（C++11 兼容）
		size_t value_wrapper_size = sizeof(ValueWrapperType);
		size_t total_size = block_size * value_wrapper_size;
		// 分配额外的空间以确保可以对齐，并存储原始指针
		// 需要确保对齐后的地址有足够空间存储原始指针
		size_t extra_size = Alignment - 1 + sizeof(void*);
		void* raw_memory = std::malloc(total_size + extra_size);
		if (unlikely(raw_memory == nullptr)) {
			throw std::bad_alloc();
		}
		// 计算对齐后的地址（确保有空间存储原始指针）
		uintptr_t addr = reinterpret_cast<uintptr_t>(raw_memory);
		uintptr_t aligned_addr = (addr + sizeof(void*) + Alignment - 1) & ~(Alignment - 1);
		// 在对齐地址前一个位置存储原始指针（用于释放）
		void** ptr_storage = reinterpret_cast<void**>(aligned_addr) - 1;
		*ptr_storage = raw_memory;
		values_ = reinterpret_cast<ValueWrapperType*>(aligned_addr);
		values_raw_memory_ = raw_memory;

		// 使用 placement new 构造每个 ValueWrapper 和内部的 Value 对象
		for (size_t i = 0; i < block_size; ++i) {
			new (&values_[i]) ValueWrapperType();  // 构造 ValueWrapper
			new (&values_[i].data) Value();  // 构造内部的 Value（如果需要）
		}
		// 预热状态块内存，避免首次访问时的 page fault
		// 状态块尺寸小，缓存友好，预热开销小
		// 这是 Allocate 操作主要访问的内存，必须预热
		if (likely(!statuses_.empty())) {
			TouchMemory<Alignment>(statuses_.data(), statuses_.size() * sizeof(ElementStatusType));
		}
		// 预热 PooledElement 数组内存，避免首次访问时的 page fault
		// PooledElement 尺寸固定（Alignment 字节），缓存友好，预热开销小
		// 这是 Allocate 操作返回的对象，必须预热
		if (likely(!pooled_elements_.empty())) {
			TouchMemory<Alignment>(pooled_elements_.data(), pooled_elements_.size() * sizeof(PooledElementType));
		}
		// 注意：不预热 values_ 内存，因为 Allocate 操作不访问 values_
		// 预热 values_ 会导致大对象时触摸大量内存（如 512 * 2000 = 1MB），影响缓存行为
		// 用户访问 data 时会自然触发 page fault，这是可接受的延迟
		// 这样可以确保 Allocate 的性能与 Value 大小解耦
		// 初始化每个状态块的 owning_block_ 指针，并建立 PooledElement 与状态块和数据的关联
		// 使用循环展开优化，每次处理多个元素，减少循环开销
		size_t i = 0;
		const size_t size = statuses_.size();
		// 每次处理 4 个元素，减少循环次数
		for (; i + 3 < size; i += 4) {
			statuses_[i].owning_block_ = this;
			statuses_[i + 1].owning_block_ = this;
			statuses_[i + 2].owning_block_ = this;
			statuses_[i + 3].owning_block_ = this;
			// 建立 PooledElement 与状态块和数据的关联
			pooled_elements_[i].status_ = &statuses_[i];
			pooled_elements_[i].data = &values_[i].data;  // 注意：使用 .data 访问 Value
			pooled_elements_[i + 1].status_ = &statuses_[i + 1];
			pooled_elements_[i + 1].data = &values_[i + 1].data;
			pooled_elements_[i + 2].status_ = &statuses_[i + 2];
			pooled_elements_[i + 2].data = &values_[i + 2].data;
			pooled_elements_[i + 3].status_ = &statuses_[i + 3];
			pooled_elements_[i + 3].data = &values_[i + 3].data;
		}
		// 处理剩余元素
		for (; i < size; ++i) {
			statuses_[i].owning_block_ = this;
			pooled_elements_[i].status_ = &statuses_[i];
			pooled_elements_[i].data = &values_[i].data;  // 注意：使用 .data 访问 Value
		}
	}

	// 析构函数：正确释放 values_ 的内存
	// 注意：ValueWrapper 析构时会自动析构其 data 成员，故只需调用 ~ValueWrapperType()。
	// 若同时调用 data.~Value()，会导致 Value 被双重析构。
	//   - 对含 std::vector 等非平凡析构的 Value ，会触发 double free；
	//   - 对纯 POD 类型，则无影响，析构函数为空，多次调用等价于多次空操作，不会释放内存，不会触发 double free。
	~MemoryBlock() {
		if (values_ != nullptr) {
			// ValueWrapper析构时会自动析构data成员，是因为C++对象析构时，其成员会递归调用各自的析构函数。
			// ValueWrapper只包含一个成员Value data，因此~ValueWrapperType()会自动调用data.~Value()。
			for (size_t i = 0; i < values_size_; ++i) {
				values_[i].~ValueWrapperType();  // 析构 ValueWrapper（会析构其 data 成员 Value）
			}
			// 然后释放内存（使用原始指针）
			// 对于手动对齐分配，需要从对齐地址前一个位置读取原始指针
			if (values_raw_memory_ != nullptr) {
				void** ptr_storage = reinterpret_cast<void**>(values_) - 1;
				void* original_ptr = *ptr_storage;
				std::free(original_ptr);
				values_raw_memory_ = nullptr;
			}
			values_ = nullptr;
		}
		// statuses_ 和 pooled_elements_ 由 AlignedVector 自动管理，无需手动释放
	}

	// 更新使用中计数（供 PooledElement 调用）
	// delta: 变化量，> 0 表示增加，< 0 表示减少
	// 注意：无论 ThreadSafe 是什么值，都必须使用原子操作
	// 因为 Release 和 ResetUseCount 都是跨线程使用的
	// 使用 relaxed 内存序，因为只是计数器，不需要与其他数据同步
	void UpdateInUseCount(int delta) {
		in_use_count.fetch_add(delta, std::memory_order_relaxed);
	}

	// 读取使用中计数
	// 注意：无论 ThreadSafe 是什么值，都必须使用原子操作
	// 因为 Release 和 ResetUseCount 都是跨线程使用的
	// 使用 relaxed 内存序，因为只是读取计数器，不需要与其他操作同步
	size_t LoadInUseCount() const {
		return in_use_count.load(std::memory_order_relaxed);
	}

	// 读取已使用计数（编译期选择实现，避免运行时判断）
	size_t LoadUsedCount() const {
		return LoadUsedCountImpl(std::integral_constant<bool, ThreadSafe>());
	}

	// 原子地递增并返回旧值（编译期选择实现，避免运行时判断）
	size_t FetchAddUsedCount(size_t increment = 1) {
		return FetchAddUsedCountImpl(increment, std::integral_constant<bool, ThreadSafe>());
	}

	// 原子地递减（编译期选择实现，避免运行时判断）
	void FetchSubUsedCount(size_t decrement = 1) {
		FetchSubUsedCountImpl(decrement, std::integral_constant<bool, ThreadSafe>());
	}

	// 设置已使用计数（编译期选择实现，避免运行时判断）
	void StoreUsedCount(size_t value) {
		StoreUsedCountImpl(value, std::integral_constant<bool, ThreadSafe>());
	}

	// 设置使用中计数
	// 注意：无论 ThreadSafe 是什么值，都必须使用原子操作
	// 因为 Release 和 ResetUseCount 都是跨线程使用的
	// 使用 relaxed 内存序，因为只是设置计数器，不需要与其他操作同步
	void StoreInUseCount(size_t value) {
		in_use_count.store(value, std::memory_order_relaxed);
	}

	// 友元声明：允许 MemoryPoolBase 访问私有成员（用于访问 next 指针和 values）
	template <typename V, bool TS, size_t Al, size_t TBB, size_t MBS>
	friend class MemoryPoolBase;

private:
	// 已使用的元素数量（同时也是下一个分配的索引）
	// 单线程版本使用普通变量，多线程版本使用原子变量
	typename std::conditional<ThreadSafe, std::atomic<size_t>, size_t>::type used_count;
	// 正在使用中的元素数量（用于快速检查块是否全部释放，避免遍历）
	// 注意：无论 ThreadSafe 是什么值，in_use_count 都必须是原子的
	// 因为 Release 和 ResetUseCount 都是跨线程使用的
	std::atomic<size_t> in_use_count;
	// 链表指针
	MemoryBlock* next;
	// SOA 布局：状态块数组（小对象，缓存友好，分配操作主要访问这个）
	// 放在 values_ 之前，确保访问 statuses_ 时不会因为 values_ 很大而触发缓存未命中
	// 使用 AlignedVector 确保每个 ElementStatus 对齐到 Alignment 字节
	// 这是关键优化：避免多线程频繁修改原子变量时的伪共享
	AlignedVector<ElementStatusType, Alignment> statuses_;
	// SOA 布局：PooledElement 句柄数组（轻量级，包含指向 status_ 和 data 的指针）
	// 放在 values_ 之前，确保访问 pooled_elements_ 时不会因为 values_ 很大而触发缓存未命中
	// 使用 AlignedVector 确保每个 PooledElement 对齐到 Alignment 字节
	// 虽然 PooledElement 只读，但多线程同时读取相邻元素时，对齐可以避免缓存行共享开销
	AlignedVector<PooledElementType, Alignment> pooled_elements_;
	// SOA 布局：用户数据数组（大对象，分配操作不访问，性能与 Value 大小解耦）
	// 放在最后，避免影响 statuses_ 和 pooled_elements_ 的缓存行为
	// 使用对齐分配 + placement new 而不是 new[]，避免 new[] 的额外开销
	// 对齐到 Alignment 边界（默认 64 字节缓存行），支持 SIMD 和避免伪共享
	// 使用 ValueWrapper 确保每个 Value 对齐到 Alignment 字节
	// 这样可以减少内存分配器状态变化对 Allocate 性能的间接影响
	ValueWrapperType* values_;
	size_t values_size_;  // 记录数组大小，用于析构时正确释放
	void* values_raw_memory_;  // 记录原始分配的内存指针，用于正确释放（可能包含对齐偏移）

	// 多线程版本的 Load 实现
	// 使用 relaxed 内存序，因为只是读取计数器，不需要与其他操作同步
	size_t LoadUsedCountImpl(std::true_type) const {
		return used_count.load(std::memory_order_relaxed);
	}

	// 单线程版本的 Load 实现
	size_t LoadUsedCountImpl(std::false_type) const {
		return used_count;
	}

	// 多线程版本的 FetchAdd 实现
	// 使用 relaxed 内存序，因为只是计数器操作，不需要与其他数据同步
	size_t FetchAddUsedCountImpl(size_t increment, std::true_type) {
		return used_count.fetch_add(increment, std::memory_order_relaxed);
	}

	// 单线程版本的 FetchAdd 实现
	size_t FetchAddUsedCountImpl(size_t increment, std::false_type) {
		size_t old_value = used_count;
		used_count += increment;
		return old_value;
	}

	// 多线程版本的 FetchSub 实现
	// 使用 relaxed 内存序，因为只是计数器操作，不需要与其他数据同步
	void FetchSubUsedCountImpl(size_t decrement, std::true_type) {
		used_count.fetch_sub(decrement, std::memory_order_relaxed);
	}

	// 单线程版本的 FetchSub 实现
	void FetchSubUsedCountImpl(size_t decrement, std::false_type) {
		used_count -= decrement;
	}

	// 多线程版本的 Store 实现
	// 使用 relaxed 内存序，因为只是设置计数器，不需要与其他操作同步
	void StoreUsedCountImpl(size_t value, std::true_type) {
		used_count.store(value, std::memory_order_relaxed);
	}

	// 单线程版本的 Store 实现
	void StoreUsedCountImpl(size_t value, std::false_type) {
		used_count = value;
	}

};

// 内存池基础实现类（内部使用，不直接暴露）
// 特性：
// 1. 多个 vector<Value> 通过指针链表连接
// 2. 内存不够时申请新的 vector 插入链表尾部
// 3. 从尾部块分配，按顺序从头到尾使用
// 4. 头部块全部使用完后移动到尾部作为新的空白块
// 5. 支持跨线程使用，使用原子操作保证线程安全
// 6. 支持内存对齐，提升 SIMD 和缓存性能
// 7. 根据 Value 大小动态计算块大小，平衡内存利用率和分配频率
// 8. 块大小自动向上取整到 2 的幂次，优化取模运算（如果未来改为循环使用）和内存分配器友好性
//
// ThreadSafe: true 表示多线程安全申请，false 表示单线程申请（但使用依然是跨线程的）
// Alignment: 内存对齐值，默认为64字节（缓存行大小）
// TargetBlockBytes: 目标块大小（字节），默认为128KB，用于动态计算块大小
// MinBlockSize: 最小块大小（元素数量），默认为64，确保块不会太小
// 注意：最终块大小会向上取整到 2 的幂次
template <typename Value,
	bool ThreadSafe,
	size_t Alignment = 64,
	size_t TargetBlockBytes = 128 * 1024,
	size_t MinBlockSize = 64>
class MemoryPoolBase {
public:
	// 静态断言：确保对齐值 Alignment 大于0且为2的幂
	static_assert(Alignment && ((Alignment & (Alignment - 1)) == 0), "Alignment must be power of two");

	// 类型别名
	using BlockType = MemoryBlock<Value, Alignment, ThreadSafe>;
	// PooledElementType 使用 BlockType 作为模板参数，避免函数指针
	using PooledElementType = PooledElement<Value, Alignment, BlockType>;

	// 构造函数
	// reserve_size: 预留的内存大小（元素数量），默认为 0（不预留）
	//               如果不是 block_size 的整数倍，会向上取整到 block_size 的整数倍
	// block_size: 如果为 0，则根据 Value 大小和目标块大小自动计算
	//             否则使用指定的 block_size（但不会小于 MinBlockSize）
	explicit MemoryPoolBase(size_t reserve_size = 0, size_t block_size = 0)
			: block_size_(CalculateBlockSize(block_size)), block_count_(0),
			initial_capacity_(0), initial_memory_byte_size_(0) {
		if (block_size_ == 0) {
			throw std::invalid_argument("block_size must be greater than 0");
		}
		// 初始化头部块、尾部块和当前块
		InitHeadBlock();
		InitTailBlock();
		InitCurrentBlock();

		// 如果指定了预留大小，预先创建内存块
		if (reserve_size > 0) {
			BatchAddNewBlocks(reserve_size);
			// 预留后，设置当前块为第一个块
			SetCurrentBlock(GetHeadBlock());
		}
	}

	// 禁止拷贝和赋值
	MemoryPoolBase(const MemoryPoolBase&) = delete;
	MemoryPoolBase& operator=(const MemoryPoolBase&) = delete;

	// 析构函数
	~MemoryPoolBase() {
		size_t final_capacity = GetCapacity();
		size_t final_memory_byte_size = GetMemoryByteSize();
		std::string type_name = GetTypeNameFor<Value>();
		const char* debug_part = debug_name_.empty() ? "unnamed" : debug_name_.c_str();
		std::string prefix = "~FastMemoryPool(SOA)";
		prefix += "<";
		if (!type_name.empty()) {
			prefix += type_name;
			prefix += "-";
		}
		prefix += debug_part;
		prefix += ">";

		const bool capacity_expanded = (final_capacity > initial_capacity_);
		// 仅扩容时为 16 色 SGR：加粗 + 亮黄（1;93）；否则为空串，保持终端默认色
		const char* const ansi_cout_color_set = capacity_expanded ? "\033[1;93m" : "";
		const char* const ansi_cout_color_reset = capacity_expanded ? "\033[0m" : "";

		// 先写入 ostringstream 再一次性输出，避免多线程下同一条日志被其它线程的 << 拆开
		if (capacity_expanded) {
			std::ostringstream warn_line;
			warn_line << "Warning: " << prefix << ": capacity expanded from " << initial_capacity_
				<< " to " << final_capacity << "!";
			std::cerr << warn_line.str() << std::endl;
		}

		// 析构时显示的内存 = GetMemoryByteSize 基础值 + capacity * extra_bytes_per_element_
		size_t initial_memory_display = initial_memory_byte_size_
			+ initial_capacity_ * extra_bytes_per_element_;
		size_t final_memory_display = final_memory_byte_size
			+ final_capacity * extra_bytes_per_element_;
		{
			std::ostringstream line;
			line << ansi_cout_color_set;
			line << prefix << ": initial capacity=" << initial_capacity_
				<< ", memory=" << std::fixed << std::setprecision(3)
				<< (initial_memory_display / (1024.0 * 1024.0)) << " MB"
				<< ", final capacity=" << final_capacity
				<< ", memory=" << (final_memory_display / (1024.0 * 1024.0)) << " MB";
			line << ansi_cout_color_reset;
			std::cout << line.str() << std::endl;
		}

		// 统计：基于 alloc_request_count_ 推导扩展次数、膨胀率、利用率
		// 推导依据：块大小固定；扩展次数=(最终容量-初始容量)/块大小
		uint64_t alloc_total = LoadAllocRequestCount();
		if (block_size_ > 0) {
			size_t expansion_count = (final_capacity > initial_capacity_)
				? (final_capacity - initial_capacity_) / block_size_
				: 0;
			double expansion_rate_pct = (initial_capacity_ > 0)
				? (100.0 * static_cast<double>(final_capacity - initial_capacity_) / initial_capacity_)
				: 0.0;  // 膨胀率 = (最终容量 - 初始容量) / 初始容量
			double utilization_pct = (final_capacity > 0)
				? (100.0 * static_cast<double>(alloc_total) / final_capacity)
				: 0.0;
			std::ostringstream line;
			line << ansi_cout_color_set;
			line << prefix << ": alloc_requests=" << alloc_total
				<< ", expansions=" << expansion_count
				<< ", expansion_rate=" << std::fixed << std::setprecision(2) << expansion_rate_pct << "%"
				<< ", utilization=" << std::setprecision(2) << utilization_pct << "%";
			if (alloc_total > final_capacity) {
				const uint64_t reuse_ops = alloc_total - static_cast<uint64_t>(final_capacity);
				const size_t elem_payload = sizeof(Value) + extra_bytes_per_element_;
				const double reuse_saved_mb =
					static_cast<double>(reuse_ops) * static_cast<double>(elem_payload)
					/ (1024.0 * 1024.0);
				line << ", saved_memory=" << std::fixed << std::setprecision(3) << reuse_saved_mb
					<< " MB";
			}
			line << ansi_cout_color_reset;
			std::cout << line.str() << std::endl;
		}

		BlockType* current = GetHeadBlock();
		while (current != nullptr) {
			BlockType* next = current->next;
			delete current;
			current = next;
		}
	}

	// 仅用于析构日志等诊断；未 Set 时 GetDebugName() 返回 "unnamed"
	void SetDebugName(std::string name) { debug_name_ = std::move(name); }
	std::string GetDebugName() const {
		return debug_name_.empty() ? std::string("unnamed") : debug_name_;
	}

	// 分配内存并设置初始使用次数
	// initial_use_count: 初始使用次数，默认为1
	// 返回: 分配的内存指针，如果分配失败返回 nullptr
	// 性能实测（典型路径，不触及新块创建）：
	//   - 单线程申请版本（SAMU）: 约 15-20ns
	//   - 多线程申请版本（MAMU）: 约 20-25ns
	// 如果当前块已满，需要循环利用或创建新块，额外耗时约 20-200ns
	//
	// 性能对比（以不同大小的对象为例）：
	//   - 使用 new: 10字节约20ns，1000字节约200ns，10000字节约1000-2000ns
	//   - 使用 memory_pool: 约 15-25ns（从预分配块中分配，内存已在缓存中，性能与对象大小解耦）
	//   - 性能提升: 对于大对象（1000字节以上），提升约 8-80 倍（在高频分配场景下效果显著）
	// 注意：对于释放操作，使用 delete 约 60ns（1000字节），使用 Release 约 20ns，性能提升约 3 倍
	PooledElementType* Allocate(int initial_use_count = 1) {
		if (unlikely(initial_use_count <= 0)) {
			throw std::invalid_argument("initial_use_count must be greater than 0");
		}

		// 统计：记录 Allocate 调用次数，析构时用于推导扩展次数、利用率
		IncrementAllocRequestCount();

		// 从当前块开始分配（而不是尾部块）
		BlockType* block = GetCurrentBlock();
		if (unlikely(block == nullptr)) {
			// 如果当前块为空，尝试从头部块开始
			block = GetHeadBlock();
			if (block != nullptr) {
				SetCurrentBlock(block);
			} else {
				// 如果头部块也为空（pool没有预留），创建第一个块
				AllocateNewBlock();
				block = GetCurrentBlock();
				if (unlikely(block == nullptr)) {
					return nullptr;
				}
			}
		}

		// 尝试从当前块分配
		PooledElementType* result = TryAllocateFromBlock(block, initial_use_count);
		if (likely(result != nullptr)) {
			return result;
		}

		// TryAllocateFromBlock 返回 nullptr，说明当前块已满
		// 检查是否有下一个块可以使用
		if (block->next != nullptr) {
			// 移动到下一个块
			SetCurrentBlock(block->next);
			block = GetCurrentBlock();
			if (block != nullptr) {
				result = TryAllocateFromBlock(block, initial_use_count);
				if (result != nullptr) {
					return result;
				}
			}
		}

		// 当前块已满且没有下一个块，先尝试循环利用已使用完毕的头部块
		if (TryRecycleHeadBlock()) {
			// 成功循环利用了头部块，设置当前块为新的尾部块
			block = GetTailBlock();
			if (block != nullptr) {
				SetCurrentBlock(block);
				result = TryAllocateFromBlock(block, initial_use_count);
				if (result != nullptr) {
					return result;
				}
			}
		}

		// 当前块已满且无法循环利用，分配新块
		AllocateNewBlock();
		// 重新获取当前块（AllocateNewBlock 会设置 current_block_ 为新创建的尾部块）
		block = GetCurrentBlock();
		if (unlikely(block == nullptr)) {
			return nullptr;
		}
		result = TryAllocateFromBlock(block, initial_use_count);
		return result;
	}

	// 减少使用次数（通过 pool 调用，需要持有 pool 引用）
	// 推荐使用成员函数版本：ptr->Release(decrement)，无需持有 pool 引用，更方便跨线程使用
	// ptr: 内存指针
	// decrement: 减少的数量，默认为1
	// 返回: 减少后的剩余使用次数
	// 性能实测（典型路径）: 约 20ns（与成员函数版本相同，只是多一次函数调用）
	// 性能对比（以 1000 字节的对象为例）：
	//   - 使用 delete: 约 60ns（取决于内存分配器、系统调用开销）
	//   - 使用 Release: 约 20ns（纯原子操作，无系统调用）
	//   - 性能提升: 约 3 倍
	int Release(PooledElementType* ptr, int decrement = 1) {
		if (ptr == nullptr) {
			throw std::invalid_argument("ptr cannot be nullptr");
		}
		// 调用成员函数版本，避免代码重复
		return ptr->Release(decrement);
	}

	// 重新设置使用次数（通过 pool 调用，需要持有 pool 引用）
	// 推荐使用成员函数版本：ptr->ResetUseCount(new_count)，无需持有 pool 引用，更方便跨线程使用
	// ptr: 内存指针
	// new_count: 新的使用次数
	void ResetUseCount(PooledElementType* ptr, int new_count) {
		if (ptr == nullptr) {
			throw std::invalid_argument("ptr cannot be nullptr");
		}
		// 调用成员函数版本，避免代码重复
		ptr->ResetUseCount(new_count);
	}

	// 获取当前内存块数量（用于统计和调试）
	size_t GetBlockCount() const {
		return block_count_;
	}

	// 获取当前块大小（用于统计和调试）
	size_t GetBlockSize() const {
		return block_size_;
	}

	// 获取当前总容量（块数量 × 块大小）
	size_t GetCapacity() const {
		return block_count_ * block_size_;
	}

	// 获取内存池占用的总内存字节数（包括所有块的数据、状态、控制信息等）
	// 返回: 所有内存块占用的总字节数
	// 计算包括：
	//   - 每个块的 statuses_ 数组内存
	//   - 每个块的 pooled_elements_ 数组内存
	//   - 每个块的 values_ 数组内存（包括对齐开销）
	//   - 每个块的 MemoryBlock 对象本身
	size_t GetMemoryByteSize() const {
		if (block_count_ == 0) {
			return 0;
		}

		// 计算单个块的内存大小
		size_t block_bytes = 0;

		// MemoryBlock 对象本身的大小
		block_bytes += sizeof(BlockType);

		// statuses_ vector 的内存（基于块的容量 block_size_）
		block_bytes += block_size_ * sizeof(typename BlockType::ElementStatusType);

		// pooled_elements_ vector 的内存（基于块的容量 block_size_）
		block_bytes += block_size_ * sizeof(typename BlockType::PooledElementType);

		// values_ 的内存（包括对齐开销）
		// values_ 使用手动对齐分配，实际分配的内存 = block_size * sizeof(ValueWrapperType) + (Alignment - 1 + sizeof(void*))
		size_t value_wrapper_size = sizeof(typename BlockType::ValueWrapperType);
		size_t total_size = block_size_ * value_wrapper_size;
		size_t extra_size = Alignment - 1 + sizeof(void*);
		block_bytes += total_size + extra_size;

		// 所有块的总内存 = 单个块的内存 × 块数量
		return block_bytes * block_count_;
	}

	// 设置每个元素的额外隐含内存（单位 Bytes），可在任意时刻调用
	// 用于 T 内含 std::vector、std::string 等时，估算其内部堆缓冲区的占用
	// 例如：T 内 vector<int> 平均 capacity 约 100，则设为 100*sizeof(int)=400
	// 仅影响析构时的内存显示：显示值 = GetMemoryByteSize() + capacity * extra_bytes_per_element
	// GetMemoryByteSize() 本身不变。默认 0 表示不估算额外内存
	void SetExtraBytesPerElement(size_t extra) { extra_bytes_per_element_ = extra; }

	// 获取当前设置的每元素额外隐含内存（单位 Bytes）
	size_t GetExtraBytesPerElement() const { return extra_bytes_per_element_; }

	// 返回包含每元素额外隐含内存的估算总内存（单位 Bytes）
	// 等价于 GetMemoryByteSize() + GetCapacity() * GetExtraBytesPerElement()
	// 当 T 内含 vector 等时，可用于查询更接近真实占用的估算值
	size_t GetMemoryByteSizeIncludingExtra() const {
		return GetMemoryByteSize() + GetCapacity() * extra_bytes_per_element_;
	}

	// 预留内存容量（可在构造后调用，用于动态扩展内存池容量）
	// reserve_size: 目标预留的内存大小（元素数量）
	//               如果不是 block_size 的整数倍，会向上取整到 block_size 的整数倍
	//               如果为 0，则不进行任何操作
	// 使用场景：
	//   - 构造时未预留，后续需要时才预留
	//   - 构造时预留不足，需要扩展容量
	//   - 根据运行时信息动态调整内存池大小（运行过程中手动扩容）
	// 注意：
	//   - 如果当前容量已经满足或超过 reserve_size，则不进行任何操作
	//   - 只有容量不足时，才会追加新的内存块到现有内存池，不会清空已有数据
	//   - 预留后不会重置当前块，保持当前分配状态，适合运行过程中扩容的场景
	//   - 预留后会预取当前块的即将可以用来分配的元素，减少下次分配时的缓存未命中
	// 命名说明：使用 ReserveMemory 而非 Preallocate，避免与 Allocate 混淆，且符合 C++ 标准库命名习惯（类似 std::vector::reserve）
	void ReserveMemory(size_t reserve_size) {
		if (reserve_size == 0) {
			return;
		}

		// 计算当前容量
		size_t current_capacity = GetCapacity();

		// 如果当前容量已经满足或超过目标大小，不需要追加
		if (current_capacity >= reserve_size) {
			return;
		}

		// 计算需要追加的大小
		size_t additional_size = reserve_size - current_capacity;

		// 追加新的内存块
		BatchAddNewBlocks(additional_size);

		// 预取当前块的即将可以用来分配的元素，减少下次分配时的缓存未命中
		// 这样更适合运行过程中手动扩容的场景，不会打断当前的分配流程
		// 注意：BatchAddNewBlocks 会确保至少有一个块，并且会设置当前块（如果之前没有块）
		BlockType* current = GetCurrentBlock();
		// 如果当前块为空（理论上不应该发生，但为了安全），使用头部块
		if (current == nullptr) {
			current = GetHeadBlock();
		}
		if (current != nullptr) {
			// 获取当前块的已使用数量，即下一个要分配的索引
			size_t next_index = GetUsedCount(current);
			// 检查索引是否有效
			if (next_index < current->statuses_.size()) {
				// 预取下一个要分配的状态和值
				__builtin_prefetch(&current->statuses_[next_index], 1, 1);
				if (current->values_ != nullptr) {
					__builtin_prefetch(&current->values_[next_index], 1, 1);
				}
			}
		}
	}

	// 扫描并回收已使用完毕的头部块
	// 将完全使用完毕的头部块移动到尾部作为新的空白块
	// 注意：通常不需要显式调用此函数，因为 Allocate() 在需要新块时会自动检查并循环利用
	// 此函数主要用于需要立即回收的场景（如定期清理、内存压力大时等）
	void RecycleBlocks() {
		BlockType* current = GetHeadBlock();
		while (current != nullptr) {
			// 检查当前块是否全部使用完毕
			size_t used = GetUsedCount(current);
			if (used == 0) {
				// 块未使用，跳过
				current = current->next;
				continue;
			}

		// 使用 in_use_count 快速检查是否所有元素都已释放（O(1) 操作）
		// 如果 in_use_count == 0，说明所有元素都不在使用中（即都已释放）
		// SOA 布局：使用 statuses_ 的大小（与 pooled_elements_ 和 values_ 相同）
		size_t in_use = current->LoadInUseCount();
		if (in_use == 0 && used == current->statuses_.size()) {
				// 头部块全部使用完毕，移动到尾部
				MoveHeadToTail();
				// 重新从头部开始检查
				current = GetHeadBlock();
			} else {
				// 当前块还未完全使用完毕，停止扫描
				break;
			}
		}
	}

private:
	// 计算大于等于 n 的最小 2 的幂
	// 用于优化取模运算（如果未来改为循环使用）和内存分配器友好性
	// 使用位操作优化，比循环更快
	static size_t RoundUpToPowerOfTwo(size_t n) {
		if (n == 0) {
			return 1;
		}
		// 如果已经是 2 的幂，直接返回
		if ((n & (n - 1)) == 0) {
			return n;
		}
		// 使用位操作找到最高位：n-- 然后找到最高位，左移 1 位
		// 这样可以避免循环，性能更好
		--n;
		n |= n >> 1;
		n |= n >> 2;
		n |= n >> 4;
		n |= n >> 8;
		n |= n >> 16;
		// 对于 64 位系统，需要额外的位移
		if (sizeof(size_t) > 4) {
			n |= n >> 32;
		}
		return n + 1;
	}

	// 计算合适的块大小
	// 如果 block_size 为 0，则根据 Value 大小和目标块大小自动计算
	// 否则使用指定的 block_size，但不会小于 MinBlockSize
	// 最终结果会向上取整到 2 的幂次，用于优化和内存分配器友好性
	static size_t CalculateBlockSize(size_t block_size) {
		size_t result;

		if (block_size > 0) {
			// 使用指定的块大小，但确保不小于最小块大小
			result = std::max(block_size, static_cast<size_t>(MinBlockSize));
		} else {
			// 自动计算块大小
			// MemoryBlock 的内存占用包括三个数组（每个数组大小都是 block_size）：
			//   1. statuses_ 数组：block_size * sizeof(ElementStatusType)（Alignment 字节/元素，对齐后）
			//   2. pooled_elements_ 数组：block_size * sizeof(PooledElementType)（Alignment 字节/元素，对齐后）
			//   3. values_ 数组：block_size * sizeof(ValueWrapperType)（用户数据，对齐后）
			// 每个元素的总内存占用 = sizeof(ElementStatusType) + sizeof(PooledElementType) + sizeof(ValueWrapperType)
			// 为了准确计算块大小，需要考虑所有三个数组的总大小
			// 注意：BlockType 和 PooledElementType 已在类中定义，可以直接使用
			using ElementStatusType = typename BlockType::ElementStatusType;
			using ValueWrapperType = typename BlockType::ValueWrapperType;

			// 计算每个元素的总内存占用
			size_t element_status_size = sizeof(ElementStatusType);  // Alignment 字节（对齐后）
			size_t pooled_element_size = sizeof(PooledElementType);  // Alignment 字节（对齐后）
			size_t value_wrapper_size = sizeof(ValueWrapperType);  // 用户数据大小（对齐后）
			size_t total_per_element = element_status_size + pooled_element_size + value_wrapper_size;

			// 根据目标块大小计算元素数量
			// 目标：每个块大约 TargetBlockBytes 字节（考虑所有三个数组的总大小）
			size_t calculated_size = TargetBlockBytes / total_per_element;

			// 确保不小于最小块大小
			result = std::max(calculated_size, static_cast<size_t>(MinBlockSize));
		}

		// 向上取整到 2 的幂次
		// 好处：
		// 1. 如果未来改为循环使用（环形缓冲区），可以使用位运算优化取模
		// 2. 内存分配器通常对 2 的幂次更友好
		return RoundUpToPowerOfTwo(result);
	}

	// 获取尾部块（根据 ThreadSafe 选择不同的实现）
	BlockType* GetTailBlock() {
		return GetTailBlockImpl(std::integral_constant<bool, ThreadSafe>());
	}

	// 多线程版本的获取
	BlockType* GetTailBlockImpl(std::true_type) {
		return tail_block_.load(std::memory_order_acquire);
	}

	// 单线程版本的获取
	BlockType* GetTailBlockImpl(std::false_type) {
		return GetTailBlockRef();
	}

	// 设置尾部块（根据 ThreadSafe 选择不同的实现）
	void SetTailBlock(BlockType* block) {
		SetTailBlockImpl(block, std::integral_constant<bool, ThreadSafe>());
	}

	// 多线程版本的设置
	void SetTailBlockImpl(BlockType* block, std::true_type) {
		tail_block_.store(block, std::memory_order_release);
	}

	// 单线程版本的设置
	void SetTailBlockImpl(BlockType* block, std::false_type) {
		GetTailBlockRef() = block;
	}

	// 获取已使用计数
	size_t GetUsedCount(BlockType* block) const {
		return block->LoadUsedCount();
	}

	// 尝试从指定块分配内存（SOA 布局：只访问小的 ElementStatus，性能与 Value 大小解耦）
	PooledElementType* TryAllocateFromBlock(BlockType* block, int initial_use_count) {
		if (unlikely(block == nullptr)) {
			return nullptr;
		}

		// 获取下一个分配索引（同时递增已使用计数）
		size_t index = block->FetchAddUsedCount(1);

		// 检查是否超出块大小（正常情况下不应该发生）
		// SOA 布局：使用 statuses_ 的大小（与 pooled_elements_ 和 values_ 相同）
		if (unlikely(index >= block->statuses_.size())) {
			// 回退计数（虽然已经超出，但保持一致性）
			block->FetchSubUsedCount(1);
			return nullptr;
		}

		// SOA 布局：返回 PooledElement 句柄（已预先建立与状态块和数据的关联）
		PooledElementType* result = &block->pooled_elements_[index];

		// 检查是否已经被使用（直接访问状态块，避免函数调用开销）
		// SOA 布局优势：只访问小的 ElementStatus，缓存友好
		// 如果已经被使用，理论上不应该发生，但为了安全需要检查
		// 使用友元直接访问，避免函数调用开销
		// 使用 relaxed 内存序，因为只是检查状态，不需要与其他操作同步
		if (unlikely(result->status_->in_use_.load(std::memory_order_relaxed))) {
			// 已经被使用，理论上不应该发生
			// 回退计数
			block->FetchSubUsedCount(1);
			return nullptr;
		}

		// 设置使用次数（使用 ResetUseCount 统一设置 remaining_uses 和 in_use）
		// ResetUseCount 会设置 in_use = true 并更新块的使用中计数
		// SOA 布局优势：ResetUseCount 只访问小的 ElementStatus，性能与 Value 大小解耦
		// 注意：Value 对象已经在创建 block 时构造好了，这里不需要再构造
		result->ResetUseCount(initial_use_count);

		// 预取本次分配的 status 和 value，减少外部使用时（访问 result->data）的缓存未命中
		// 参数说明：
		//   rw=1: 预取为写操作（分配后要写入数据）
		//   locality=1: 短期复用（外部很可能立即使用）
		// SOA 布局：预取当前分配的 status 和 value，提升外部访问性能
		__builtin_prefetch(&block->statuses_[index], 1, 1);
		if (block->values_ != nullptr) {
			__builtin_prefetch(&block->values_[index], 1, 1);
		}

		// 试探性预取下一个可能分配的状态块和值，减少下次分配时的缓存未命中
		// 参数说明：
		//   rw=1: 预取为写操作（分配后要写入数据）
		//   locality=1: 短期复用（下次分配很可能就是下一个元素）
		// SOA 布局优势：预取下一个元素的 status 和 value，提升下次分配性能
		if (index + 1 < block->statuses_.size()) {
			__builtin_prefetch(&block->statuses_[index + 1], 1, 1);
			if (block->values_ != nullptr) {
				__builtin_prefetch(&block->values_[index + 1], 1, 1);
			}
		}

		return result;
	}

	// 批量添加新的内存块（追加到现有内存池）
	// additional_size: 需要追加的内存大小（元素数量）
	//                  如果不是 block_size 的整数倍，会向上取整到 block_size 的整数倍
	//                  如果为 0，则不进行任何操作
	// 注意：此函数假设调用者已经检查过当前容量不足，只负责追加新块
	void BatchAddNewBlocks(size_t additional_size) {
		if (additional_size == 0) {
			return;
		}

		// 计算需要多少个块（向上取整）
		size_t num_blocks = (additional_size + block_size_ - 1) / block_size_;

		// 如果还没有第一个块，先创建第一个块
		if (GetHeadBlock() == nullptr) {
			BlockType* first_block = new BlockType(block_size_);
			SetHeadBlock(first_block);
			SetTailBlock(first_block);
			SetCurrentBlock(first_block);
			++block_count_;
			// 每次调用时更新，析构时的“初始值”为最后一次 BatchAddNewBlocks 调用后的 capacity 和 memory_byte_size
			initial_capacity_ = GetCapacity();
			initial_memory_byte_size_ = GetMemoryByteSize();
			// 如果只需要一个块，直接返回（预取由 ReserveMemory 负责）
			if (num_blocks == 1) {
				return;
			}
			// 否则继续创建剩余的块（从第二个块开始）
			num_blocks--;
		}

		// 创建新块并连接到尾部（追加时直接创建新块，不尝试循环利用）
		for (size_t i = 0; i < num_blocks; ++i) {
			BlockType* new_block = new BlockType(block_size_);
			BlockType* old_tail = GetTailBlock();
			if (old_tail != nullptr) {
				old_tail->next = new_block;
			}
			SetTailBlock(new_block);
			++block_count_;
		}

		// 每次调用时更新，析构时的“初始值”为最后一次 BatchAddNewBlocks 调用后的 capacity 和 memory_byte_size
		initial_capacity_ = GetCapacity();
		initial_memory_byte_size_ = GetMemoryByteSize();
		// 注意：预取逻辑由 ReserveMemory 函数负责，这里不进行预取
	}

	// 检查头部块是否全部使用完毕，如果是则循环利用
	// 返回: true 表示成功循环利用了头部块，false 表示头部块还未全部使用完毕
	// 性能优化：使用 in_use_count 计数器，避免遍历所有元素（O(1) vs O(n)）
	bool TryRecycleHeadBlock() {
		BlockType* head = GetHeadBlock();
		if (head == nullptr) {
			return false;
		}

		// 检查头部块是否全部使用完毕
		size_t used = GetUsedCount(head);
		if (used == 0) {
			// 块未使用，跳过
			return false;
		}

		// 使用 in_use_count 快速检查是否所有元素都已释放（O(1) 操作）
		// 如果 in_use_count == 0，说明所有元素都不在使用中（即都已释放）
		// SOA 布局：使用 statuses_ 的大小（与 pooled_elements_ 和 values_ 相同）
		size_t in_use = head->LoadInUseCount();
		if (in_use == 0 && used == head->statuses_.size()) {
			// 头部块全部使用完毕，循环利用它
			MoveHeadToTail();
			return true;
		}

		return false;
	}

	// 分配新的内存块并插入到链表尾部
	// 如果不是第一个块，会先尝试循环利用已使用完毕的头部块
	void AllocateNewBlock() {
		if (GetHeadBlock() == nullptr) {
			// 第一个块，直接创建
			BlockType* new_block = new BlockType(block_size_);
			SetHeadBlock(new_block);
			SetTailBlock(new_block);
			SetCurrentBlock(new_block);
			++block_count_;
			return;
		}

		// 不是第一个块，先尝试循环利用已使用完毕的头部块
		if (TryRecycleHeadBlock()) {
			// 成功循环利用了头部块，设置当前块为新的尾部块
			// 注意：循环利用不增加块数量，因为只是移动已有块
			BlockType* recycled_block = GetTailBlock();
			if (recycled_block != nullptr) {
				SetCurrentBlock(recycled_block);
			}
			return;
		}

		// 头部块还未全部使用完毕，创建新块
		BlockType* new_block = new BlockType(block_size_);
		BlockType* old_tail = GetTailBlock();
		if (old_tail != nullptr) {
			old_tail->next = new_block;
		}
		SetTailBlock(new_block);
		// 设置当前块为新创建的块
		SetCurrentBlock(new_block);
		++block_count_;

		// 预取新块的第一个状态块，减少首次分配时的缓存未命中
		// 参数说明：
		//   rw=1: 预取为写操作（分配后要写入数据）
		//   locality=1: 短期复用（即将分配）
		// SOA 布局优势：只预取小的 ElementStatus，缓存友好，预取开销小
		if (!new_block->statuses_.empty()) {
			__builtin_prefetch(&new_block->statuses_[0], 1, 1);
		}
	}

	// 重置块的状态（用于循环利用）
	void ResetBlockState(BlockType* block) {
		if (block == nullptr) {
			return;
		}

		// 重置块的状态
		block->StoreUsedCount(0);
		block->StoreInUseCount(0);

		// 注意：不需要重置每个元素的状态
		// 因为当块可以被循环利用时，in_use_count == 0 且 used == block->statuses_.size()
		// 这意味着所有元素的 in_use 都已经是 false，remaining_uses 都已经是 <= 0
		// 重复重置会浪费性能，特别是对于大块
		// SOA 布局：statuses_、values_ 和 pooled_elements_ 的大小相同
	}

	// 将头部块移动到尾部
	void MoveHeadToTail() {
		BlockType* head = GetHeadBlock();
		if (head == nullptr) {
			return;
		}

		BlockType* old_tail = GetTailBlock();
		if (old_tail == nullptr) {
			return;
		}

		// 如果只有一个块，只需要重置状态，无需移动
		if (head == old_tail) {
			ResetBlockState(head);
			return;
		}

		// 多个块的情况，需要移动
		BlockType* old_head = head;
		SetHeadBlock(old_head->next);
		old_head->next = nullptr;

		// 重置头部块的状态
		ResetBlockState(old_head);

		// 移动到尾部
		old_tail->next = old_head;
		SetTailBlock(old_head);
	}

	size_t block_size_;
	size_t block_count_;  // 当前内存块数量（用于快速获取，避免动态计数）
	size_t initial_capacity_;       // BatchAddNewBlocks 最后一次调用时的 capacity
	size_t initial_memory_byte_size_; // BatchAddNewBlocks 最后一次调用时的 memory_byte_size
	size_t extra_bytes_per_element_{0};  // 每元素额外隐含内存（如 vector 内部 buffer），通过 SetExtraBytesPerElement 设置
	std::string debug_name_;  // 诊断用实例标识，析构日志中为 <type-名称>
	// 统计：Allocate() 调用总次数
	// 根据 ThreadSafe 选择不同的实现：SAMU 用普通变量，MAMU 用原子变量以保证多线程安全
	typename std::conditional<ThreadSafe, std::atomic<uint64_t>, uint64_t>::type alloc_request_count_{0};
	// 根据 ThreadSafe 选择不同的头部块存储方式
	typename std::conditional<ThreadSafe,
		std::atomic<BlockType*>,
		BlockType*>::type head_block_;
	// 根据 ThreadSafe 选择不同的尾部块存储方式
	typename std::conditional<ThreadSafe,
		std::atomic<BlockType*>,
		BlockType*>::type tail_block_;
	// 当前正在分配的块指针（用于快速访问，避免每次都从头遍历链表）
	// 根据 ThreadSafe 选择不同的存储方式
	typename std::conditional<ThreadSafe,
		std::atomic<BlockType*>,
		BlockType*>::type current_block_;

	// 初始化 head_block_ 成员（根据 ThreadSafe 选择不同的初始化方式）
	void InitHeadBlock() {
		InitHeadBlockImpl(std::integral_constant<bool, ThreadSafe>());
	}

	// 多线程版本的初始化
	void InitHeadBlockImpl(std::true_type) {
		head_block_.store(nullptr, std::memory_order_release);
	}

	// 单线程版本的初始化
	void InitHeadBlockImpl(std::false_type) {
		GetHeadBlockRef() = nullptr;
	}

	// 获取头部块（根据 ThreadSafe 选择不同的实现）
	BlockType* GetHeadBlock() {
		return GetHeadBlockImpl(std::integral_constant<bool, ThreadSafe>());
	}

	// const 版本的获取头部块
	const BlockType* GetHeadBlock() const {
		return GetHeadBlockConstImpl(std::integral_constant<bool, ThreadSafe>());
	}

	// 多线程版本的获取
	BlockType* GetHeadBlockImpl(std::true_type) {
		return head_block_.load(std::memory_order_acquire);
	}

	// 单线程版本的获取
	BlockType* GetHeadBlockImpl(std::false_type) {
		return GetHeadBlockRef();
	}

	// const 版本的多线程获取
	const BlockType* GetHeadBlockConstImpl(std::true_type) const {
		return head_block_.load(std::memory_order_acquire);
	}

	// const 版本的单线程获取
	const BlockType* GetHeadBlockConstImpl(std::false_type) const {
		return *reinterpret_cast<const BlockType* const*>(&head_block_);
	}

	// 设置头部块（根据 ThreadSafe 选择不同的实现）
	void SetHeadBlock(BlockType* block) {
		SetHeadBlockImpl(block, std::integral_constant<bool, ThreadSafe>());
	}

	// 多线程版本的设置
	void SetHeadBlockImpl(BlockType* block, std::true_type) {
		head_block_.store(block, std::memory_order_release);
	}

	// 单线程版本的设置
	void SetHeadBlockImpl(BlockType* block, std::false_type) {
		GetHeadBlockRef() = block;
	}

	// 获取头部块的引用（用于单线程版本）
	BlockType*& GetHeadBlockRef() {
		return *reinterpret_cast<BlockType**>(&head_block_);
	}

	// 初始化 tail_block_ 成员（根据 ThreadSafe 选择不同的初始化方式）
	void InitTailBlock() {
		InitTailBlockImpl(std::integral_constant<bool, ThreadSafe>());
	}

	// 多线程版本的初始化
	void InitTailBlockImpl(std::true_type) {
		tail_block_.store(nullptr, std::memory_order_release);
	}

	// 单线程版本的初始化
	void InitTailBlockImpl(std::false_type) {
		GetTailBlockRef() = nullptr;
	}

	// 获取尾部块的引用（用于单线程版本）
	BlockType*& GetTailBlockRef() {
		return *reinterpret_cast<BlockType**>(&tail_block_);
	}

	// 初始化 current_block_ 成员（根据 ThreadSafe 选择不同的初始化方式）
	void InitCurrentBlock() {
		InitCurrentBlockImpl(std::integral_constant<bool, ThreadSafe>());
	}

	// 多线程版本的初始化
	void InitCurrentBlockImpl(std::true_type) {
		current_block_.store(nullptr, std::memory_order_release);
	}

	// 单线程版本的初始化
	void InitCurrentBlockImpl(std::false_type) {
		GetCurrentBlockRef() = nullptr;
	}

	// 获取当前块（根据 ThreadSafe 选择不同的实现）
	BlockType* GetCurrentBlock() {
		return GetCurrentBlockImpl(std::integral_constant<bool, ThreadSafe>());
	}

	// 多线程版本的获取
	BlockType* GetCurrentBlockImpl(std::true_type) {
		return current_block_.load(std::memory_order_acquire);
	}

	// 单线程版本的获取
	BlockType* GetCurrentBlockImpl(std::false_type) {
		return GetCurrentBlockRef();
	}

	// 设置当前块（根据 ThreadSafe 选择不同的实现）
	void SetCurrentBlock(BlockType* block) {
		SetCurrentBlockImpl(block, std::integral_constant<bool, ThreadSafe>());
	}

	// 多线程版本的设置
	void SetCurrentBlockImpl(BlockType* block, std::true_type) {
		current_block_.store(block, std::memory_order_release);
	}

	// 单线程版本的设置
	void SetCurrentBlockImpl(BlockType* block, std::false_type) {
		GetCurrentBlockRef() = block;
	}

	// 获取当前块的引用（用于单线程版本）
	BlockType*& GetCurrentBlockRef() {
		return *reinterpret_cast<BlockType**>(&current_block_);
	}

	// 递增 Allocate 调用计数（编译期根据 ThreadSafe 选择原子或非原子实现）
	void IncrementAllocRequestCount() {
		IncrementAllocRequestCountImpl(std::integral_constant<bool, ThreadSafe>());
	}

	void IncrementAllocRequestCountImpl(std::true_type) {
		alloc_request_count_.fetch_add(1, std::memory_order_relaxed);
	}

	void IncrementAllocRequestCountImpl(std::false_type) {
		++alloc_request_count_;
	}

	// 读取 Allocate 调用计数（析构时用于统计）
	uint64_t LoadAllocRequestCount() const {
		return LoadAllocRequestCountImpl(std::integral_constant<bool, ThreadSafe>());
	}

	uint64_t LoadAllocRequestCountImpl(std::true_type) const {
		return alloc_request_count_.load(std::memory_order_relaxed);
	}

	uint64_t LoadAllocRequestCountImpl(std::false_type) const {
		return alloc_request_count_;
	}
};

// 单线程申请、多线程使用版本（SAMU = Single Allocation Multi Use）
// 申请内存只能在单线程，但使用可以跨线程
// 使用方式与多线程版本完全相同，只是性能更好（无原子操作开销）
template <typename Value,
	size_t Alignment = 64,
	size_t TargetBlockBytes = 128 * 1024,
	size_t MinBlockSize = 64>
using SAMUMemoryPool = MemoryPoolBase<Value, false, Alignment, TargetBlockBytes, MinBlockSize>;

// 多线程申请、多线程使用版本（MAMU = Multi Allocation Multi Use）
// 申请内存可以跨线程，使用也可以跨线程
template <typename Value,
	size_t Alignment = 64,
	size_t TargetBlockBytes = 128 * 1024,
	size_t MinBlockSize = 64>
using MAMUMemoryPool = MemoryPoolBase<Value, true, Alignment, TargetBlockBytes, MinBlockSize>;

} // namespace fast_soa_memory_pool
} // namespace velapex
