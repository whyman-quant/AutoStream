#pragma once

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>

// 仅在使用 typeid / demangle 时包含，避免 -fno-rtti 下误用 typeid 导致链接错误
#if defined(__GXX_RTTI) && (defined(__GNUC__) || defined(__clang__))
#include <cxxabi.h>
#include <typeinfo>
#elif defined(_CPPRTTI) && _CPPRTTI && defined(_MSC_VER)
#include <typeinfo>
#endif

namespace velapex {
namespace spsc_queue {

// 缓存行大小定义，通常为64字节
constexpr size_t kCacheLineSize = 64;

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

// 内部预热函数：触碰内存，触发 page fault，避免运行时首次访问延迟
namespace {
	inline void TouchMemory(void* ptr, size_t bytes) {
		if (ptr == nullptr || bytes == 0) {
			return;
		}
		constexpr size_t stride = kCacheLineSize;
		volatile char* data = static_cast<volatile char*>(ptr);
		for (size_t offset = 0; offset < bytes; offset += stride) {
			data[offset] = 0;
		}
		data[bytes - 1] = 0;
	}
}

template <typename T> class SPSCQueue {
private:
	// 缓存行对齐，防止false sharing
	alignas(kCacheLineSize) std::atomic<size_t> head_; // 读取位置
	alignas(kCacheLineSize) std::atomic<size_t> tail_; // 写入位置

	// 提高到buffer_size_将自动扩展到2的幂，优化取模操作
	size_t capacity_;
	size_t mask_;

	// 使用智能指针管理内存，避免vector的开销
	std::unique_ptr<T[]> buffer_;

	// 写操作失败计数：单生产者单消费者，仅生产者写 push，失败计数无需原子变量
	size_t push_fail_count_{0};

public:
	explicit SPSCQueue(size_t capacity = 1024) {
		if (capacity < 2) {
			throw std::invalid_argument("队列容量必须至少为2");
		}

		// 将容量调整为2的幂，优化取模运算
		capacity_ = 1;
		while (capacity_ < capacity) {
			capacity_ <<= 1;
		}

		mask_ = capacity_ - 1;
		buffer_ = std::unique_ptr<T[]>(new T[capacity_]);
		// 仅对平凡类型做按 cache line 写入预热：非平凡 T（例如含 std::vector 的元素）在
		// default-new 之后不得再按字节覆盖，否则会破坏对象表示（可表现为 std::bad_alloc）。
		// 使用 is_trivial：GCC 4.8 无 is_trivially_default_constructible。
		if (std::is_trivial<T>::value) {
			TouchMemory(buffer_.get(), capacity_ * sizeof(T));
		}
		head_.store(0, std::memory_order_relaxed);
		tail_.store(0, std::memory_order_relaxed);
	}

	~SPSCQueue() {
		if (push_fail_count_ > 0) {
			std::ostringstream msg;
			msg << "SPSCQueue: push failed " << push_fail_count_
				<< " time(s) (due to queue full), capacity=" << capacity_;
			std::string type_name = GetTypeNameFor<T>();
			if (!type_name.empty()) {
				msg << ", type=" << type_name;
			}
			std::cerr << msg.str() << std::endl;
		}
	}

	// 批量推送元素，减少原子操作次数
	template <typename Iterator> size_t PushBulk(Iterator begin, Iterator end) {
		const size_t requested = static_cast<size_t>(std::distance(begin, end));
		const size_t available = FreeSpace();
		const size_t count = std::min(available, requested);

		if (count == 0) {
			if (requested > 0)
				push_fail_count_ += requested;
			return 0;
		}
		if (count < requested)
			push_fail_count_ += (requested - count);

		size_t current_tail = tail_.load(std::memory_order_relaxed);

		for (size_t i = 0; i < count; ++i) {
			buffer_[current_tail & mask_] = *(begin + i);
			current_tail++;
		}

		// 只在最后更新一次tail，减少内存屏障
		tail_.store(current_tail, std::memory_order_release);
		return count;
	}

	// 使用位运算代替取模，优化索引计算
	bool Push(const T &item) {
		size_t current_tail = tail_.load(std::memory_order_relaxed);
		size_t next_tail = current_tail + 1;

		if ((next_tail & mask_) == (head_.load(std::memory_order_acquire) & mask_)) {
			++push_fail_count_;
			return false; // 队列已满
		}

		buffer_[current_tail & mask_] = item;

		// 使用内存预取，提示下一个位置可能即将使用
		__builtin_prefetch(&buffer_[(current_tail + 1) & mask_], 1, 0);

		tail_.store(next_tail, std::memory_order_release);
		return true;
	}

	// 同样优化移动语义版本的push
	bool Push(T &&item) {
		size_t current_tail = tail_.load(std::memory_order_relaxed);
		size_t next_tail = current_tail + 1;

		if ((next_tail & mask_) == (head_.load(std::memory_order_acquire) & mask_)) {
			++push_fail_count_;
			return false;
		}

		buffer_[current_tail & mask_] = std::move(item);

		// 使用内存预取
		__builtin_prefetch(&buffer_[(current_tail + 1) & mask_], 1, 0);

		tail_.store(next_tail, std::memory_order_release);
		return true;
	}

	// 使用emplace直接构造对象，避免不必要的拷贝
	template <typename... Args> bool EmplacePush(Args &&... args) {
		size_t current_tail = tail_.load(std::memory_order_relaxed);
		size_t next_tail = current_tail + 1;

		if ((next_tail & mask_) == (head_.load(std::memory_order_acquire) & mask_)) {
			++push_fail_count_;
			return false; // 队列已满
		}

		// 在队列空间中直接构造对象
		// 这里使用的是定位new（placement new），不涉及新内存申请，只是在已有的 buffer_ 空间上构造对象
		new (&buffer_[current_tail & mask_]) T(std::forward<Args>(args)...);

		tail_.store(next_tail, std::memory_order_release);
		return true;
	}

	// 优化pop操作
	bool Pop(T &item) {
		size_t current_head = head_.load(std::memory_order_relaxed);

		if (current_head == tail_.load(std::memory_order_acquire)) {
			return false; // 队列为空
		}

		item = std::move(buffer_[current_head & mask_]);

		// 预取下一个可能要读取的元素
		__builtin_prefetch(&buffer_[(current_head + 1) & mask_], 0, 0);

		head_.store(current_head + 1, std::memory_order_release);
		return true;
	}

	// 批量弹出元素，减少原子操作次数
	template <typename OutputIterator> size_t PopBulk(OutputIterator dest, size_t max_items) {
		size_t current_head = head_.load(std::memory_order_relaxed);
		size_t current_tail = tail_.load(std::memory_order_acquire);

		size_t available = current_tail - current_head;
		size_t count = std::min(available, max_items);

		if (count == 0)
			return 0;

		for (size_t i = 0; i < count; ++i) {
			*(dest + i) = std::move(buffer_[(current_head + i) & mask_]);
		}

		// 只在最后更新一次head
		head_.store(current_head + count, std::memory_order_release);
		return count;
	}

	// 可用空间估计更精确
	size_t FreeSpace() const {
		size_t head = head_.load(std::memory_order_relaxed);
		size_t tail = tail_.load(std::memory_order_relaxed);
		return capacity_ - (tail - head);
	}

	// 高效清空操作
	void Clear() { head_.store(tail_.load(std::memory_order_acquire), std::memory_order_release); }

	// 其他辅助方法（Size和Empty）
	bool Empty() const {
		return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
	}

	size_t Size() const {
		return tail_.load(std::memory_order_acquire) - head_.load(std::memory_order_acquire);
	}
};

} // namespace spsc_queue
} // namespace velapex
