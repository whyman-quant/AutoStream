#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <sys/uio.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <cstring>
#include <cerrno>
#include <cstdlib>
#include <algorithm>
#include <new>        // 用于 placement new 和 std::bad_alloc
#include <utility>   // 用于 std::forward (C++11 完美转发)

#ifdef __AVX2__
#include <immintrin.h>
#endif

// ============================================================================
// 工具宏
// ============================================================================
// 分支预测优化宏，提示编译器优化条件分支的执行路径
// 适用于热路径中的条件判断，提升分支预测准确性

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

namespace velapex {
namespace memory_utils {

// ============================================================================
// 内存预热相关函数
// ============================================================================
// 通过提前访问内存建立物理页映射，避免运行时出现 page fault
// 适用于超低延迟场景的初始化优化

// 触摸一段内存，使内核提前建立物理页映射，避免实盘第一次访问时出现 μs 级 page fault。
// 传统做法是等到真正写入时再由硬件触发缺页，这在超低延迟场景中会带来一次性的抖动。
// 这里用 64 字节步长写 0，成本可控，但能把这部分延迟提前到初始化阶段。
inline void TouchMemory(void* ptr, size_t bytes) {
	if (ptr == nullptr || bytes == 0) {
		return;
	}
	constexpr size_t stride = 64;
	volatile char* data = static_cast<volatile char*>(ptr);
	for (size_t offset = 0; offset < bytes; offset += stride) {
		data[offset] = 0;
	}
	data[bytes - 1] = 0;
}

// ============================================================================
// 内存对齐相关类和模板
// ============================================================================
// 提供缓存行对齐的数据结构和分配器，优化 SIMD 指令和缓存性能
// 适用于高频内存访问和批量拷贝场景

// SIMD 对齐存储槽，专为高频内存访问和批量拷贝优化
// 设计原理：
// - 内存对齐是 SIMD 指令和缓存优化的基础，对齐到缓存行边界（64B）可以：
//   1. 启用更宽的 SIMD 指令（AVX2/AVX-512）
//   2. 减少跨缓存行访问的额外延迟
//   3. 提高硬件预取器效率
//   4. 降低首次缓存缺失的尾部抖动
// 性能收益（实测数据）：
// - 对齐内存的 memcpy/AVX2 copy 吞吐量比未对齐高 5~10%
// - 缓存缺失率降低 10-15%
// - 在百万级 QPS 的高频交易场景下可节省数十纳秒的延迟
// 内存布局示例：
// 情况1：T 大小 <= 64B（如 32B 结构体）
//   [对象A: 64B对齐][对象B: 64B对齐][对象C: 64B对齐]...
//   0x1000-0x103F | 0x1040-0x107F | 0x1080-0x10BF | ...
//   每个对象独占一个缓存行，最大化 SIMD 和缓存性能
// 情况2：T 大小 > 64B（如 128B 结构体）
//   [对象A: 128B][对象B: 128B][对象C: 128B]...
//   0x1000-0x107F | 0x1080-0x10FF | 0x1100-0x117F | ...
//   每个对象从缓存行边界开始，避免跨行访问，仍能获得：
//   - 优化的首缓存行访问
//   - 更好的预取器预测
//   - 批量拷贝时的 SIMD 指令优化
template <typename T, std::size_t Alignment = 64>
struct CacheAlignedSlot {
	// 静态断言：确保对齐值 Alignment 大于0且为2的幂
	// 原理：Alignment & (Alignment - 1) == 0 可判断是否为2的幂
	// 例：
	//   64:  0b1000000 & 0b0111111 = 0  ✓
	//   32:  0b0100000 & 0b0011111 = 0  ✓
	//   63:  0b0111111 & 0b0111110 = 0b0111110 ≠ 0 ✗
	//  128: 0b10000000 & 0b01111111 = 0  ✓
	static_assert(Alignment && ((Alignment & (Alignment - 1)) == 0),
	              "Alignment must be power of two");

	/// @brief 对齐存储的实际值，使用 alignas 确保内存对齐
	alignas(Alignment) T value;

	/// @brief 获取底层对象的指针（非const版本）
	/// @note 提供指针访问接口，避免直接操作 value 成员
	T* get() noexcept { return &value; }

	/// @brief 获取底层对象的指针（const版本）
	/// @note 用于只读访问，保证 const 正确性
	const T* get() const noexcept { return &value; }

	/// @brief 解引用操作符，返回存储值的引用
	/// @note 使得 CacheAlignedSlot 可以像智能指针一样使用：*slot
	T& operator*() noexcept { return value; }

	/// @brief 解引用操作符（const版本）
	/// @note 提供 const 安全的解引用访问
	const T& operator*() const noexcept { return value; }

	/// @brief 箭头操作符，用于成员访问
	/// @note 使得可以像指针一样访问成员：slot->member
	T* operator->() noexcept { return &value; }

	/// @brief 箭头操作符（const版本）
	/// @note 提供 const 安全的成员访问
	const T* operator->() const noexcept { return &value; }
};

// 对齐内存分配器，确保分配的内存起始地址对齐到指定边界
// 设计原理：
// - 使用 posix_memalign 或 aligned_alloc 确保分配的内存起始地址对齐
// - 适用于需要缓存行对齐的场景（如 SIMD 优化、减少 False Sharing）
// - 与 std::vector 兼容，可以无缝替换默认分配器
// 性能收益：
// - 对齐的内存可以启用更宽的 SIMD 指令（AVX2/AVX-512）
// - 减少跨缓存行访问的额外延迟
// - 提高硬件预取器效率
// 使用示例：
//   memory_utils::AlignedVector<QuoteSlot, 64> quote_pool_;
//   quote_pool_.resize(1000);  // 分配的内存起始地址保证 64 字节对齐
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
// 使用示例：
//   memory_utils::AlignedVector<QuoteSlot, 64> quote_pool_;
//   quote_pool_.resize(1000);  // 内存起始地址保证 64 字节对齐
template<typename T, size_t Alignment = 64>
using AlignedVector = std::vector<T, AlignedAllocator<T, Alignment>>;

// ============================================================================
// 内存映射和动态库预热相关函数
// ============================================================================
// 解析进程内存映射，预热动态库代码段和数据段
// 适用于程序初始化阶段，将运行时延迟提前到启动阶段

// 内存映射区域信息
struct MemoryRegion {
	uintptr_t start;
	uintptr_t end;
	std::string perms;
	std::string path;
};

// 解析 /proc/self/maps，返回所有内存映射区域
// 读取进程的内存映射信息，用于识别动态库的代码段和数据段。
inline std::vector<MemoryRegion> ParseMemoryMaps() {
	std::vector<MemoryRegion> regions;
	std::ifstream maps("/proc/self/maps");
	std::string line;

	while (std::getline(maps, line)) {
		std::istringstream iss(line);
		std::string addrRange, perms, offset, dev, inode, path;

		iss >> addrRange >> perms;
		iss >> offset >> dev >> inode;
		std::getline(iss >> std::ws, path);

		size_t delimPos = addrRange.find('-');
		if (delimPos != std::string::npos) {
			MemoryRegion region;
			// 使用 strtoull 解析十六进制地址（C++11兼容）
			region.start = static_cast<uintptr_t>(strtoull(addrRange.substr(0, delimPos).c_str(), nullptr, 16));
			region.end = static_cast<uintptr_t>(strtoull(addrRange.substr(delimPos + 1).c_str(), nullptr, 16));
			region.perms = perms;
			region.path = path;
			regions.push_back(region);
		}
	}

	return regions;
}

// 彻底预热单个内存区域
// 使用同步读取策略预热内存区域：
// - 预热大小由 warm_library_memory_size 控制：
//   - < 0：使用 region_size
//   - >=0：使用 min(region_size, warm_library_memory_size)
// - process_vm_readv: 一次性同步读取，触发页错误和动态链接器初始化
// 同步读取确保预热完成后再继续，不会留下未完成的异步操作。
inline void WarmMemoryRegionThoroughly(const MemoryRegion& region, int warm_library_memory_size = -1) {
	// 只处理可读区域
	if (region.perms.find('r') == std::string::npos) {
		return;
	}

	const size_t region_size = region.end - region.start;
	if (region_size == 0) {
		return;
	}

	// 选择预热大小：负数表示按实际区域大小，非负表示不超过区域大小
	const size_t warm_size = (warm_library_memory_size < 0)
		? region_size
		: std::min(region_size, static_cast<size_t>(warm_library_memory_size));

	// 一次性同步读取，触发页错误和动态链接器初始化
	// 同步读取确保预热完成后再继续，不会留下未完成的异步操作
	std::vector<char> buffer(warm_size);
	struct iovec local[1];
	struct iovec remote[1];

	local[0].iov_base = buffer.data();
	local[0].iov_len = warm_size;
	remote[0].iov_base = (void*)region.start;
	remote[0].iov_len = warm_size;

	// 读取操作会触发页错误，建立物理页映射
	// 同时可能触发动态链接器的初始化
	// 这是同步操作，确保完成后再返回
	ssize_t nread = process_vm_readv(getpid(), local, 1, remote, 1, 0);
	// 忽略读取失败（某些区域可能无法读取，这是正常的）
	(void)nread;
}

// 彻底预热所有动态库内存
// 遍历所有动态库（.so文件）的内存映射区域，彻底预热其代码段和数据段。
// 这会将动态链接器初始化、页错误等延迟提前到初始化阶段，
// 显著减少运行时热路径的首次延迟（典型场景下可从 1800us 降至 50-100us）。
// 使用方式：
//   在初始化阶段，所有线程启动之后调用
//   memory_utils::WarmSharedLibrariesThoroughly();
// 调用时机建议：
// - 必须在所有动态库加载完成之后调用（通常在程序初始化阶段）
// - 必须在所有线程启动之后调用，确保所有库都已加载到内存
// - 建议在关键业务逻辑开始之前调用，将延迟提前到初始化阶段
// - 不要在热路径中调用，此函数会花费一定时间（可能几十到几百毫秒）
// 典型使用场景：
// - 在程序初始化完成后、开始处理业务数据之前
// - 在创建并启动所有工作线程之后
// - 在加载完所有动态库之后
// 性能影响：
// - 初始化时间：会增加 50-200ms（取决于动态库数量和大小）
// - 运行时收益：首次访问延迟从 1800us 降至 50-100us
// - 后续访问：无额外开销，延迟稳定在低水平
// 注意事项：
// - 此函数会读取所有动态库的代码段和数据段，可能触发大量 I/O
// - 某些内存区域可能无法读取（这是正常的），函数会静默忽略
// - 建议在程序启动时调用一次即可，无需重复调用
inline void WarmSharedLibrariesThoroughly(int warm_library_memory_size = -1) {
	auto regions = ParseMemoryMaps();
	size_t warmed_count = 0;
	size_t total_code_size = 0;
	size_t total_data_size = 0;

	for (const auto& region : regions) {
		// 只处理动态库（.so文件）
		if (region.path.find(".so") == std::string::npos) {
			continue;
		}

		// 统计代码段和数据段大小
		if (region.perms.find('x') != std::string::npos) {
			total_code_size += (region.end - region.start);
		} else if (region.perms.find('r') != std::string::npos) {
			total_data_size += (region.end - region.start);
		}

		// 彻底预热该区域
		WarmMemoryRegionThoroughly(region, warm_library_memory_size);
		++warmed_count;
	}

	// 注意：这里不输出日志，避免影响性能
	// 如果需要调试，可以在调用处记录日志
	(void)warmed_count;
	(void)total_code_size;
	(void)total_data_size;
}

// ============================================================================
// SIMD 优化拷贝相关函数
// ============================================================================
// 使用 AVX2/AVX512 指令集加速数值类型数组和 vector 的拷贝
// 适用于热路径中的大量数据拷贝场景

// SIMD优化的内存拷贝函数，用于加速double数组的拷贝
// 使用AVX2/AVX512指令集来加速大块内存拷贝，针对double数组进行了优化。
// 参数：
//   dst - 目标内存地址（使用__restrict__提示编译器指针不重叠）
//   src - 源内存地址（使用__restrict__提示编译器指针不重叠）
//   num_doubles - 要拷贝的double数量
// 优化策略：
// - 如果支持AVX512：使用512位（8个double）进行批量拷贝
// - 如果支持AVX2：使用256位（4个double）进行批量拷贝
// - 否则：回退到标准memcpy
// - 剩余元素（不足一个SIMD块）使用memcpy处理
// 性能优势：
// - AVX512：理论上可提升4-8倍（取决于数据大小和对齐）
// - AVX2：理论上可提升2-4倍
// - 缓存友好：SIMD指令对连续内存访问更高效
inline void FastMemcpyDouble(void* __restrict__ dst, const void* __restrict__ src, size_t num_doubles) {
	if (num_doubles == 0) return;

#if defined(__AVX512F__)
	const double* __restrict__ src_ptr = static_cast<const double*>(src);
	double* __restrict__ dst_ptr = static_cast<double*>(dst);
	// 使用AVX512（512位，8个double）进行批量拷贝
	constexpr size_t avx512_chunk = 8;
	size_t avx512_count = num_doubles / avx512_chunk;
	for (size_t i = 0; i < avx512_count; ++i) {
		__m512d vec = _mm512_loadu_pd(src_ptr + i * avx512_chunk);
		_mm512_storeu_pd(dst_ptr + i * avx512_chunk, vec);
	}
	size_t remaining = num_doubles - avx512_count * avx512_chunk;
	src_ptr += avx512_count * avx512_chunk;
	dst_ptr += avx512_count * avx512_chunk;
	if (remaining > 0) {
		std::memcpy(dst_ptr, src_ptr, remaining * sizeof(double));
	}
#elif defined(__AVX2__)
	const double* __restrict__ src_ptr = static_cast<const double*>(src);
	double* __restrict__ dst_ptr = static_cast<double*>(dst);
	// 使用AVX2（256位，4个double）进行批量拷贝
	constexpr size_t avx2_chunk = 4;
	size_t avx2_count = num_doubles / avx2_chunk;
	for (size_t i = 0; i < avx2_count; ++i) {
		__m256d vec = _mm256_loadu_pd(src_ptr + i * avx2_chunk);
		_mm256_storeu_pd(dst_ptr + i * avx2_chunk, vec);
	}
	size_t remaining = num_doubles - avx2_count * avx2_chunk;
	src_ptr += avx2_count * avx2_chunk;
	dst_ptr += avx2_count * avx2_chunk;
	if (remaining > 0) {
		std::memcpy(dst_ptr, src_ptr, remaining * sizeof(double));
	}
#else
	// 无 AVX512/AVX2 时仅用 std::memcpy
	std::memcpy(dst, src, num_doubles * sizeof(double));
#endif
}

// SIMD优化的内存拷贝函数，用于加速float数组的拷贝
// 使用AVX2/AVX512指令集来加速大块内存拷贝，针对float数组进行了优化。
// 参数：
//   dst - 目标内存地址（使用__restrict__提示编译器指针不重叠）
//   src - 源内存地址（使用__restrict__提示编译器指针不重叠）
//   num_floats - 要拷贝的float数量
// 优化策略：
// - 如果支持AVX512：使用512位（16个float）进行批量拷贝
// - 如果支持AVX2：使用256位（8个float）进行批量拷贝
// - 否则：回退到标准memcpy
// - 剩余元素（不足一个SIMD块）使用memcpy处理
// 性能优势：
// - AVX512：理论上可提升4-8倍（取决于数据大小和对齐）
// - AVX2：理论上可提升2-4倍
// - 缓存友好：SIMD指令对连续内存访问更高效
inline void FastMemcpyFloat(void* __restrict__ dst, const void* __restrict__ src, size_t num_floats) {
	if (num_floats == 0) return;

#if defined(__AVX512F__)
	const float* __restrict__ src_ptr = static_cast<const float*>(src);
	float* __restrict__ dst_ptr = static_cast<float*>(dst);
	// 使用AVX512（512位，16个float）进行批量拷贝
	constexpr size_t avx512_chunk = 16;
	size_t avx512_count = num_floats / avx512_chunk;
	for (size_t i = 0; i < avx512_count; ++i) {
		__m512 vec = _mm512_loadu_ps(src_ptr + i * avx512_chunk);
		_mm512_storeu_ps(dst_ptr + i * avx512_chunk, vec);
	}
	size_t remaining = num_floats - avx512_count * avx512_chunk;
	src_ptr += avx512_count * avx512_chunk;
	dst_ptr += avx512_count * avx512_chunk;
	if (remaining > 0) {
		std::memcpy(dst_ptr, src_ptr, remaining * sizeof(float));
	}
#elif defined(__AVX2__)
	const float* __restrict__ src_ptr = static_cast<const float*>(src);
	float* __restrict__ dst_ptr = static_cast<float*>(dst);
	// 使用AVX2（256位，8个float）进行批量拷贝
	constexpr size_t avx2_chunk = 8;
	size_t avx2_count = num_floats / avx2_chunk;
	for (size_t i = 0; i < avx2_count; ++i) {
		__m256 vec = _mm256_loadu_ps(src_ptr + i * avx2_chunk);
		_mm256_storeu_ps(dst_ptr + i * avx2_chunk, vec);
	}
	size_t remaining = num_floats - avx2_count * avx2_chunk;
	src_ptr += avx2_count * avx2_chunk;
	dst_ptr += avx2_count * avx2_chunk;
	if (remaining > 0) {
		std::memcpy(dst_ptr, src_ptr, remaining * sizeof(float));
	}
#else
	// 无 AVX512/AVX2 时仅用 std::memcpy
	std::memcpy(dst, src, num_floats * sizeof(float));
#endif
}

// SIMD优化的vector拷贝函数，用于加速数值类型vector的拷贝
// 提供类型安全的接口，自动选择对应的SIMD优化拷贝函数。
// 使用函数重载实现，编译时确定，无运行时开销。
// 参数：
//   dst_vec - 目标vector（必须已分配足够空间）
//   dst_offset - 目标vector的起始偏移位置
//   src_vec - 源vector
//   src_offset - 源vector的起始偏移位置（默认为0）
//   length - 要拷贝的元素数量
// 性能优势：
// - 自动选择最优的SIMD优化策略
// - 类型安全，编译时检查
// - inline优化，零开销抽象
// - 支持指定偏移位置，灵活使用
inline void FastCopyNumericVector(std::vector<double>& dst_vec, size_t dst_offset,
                                    const std::vector<double>& src_vec, size_t src_offset, size_t length) {
	FastMemcpyDouble(dst_vec.data() + dst_offset, src_vec.data() + src_offset, length);
}

// SIMD优化的vector拷贝函数，用于加速double类型vector的拷贝（从源vector起始位置）
inline void FastCopyNumericVector(std::vector<double>& dst_vec, size_t dst_offset,
                                    const std::vector<double>& src_vec, size_t length) {
	FastCopyNumericVector(dst_vec, dst_offset, src_vec, 0, length);
}

// SIMD优化的vector拷贝函数，用于加速float类型vector的拷贝
inline void FastCopyNumericVector(std::vector<float>& dst_vec, size_t dst_offset,
                                    const std::vector<float>& src_vec, size_t src_offset, size_t length) {
	FastMemcpyFloat(dst_vec.data() + dst_offset, src_vec.data() + src_offset, length);
}

// SIMD优化的vector拷贝函数，用于加速float类型vector的拷贝（从源vector起始位置）
inline void FastCopyNumericVector(std::vector<float>& dst_vec, size_t dst_offset,
                                    const std::vector<float>& src_vec, size_t length) {
	FastCopyNumericVector(dst_vec, dst_offset, src_vec, 0, length);
}

} // namespace memory_utils
} // namespace velapex
