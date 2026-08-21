#pragma once

// 注释约定：统一采用 // 注释，不使用 /** ... */ 格式的 docstring 注释
// 遵循 Google C++ 代码规范

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <memory>
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
// 示例1: 基本使用（单生产者、多消费者）
// ----------------------------------------------------------------------------
//
// struct MyData {
//     int value;
//     double price;
// };
//
// // 创建广播缓冲区（3 个消费者线程和 1 个生产者线程）
// // 无参构造：最大消费者数量为 1，块大小自动计算
// velapex::spmc_broadcast_buffer_recyclable::SPMCBroadcastBuffer<MyData> buffer;
// // 有参构造：最大消费者数量为 3，块大小自动计算
// velapex::spmc_broadcast_buffer_recyclable::SPMCBroadcastBuffer<MyData> buffer_with_consumers(3);
// // 指定预分配大小和块大小：最大消费者数量为 3，预分配 1000 个元素，块大小为 256
// velapex::spmc_broadcast_buffer_recyclable::SPMCBroadcastBuffer<MyData> buffer_with_prealloc(3, 1000, 256);
//
// // 生产者线程内推送数据
// MyData data1{100, 99.5};
// buffer.Push(data1);
// buffer.Push(MyData{200, 199.5});
//
// // 或者使用零拷贝方式
// buffer.GetWriteSlot() = MyData{300, 299.5};
// buffer.CommitWrite();
//
// // 消费者线程内部通过令牌读取数据
// auto token = buffer.RegisterConsumer();  // 注册消费者
// MyData received;
// while (true) {
//     if (buffer.TryRead(token, received)) {
//         std::cout << "Consumer read: value=" << received.value
//                   << ", price=" << received.price << std::endl;
//     }
//     // 检查是否完成所有数据的读取
//     if (buffer.IsConsumerFinished(token)) {
//         break;
//     }
// }
//
//
// 示例3: 多消费者场景
// ----------------------------------------------------------------------------
// // 创建支持 5 个消费者的缓冲区
// velapex::spmc_broadcast_buffer_recyclable::SPMCBroadcastBuffer<int> buffer(5);
//
// // 生产者线程推送数据
// for (int i = 0; i < 1000; ++i) {
//     buffer.Push(i);
// }
//
// // 每个消费者线程独立注册和读取
// // 消费者线程 1
// auto token1 = buffer.RegisterConsumer();
// int value1;
// while (buffer.TryRead(token1, value1)) {
//     // 处理数据
// }
//
// // 消费者线程 2
// auto token2 = buffer.RegisterConsumer();
// int value2;
// while (buffer.TryRead(token2, value2)) {
//     // 处理数据
// }
//
// // ... 其他消费者线程类似
//
// 示例4: 查询缓冲区状态
// ----------------------------------------------------------------------------
// velapex::spmc_broadcast_buffer_recyclable::SPMCBroadcastBuffer<int> buffer(3);
//
// // 推送一些数据
// for (int i = 0; i < 100; ++i) {
//     buffer.Push(i);
// }
//
// // 查询已写入的数据量
// size_t written_count = buffer.Size();  // 返回 100
//
// // 查询总容量
// size_t total_capacity = buffer.Capacity();  // 返回所有内存块的总大小
//
// // 检查消费者是否完成读取
// auto token = buffer.RegisterConsumer();
// bool finished = buffer.IsConsumerFinished(token);  // 如果消费者已读完所有数据，返回 true

namespace velapex {
namespace spmc_broadcast_buffer_recyclable {

// 定义缓存行大小
static constexpr size_t kCacheLineAlignSize = 64;

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

		// posix_memalign 的对齐参数须 ≥ alignof(T)。仅用模板 Alignment（默认 64）若小于 T 的自然对齐，
		// 在未对齐存储上构造/访问 T 属于 UB；GCC 11 等在 -O3 下可能用向量化初始化并以 SIGSEGV 暴露。
		size_t req_align = Alignment;
		const size_t natural_align = alignof(T);
		if (natural_align > req_align) {
			req_align = natural_align;
		}
		const size_t ptr_align = sizeof(void*);
		if (req_align < ptr_align) {
			req_align = ptr_align;
		}

		// 使用 posix_memalign 分配对齐的内存
		// posix_memalign 是 POSIX 标准，比 aligned_alloc 更通用（aligned_alloc 需要 C++17）
		void* ptr = nullptr;
		int result = posix_memalign(&ptr, req_align, total_bytes);

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

// 缓冲区的内存块，使用链表结构支持循环利用
// 每个块包含：数据数组、最后一个 slot 的消费计数、链表指针
// 优化：只维护最后一个 slot 的消费计数，因为如果最后一个 slot 被所有消费者访问完，说明整个块都被访问完
// 64 字节对齐确保每个 BufferBlock 独占一个缓存行，避免多线程访问时的伪共享
template <typename T>
struct alignas(64) BufferBlock {
	// 块结构体已 alignas(64)；元素使用默认 allocator（避免自定义分配器与 vector(n) 组合异常）。
	std::vector<T> data_;
	std::atomic<size_t> last_slot_consume_count_{0};  // 最后一个 slot 的消费计数（记录有多少消费者访问过最后一个 slot）
	BufferBlock* next;  // 链表指针

	explicit BufferBlock(size_t size) : data_(size), next(nullptr) {
		// 预热内存，避免首次访问触发 page fault
		// 优化：只预热第一个缓存行（64字节），而不是整个 block
		// 这样可以减少大 block 的预热开销，同时仍然能触发首次 page fault
		size_t total_bytes = data_.size() * sizeof(T);
		size_t bytes_to_touch = std::min(static_cast<size_t>(64), total_bytes);
		TouchMemory<64>(data_.data(), bytes_to_touch);
	}
};

namespace {
// C++11 下 alignas(64) 超过默认 operator new 对齐能力时，普通 new BufferBlock 可能未满足对齐；用 posix_memalign + placement new。
template <typename T>
inline BufferBlock<T>* CreateAlignedBufferBlock(size_t element_count) {
	using BB = BufferBlock<T>;
	const std::size_t alignment = alignof(BB);
	std::size_t nbytes = sizeof(BB);
	const std::size_t padded = (nbytes + alignment - 1u) & ~(alignment - 1u);
	void* mem = nullptr;
	if (posix_memalign(&mem, alignment, padded) != 0) {
		throw std::bad_alloc();
	}
	try {
		return ::new (mem) BB(element_count);
	} catch (...) {
		std::free(mem);
		throw;
	}
}

template <typename T>
inline void DestroyAlignedBufferBlock(BufferBlock<T>* p) noexcept {
	if (p == nullptr) {
		return;
	}
	p->~BufferBlock<T>();
	std::free(p);
}
}  // namespace

// 单进多出广播缓存的消费者令牌
// 每个消费者都有独立的状态，包括当前内存块指针、块内位置和已读取总数据量
// 优化：缓存数据指针，减少指针解引用开销
// 注意：如果 token 存储在容器中（如 vector），建议添加 alignas(64) 避免 false sharing
// 如果 token 只在栈上使用，则不需要对齐（每个线程的栈是独立的）
template <typename T>
struct SPMCBroadcastBufferConsumerToken {
	size_t consumer_id = std::numeric_limits<size_t>::max();  // 消费者编号
	BufferBlock<T>* read_block = nullptr;  // 当前读取的内存块指针
	T* data_ptr = nullptr;  // 缓存的数据数组指针，减少指针解引用
	size_t read_pos = 0;  // 当前块内读取位置
	size_t read_num = 0;  // 已读取总数据量
	size_t write_num_snapshot = 0;  // write_num_ 的缓存值，用于减少原子变量访问

	explicit SPMCBroadcastBufferConsumerToken(size_t p_consumer_id) : consumer_id(p_consumer_id) {}
	SPMCBroadcastBufferConsumerToken() = default;
};

// 单生产者多消费者 (SPMC) 的无锁广播缓存容器，支持 block 循环利用
// 该容器支持单个生产者线程和多个消费者线程
// 内部存储采用链表结构的内存块，支持循环利用已消费完毕的块
// 每个消费者线程都有独立的读取状态，消费者令牌会记录每个消费者的读取位置
// 每个数据会被每个消费者都读取一次
// 循环利用机制：只维护最后一个 slot 的消费计数，当头部 block 的最后一个 slot 被所有消费者访问完时，整个 block 可以被循环利用
// T: 存储的数据类型
// TargetBlockBytes: 目标块大小（字节），默认为128KB，用于动态计算块大小
// MinBlockSize: 最小块大小（元素数量），默认为64，确保块不会太小
// 注意：最终块大小会向上取整到 2 的幂次
template <typename T,
	size_t TargetBlockBytes = 128 * 1024,
	size_t MinBlockSize = 64>
class SPMCBroadcastBuffer {
public:

	// 构造函数
	// max_consumers: 最大消费者数量，默认为 1
	// preallocate_size: 预分配的内存大小（元素数量），默认为 0（不预分配），如果不是 block_size 的整数倍，会向上取整到 block_size 的整数倍
	// block_size: 如果为 0，则根据 T 大小和目标块大小自动计算，否则使用指定的 block_size（但不会小于 MinBlockSize）
	explicit SPMCBroadcastBuffer(size_t max_consumers = 1, size_t preallocate_size = 0, size_t block_size = 0)
			: max_consumers_(max_consumers),
			size_per_block_(CalculateBlockSize(block_size)),
			head_block_(nullptr),
			tail_block_(nullptr),
			write_block_(nullptr),
			block_count_(0),
			initial_capacity_(0),
			initial_memory_byte_size_(0) {
		if (max_consumers == 0) {
			throw std::invalid_argument("[SPMCBroadcastBuffer] Max consumers must be > 0");
		}
		if (size_per_block_ == 0) {
			throw std::invalid_argument("[SPMCBroadcastBuffer] block_size must be greater than 0");
		}

		// 如果指定了预分配大小，预先创建内存块
		if (preallocate_size > 0) {
			PreallocateBlocks(preallocate_size);
		} else {
			// 创建第一个内存块
			AllocateFirstBlock();
		}
		initial_capacity_ = Capacity();
		initial_memory_byte_size_ = GetMemoryByteSize();
	}

	// 析构函数：释放所有内存块
	~SPMCBroadcastBuffer() {
		size_t final_capacity = Capacity();
		size_t final_memory_byte_size = GetMemoryByteSize();
		std::string type_name = GetTypeNameFor<T>();
		const char* debug_part = debug_name_.empty() ? "unnamed" : debug_name_.c_str();
		std::string prefix = "~SPMCBroadcastBuffer(recyclable)";
		prefix += "<";
		if (!type_name.empty()) {
			prefix += type_name;
			prefix += "-";
		}
		prefix += debug_part;
		prefix += ">";

		const bool capacity_expanded = (final_capacity > initial_capacity_);
		// 与 fast_soa_memory_pool：扩容时 cerr 告警；cout 统计为 \033[1;93m / \033[0m，否则空串
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

		// 统计：基于 write_num_ 推导扩展次数、膨胀率、利用率
		if (size_per_block_ > 0) {
			uint64_t push_total = write_num_.load(std::memory_order_relaxed);
			size_t expansion_count = (final_capacity > initial_capacity_)
				? (final_capacity - initial_capacity_) / size_per_block_
				: 0;
			double expansion_rate_pct = (initial_capacity_ > 0)
				? (100.0 * static_cast<double>(final_capacity - initial_capacity_) / initial_capacity_)
				: 0.0;  // 膨胀率 = (最终容量 - 初始容量) / 初始容量
			double utilization_pct = (final_capacity > 0)
				? (100.0 * static_cast<double>(push_total) / final_capacity)
				: 0.0;
			std::ostringstream line;
			line << ansi_cout_color_set;
			line << prefix << ": push_requests=" << push_total
				<< ", expansions=" << expansion_count
				<< ", expansion_rate=" << std::fixed << std::setprecision(2) << expansion_rate_pct << "%"
				<< ", utilization=" << std::setprecision(2) << utilization_pct << "%";
			if (push_total > final_capacity) {
				const uint64_t reuse_ops = push_total - static_cast<uint64_t>(final_capacity);
				const size_t elem_payload = sizeof(T) + extra_bytes_per_element_;
				const double reuse_saved_mb =
					static_cast<double>(reuse_ops) * static_cast<double>(elem_payload)
					/ (1024.0 * 1024.0);
				line << ", saved_memory=" << std::fixed << std::setprecision(3) << reuse_saved_mb
					<< " MB";
			}
			line << ansi_cout_color_reset;
			std::cout << line.str() << std::endl;
		}

		BufferBlock<T>* current = head_block_;
		while (current != nullptr) {
			BufferBlock<T>* next = current->next;
			current->next = nullptr;
			DestroyAlignedBufferBlock(current);
			current = next;
		}
	}

	// 仅用于析构日志等诊断；未 Set 时 GetDebugName() 返回 "unnamed"
	void SetDebugName(std::string name) { debug_name_ = std::move(name); }
	std::string GetDebugName() const {
		return debug_name_.empty() ? std::string("unnamed") : debug_name_;
	}

	// 注册一个消费者，每个消费者线程只可以注册一次
	// 返回: 消费者令牌
	SPMCBroadcastBufferConsumerToken<T> RegisterConsumer() {
		// 此处只用于分配 ID，无需建立跨线程的 happens-before，所以使用 relaxed。
		size_t handle = next_consumer_id_.fetch_add(1, std::memory_order_relaxed);
		active_consumer_num_.fetch_add(1, std::memory_order_relaxed);
		if (handle >= max_consumers_) {
			throw std::runtime_error("[SPMCBroadcastBuffer] Max consumers exceeded");
		}
		SPMCBroadcastBufferConsumerToken<T> token(handle);
		// 优化：构造函数保证至少有一个block，所以可以直接初始化
		// 缓存数据指针，减少指针解引用开销
		token.read_block = head_block_;  // 初始化读取块为头部块
		token.data_ptr = token.read_block->data_.data();
		// 预取第一个数据元素，因为首次读取会立即访问
		// 构造函数保证size_per_block_ > 0（否则会抛异常）
		__builtin_prefetch(&token.data_ptr[0], 0, 1); // 预取为读操作，locality=1
		return token;
	}

	// 返回当前写指针所指向的槽位，供调用方零拷贝填充
	// 返回: 当前写入槽位的引用
	T& GetWriteSlot() {
		// 构造函数保证至少有一个block，write_block_不为空
		return write_block_->data_[write_pos_];
	}

	// 提交写入并前移写指针
	// release 语义确保消费者在 acquire 读取时能观测到最新数据，保持单生产者/多消费者的 happens-before 关系
	void CommitWrite() {
		// 更新下一次写入位置
		if (likely((write_pos_ + 1) != size_per_block_)) { // 还在当前内存块（热路径）
			++write_pos_;
			// 预取当前块的下一个 slot，减少下一次 GetWriteSlot() 的缓存未命中
			// 构造函数保证write_block_不为空，且write_pos_ < size_per_block_ == data_.size()
			__builtin_prefetch(&write_block_->data_[write_pos_], 1, 1); // 预取为写操作，locality=1
		} else { // 进入下一个内存块（冷路径）
			write_pos_ = 0;
			// 先检查当前块后面是否已经有块了（预分配或之前创建的）
			// 构造函数保证write_block_不为空
			if (write_block_->next != nullptr) {
				// 后面已经有块了，直接使用
				write_block_ = write_block_->next;
			} else {
				// 当前块是尾部块（write_block_->next == nullptr 意味着 write_block_ == tail_block_）
				// 需要创建新块或循环利用
				// AllocateNewBlock 会先尝试循环利用头部 block，如果失败则创建新 block
				AllocateNewBlock();
			}
			// 预取下一个块的第一个元素，减少下一次 GetWriteSlot() 的缓存未命中
			// AllocateNewBlock保证write_block_不为空
			if (likely(write_block_->next != nullptr)) {
				__builtin_prefetch(&write_block_->next->data_[0], 1, 1); // 预取为写操作，locality=1
			}
		}
		// 单生产者：写入完成后发布 write_num，消费者 acquire 即可看到最新数据。
		// 必须放在最后执行：跨块时先完成下一块分配/链接，再发布 write_num，
		// 确保消费者读到块尾时 AdvanceToken 也能顺利得到后续要读的块，而不是 nullptr 。
		// 缺点：需要等待下一块分配/链接完成才会更新计数，消费者才能获得最新情况，性能略差。
		write_num_.fetch_add(1, std::memory_order_release);
	}

	// 生产者推送数据到缓冲区
	void Push(const T& item) {
		GetWriteSlot() = item;
		CommitWrite();
	}

	// 生产者推送数据到缓冲区（移动语义）
	void Push(T&& item) {
		GetWriteSlot() = std::move(item);
		CommitWrite();
	}

	// 生产者就地构造并推送（完美转发，避免调用方先构造再 Push）
	template <typename... Args>
	void EmplacePush(Args&&... args) {
		GetWriteSlot() = T(std::forward<Args>(args)...);
		CommitWrite();
	}

	// 消费者尝试从缓冲区读取数据（拷贝式读取）
	// consumer_token: 消费者令牌
	// out: 输出参数，存储读取的数据
	// 返回: 是否成功读取数据
	bool TryRead(SPMCBroadcastBufferConsumerToken<T>& consumer_token, T& out) {
		// 如果消费者令牌中的编号不在预设范围则退出
		if (unlikely(consumer_token.consumer_id >= max_consumers_)) return false;

		// 优化：先检查本地缓存的 write_num_snapshot，减少原子变量访问
		// 如果 read_num >= write_num_snapshot，说明缓存可能过期，需要更新
		if (consumer_token.read_num >= consumer_token.write_num_snapshot) {
			// 更新缓存值（使用 acquire 语义确保看到最新的写入值）
			consumer_token.write_num_snapshot = write_num_.load(std::memory_order_acquire);
			// 如果已经读满，还没有新数据，则退出
			if (consumer_token.read_num == consumer_token.write_num_snapshot) return false;
		}

		// 读取新数据
		// 优化：使用缓存的数据指针，减少指针解引用
		out = consumer_token.data_ptr[consumer_token.read_pos];

		// 预取下一个可能读取的数据，减少下次读取时的缓存未命中
		if (likely(consumer_token.read_pos + 1 < size_per_block_)) {
			__builtin_prefetch(&consumer_token.data_ptr[consumer_token.read_pos + 1], 0, 1); // 预取为读操作，locality=1
		}

		// 推进消费者指针
		AdvanceToken(consumer_token);
		return true;
	}

	// 检查消费者是否完成所有数据的读取
	// consumer_token: 消费者令牌
	// 返回: 是否完成所有数据的读取
	bool IsConsumerFinished(SPMCBroadcastBufferConsumerToken<T>& consumer_token) const noexcept {
		// 使用 acquire 语义确保看到最新的写入值
		return consumer_token.read_num == write_num_.load(std::memory_order_acquire);
	}

	// 返回当前已写入的数据量（已发布的数据条数）
	// 返回: 已写入的数据条数
	size_t Size() const noexcept {
		return write_num_.load(std::memory_order_acquire);
	}

	// 返回缓冲区的总容量（所有内存块的总大小）
	// 返回: 总容量（数据条数）
	size_t Capacity() const noexcept {
		return block_count_ * size_per_block_;
	}

	// 返回缓冲区占用的总内存字节数（所有块的数据及块结构）
	size_t GetMemoryByteSize() const noexcept {
		if (block_count_ == 0) return 0;
		size_t per_block = sizeof(BufferBlock<T>) + size_per_block_ * sizeof(T);
		return per_block * block_count_;
	}

	// 设置每个元素的额外隐含内存（单位 Bytes），可在任意时刻调用
	// 用于 T 内含 std::vector、std::string 等时，估算其内部堆缓冲区的占用
	// 例如：T 内 vector<int> 平均 capacity 约 100，则设为 100*sizeof(int)=400
	// 仅影响析构时的内存显示：显示值 = GetMemoryByteSize() + capacity * extra_bytes_per_element
	// GetMemoryByteSize() 本身不变。默认 0 表示不估算额外内存
	void SetExtraBytesPerElement(size_t extra) noexcept { extra_bytes_per_element_ = extra; }

	// 获取当前设置的每元素额外隐含内存（单位 Bytes）
	size_t GetExtraBytesPerElement() const noexcept { return extra_bytes_per_element_; }

	// 返回包含每元素额外隐含内存的估算总内存（单位 Bytes）
	// 等价于 GetMemoryByteSize() + Capacity() * GetExtraBytesPerElement()
	// 当 T 内含 vector 等时，可用于查询更接近真实占用的估算值
	size_t GetMemoryByteSizeIncludingExtra() const noexcept {
		return GetMemoryByteSize() + Capacity() * extra_bytes_per_element_;
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
	// 如果 block_size 为 0，则根据 T 大小和目标块大小自动计算
	// 否则使用指定的 block_size，但不会小于 MinBlockSize
	// 最终结果会向上取整到 2 的幂次，用于优化和内存分配器友好性
	static size_t CalculateBlockSize(size_t block_size) {
		size_t result;

		if (block_size > 0) {
			// 使用指定的块大小，但确保不小于最小块大小
			result = (block_size < MinBlockSize) ? MinBlockSize : block_size;
		} else {
			// 自动计算块大小
			// 根据目标块大小计算元素数量
			// 目标：每个块大约 TargetBlockBytes 字节
			// 注意：每个 slot 只包含 T 类型的数据，消费计数是每个块共享的（只有一个）
			size_t calculated_size = TargetBlockBytes / sizeof(T);

			// 确保不小于最小块大小
			result = (calculated_size < MinBlockSize) ? MinBlockSize : calculated_size;
		}

		// 向上取整到 2 的幂次
		// 好处：
		// 1. 如果未来改为循环使用（环形缓冲区），可以使用位运算优化取模
		// 2. 内存分配器通常对 2 的幂次更友好
		return RoundUpToPowerOfTwo(result);
	}

	// 预分配内存块
	// preallocate_size: 预分配的内存大小（元素数量），如果不是 block_size 的整数倍，会向上取整到 block_size 的整数倍
	void PreallocateBlocks(size_t preallocate_size) {
		if (preallocate_size == 0) {
			return;
		}

		// 计算需要多少个块（向上取整）
		size_t num_blocks = (preallocate_size + size_per_block_ - 1) / size_per_block_;

		// 如果还没有第一个块，先创建第一个块
		if (head_block_ == nullptr) {
			AllocateFirstBlock();
		}

		// 创建剩余的块并连接好（预分配时直接创建新块，不尝试循环利用）
		// 已经有一个块了，需要创建 num_blocks - 1 个块
		// AllocateFirstBlock保证tail_block_不为空
		for (size_t i = 1; i < num_blocks; ++i) {
			BufferBlock<T>* new_block = CreateAlignedBufferBlock<T>(size_per_block_);
			tail_block_->next = new_block;
			tail_block_ = new_block;
			++block_count_;
		}
		// 预分配完成后，确保 write_block_ 指向第一个块
		// 如果head_block_ == nullptr，AllocateFirstBlock会设置write_block_
		// 如果head_block_ != nullptr，说明之前已经创建了block，write_block_应该已经被设置
		// 但为了安全，如果write_block_为空，设置为head_block_
		if (write_block_ == nullptr) {
			write_block_ = head_block_;
			write_pos_ = 0;
		}
	}

	// 推进消费者指针，并维护 read_num
	// read_num 用于"读完数据后是否可以安全退出"的判断，也可辅助统计消费进度
	// 优化：在块切换时更新上一个块的最后一个 slot 的消费计数
	void AdvanceToken(SPMCBroadcastBufferConsumerToken<T>& consumer_token) noexcept {
		++(consumer_token.read_num);
		// 更新下一次读取位置
		if (likely((consumer_token.read_pos + 1) != size_per_block_)) { // 还在当前内存块（热路径）
			++(consumer_token.read_pos);
		} else { // 进入下一个内存块（冷路径）
			// 优化：在块切换时更新上一个块的最后一个 slot 的消费计数
			// 由于所有消费者都按顺序访问数据，如果最后一个 slot 被所有消费者访问完，说明整个块都被访问完
			// RegisterConsumer保证read_block初始不为空，但块切换后可能变成nullptr（已读完所有块）
			BufferBlock<T>* current_block = consumer_token.read_block;
			if (likely(current_block != nullptr)) {
				consumer_token.read_block = current_block->next;
				// 优化：更新缓存的数据指针，减少后续的指针解引用
				if (likely(consumer_token.read_block != nullptr)) {
					consumer_token.data_ptr = consumer_token.read_block->data_.data();
					// 预取新块的第一个数据元素，减少块切换后首次读取的延迟
					// 构造函数保证size_per_block_ > 0
					__builtin_prefetch(&consumer_token.data_ptr[0], 0, 1); // 预取为读操作，locality=1
				} else {
					consumer_token.data_ptr = nullptr;
				}
				// 更新上一个块的最后一个 slot 的消费计数
				// 希望fetch_add在最后完成并且之前的写入可见，用memory_order_release
				current_block->last_slot_consume_count_.fetch_add(1, std::memory_order_release);
			}
			consumer_token.read_pos = 0;
		}
	}

	// 检查头部块是否可以被循环利用
	// 检查头部块的最后一个 slot 是否被所有消费者访问完
	// 由于所有消费者都按顺序访问数据，如果最后一个 slot 被所有消费者访问完，说明整个块的所有 slot 都被所有消费者访问完，可以循环利用
	// 返回: true 表示成功循环利用了头部块，false 表示头部块还未全部使用完毕
	// 注意：调用此函数时，必须已经存在至少一个块（通过 AllocateFirstBlock 创建）
	bool TryRecycleHeadBlock() {
		BufferBlock<T>* head = head_block_;
		// AllocateNewBlock只在至少有一个block时调用，所以head不为空

		// 预取可能访问的链表节点，减少链表操作的延迟
		if (head->next != nullptr) {
			__builtin_prefetch(head->next, 0, 1); // 预取为读操作，locality=1
		}
		// 如果head不为空，tail_block_也不应该为空（至少有一个block）
		__builtin_prefetch(tail_block_, 0, 1); // 预取为读操作，locality=1

		// 优化：使用缓存的活跃消费者数量（在所有消费者注册完后，数量不会变化）
		// 如果缓存值为 0（未初始化），则从原子变量加载并缓存
		size_t active_consumers = cached_active_consumer_num_;
		if (unlikely(active_consumers == 0)) {
			// 首次访问，从原子变量加载并缓存（使用 relaxed，因为此时所有消费者已注册完成）
			active_consumers = active_consumer_num_.load(std::memory_order_relaxed);
			cached_active_consumer_num_ = active_consumers;
			if (active_consumers == 0) {
				return false;
			}
		}

		// 检查头部块的最后一个 slot 是否被所有消费者访问完
		// 由于所有消费者都按顺序访问数据，如果最后一个 slot 被访问完，说明整个块都被访问完
		size_t consume_count = head->last_slot_consume_count_.load(std::memory_order_acquire);

		if (consume_count >= active_consumers) {
			// 头部块可以被循环利用
			MoveHeadToTail();
			return true;
		}

		return false;
	}

	// 将头部块移动到尾部，用于循环利用
	// 注意：调用此函数时，head_block_和tail_block_都不为空（TryRecycleHeadBlock已检查）
	void MoveHeadToTail() {
		BufferBlock<T>* head = head_block_;
		// TryRecycleHeadBlock已经检查了head不为空，且如果head不为空，tail_block_也不应该为空

		// 如果只有一个块，只需要重置状态，无需移动
		if (head == tail_block_) {
			ResetBlockState(head);
			return;
		}

		// 多个块的情况，需要移动
		BufferBlock<T>* old_head = head;
		head_block_ = old_head->next;
		old_head->next = nullptr;

		// 重置头部块的状态
		ResetBlockState(old_head);

		// 移动到尾部
		tail_block_->next = old_head;
		tail_block_ = old_head;
	}

	// 重置块的状态（用于循环利用）
	// 重置最后一个 slot 的消费计数为 0，确保 TryRecycleHeadBlock 的逻辑正确性
	// 注意：调用此函数时，block不为空（MoveHeadToTail已检查）
	void ResetBlockState(BufferBlock<T>* block) {
		// MoveHeadToTail调用时block不为空
		block->last_slot_consume_count_.store(0, std::memory_order_relaxed);
	}

	// 创建第一个内存块（仅在初始化时调用）
	// 优化：将创建第一个块的逻辑从 AllocateNewBlock 中拆分出来，简化 AllocateNewBlock 的逻辑
	void AllocateFirstBlock() {
		// 确保还没有第一个块
		if (head_block_ != nullptr) {
			return;
		}
		BufferBlock<T>* first_block = CreateAlignedBufferBlock<T>(size_per_block_);
		head_block_ = first_block;
		tail_block_ = first_block;
		write_block_ = first_block;
		write_pos_ = 0;
		++block_count_;
	}

	// 分配新的内存块并插入到链表尾部
	// 先尝试循环利用已使用完毕的头部块，如果失败则创建新块
	// 注意：调用此函数时，必须已经存在至少一个块（通过 AllocateFirstBlock 创建）
	void AllocateNewBlock() {
		// 先尝试循环利用已使用完毕的头部块
		if (TryRecycleHeadBlock()) {
			// 成功循环利用了头部块，设置当前块指向新的尾部块
			// 注意：循环利用不增加块数量，因为只是移动已有块
			write_block_ = tail_block_;
			write_pos_ = 0;
			return;
		}

		// 头部块还未全部使用完毕，创建新块
		// 调用此函数时，tail_block_不为空（至少有一个block）
		BufferBlock<T>* new_block = CreateAlignedBufferBlock<T>(size_per_block_);
		tail_block_->next = new_block;
		tail_block_ = new_block;
		write_block_ = new_block;
		write_pos_ = 0;
		++block_count_;

		// 预取新块的第一个元素，减少首次写入时的缓存未命中
		// 构造函数保证size_per_block_ > 0，所以data_不为空
		__builtin_prefetch(&new_block->data_[0], 1, 1); // 预取为写操作，locality=1
	}

	// 成员变量
	// 注意：const 成员变量和指针成员变量不需要对齐（不会导致 false sharing）
	// 只对齐频繁写入的原子变量和普通变量
	const size_t max_consumers_;  // 最大消费者数量
	const size_t size_per_block_;  // 每个内存块的大小
	alignas(kCacheLineAlignSize) std::atomic<size_t> active_consumer_num_{0};  // 当前注册的消费者数量
	alignas(kCacheLineAlignSize) std::atomic<size_t> next_consumer_id_{0};  // 下一个消费者的唯一标识符
	alignas(kCacheLineAlignSize) std::atomic<size_t> write_num_{0};  // 已写入的总数据量
	BufferBlock<T>* head_block_;  // 头部块指针（链表头）
	BufferBlock<T>* tail_block_;  // 尾部块指针（链表尾）
	BufferBlock<T>* write_block_;  // 当前写入块指针
	alignas(kCacheLineAlignSize) size_t write_pos_{0};  // 当前内存块内的写入位置（频繁写入）
	size_t block_count_{0};  // 当前内存块数量
	size_t initial_capacity_;       // 构造结束时的容量，析构时用于输出
	size_t initial_memory_byte_size_; // 构造结束时的内存字节数，析构时用于输出
	size_t extra_bytes_per_element_{0};  // 每元素额外隐含内存（如 vector 内部 buffer），通过 SetExtraBytesPerElement 设置
	std::string debug_name_;  // 诊断用实例标识，析构日志中为 <type-名称>
	alignas(kCacheLineAlignSize) size_t cached_active_consumer_num_{0};  // 缓存的活跃消费者数量（频繁读取）
};

// SPMCBroadcastBuffer 含 alignas(64) 成员；C++11 下普通 new / make_shared 不保证对象基址满足扩展对齐，须对齐堆分配。
template <typename T, typename... Args>
inline std::shared_ptr<SPMCBroadcastBuffer<T>> MakeAlignedSharedSpmc(Args&&... args) {
	using Buf = SPMCBroadcastBuffer<T>;
	const std::size_t alignment = alignof(Buf);
	std::size_t nbytes = sizeof(Buf);
	const std::size_t padded = (nbytes + alignment - 1u) & ~(alignment - 1u);
	void* mem = nullptr;
	if (posix_memalign(&mem, alignment, padded) != 0) {
		throw std::bad_alloc();
	}
	try {
		Buf* p = ::new (mem) Buf(std::forward<Args>(args)...);
		return std::shared_ptr<Buf>(p, [](Buf* ptr) noexcept {
			if (ptr == nullptr) {
				return;
			}
			ptr->~Buf();
			std::free(static_cast<void*>(ptr));
		});
	} catch (...) {
		std::free(mem);
		throw;
	}
}

} // namespace spmc_broadcast_buffer_recyclable
} // namespace velapex
