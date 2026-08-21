#pragma once
#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <new>
#include <stdexcept>
#include <sys/types.h>
#include <utility>

#ifndef likely
#define likely(x) __builtin_expect(!!(x), 1)
#endif

#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

namespace factors {
namespace demofw00 {
namespace tools {
namespace base {

template <typename T>
class RingBuffer {
private:
    T* _data;
    int32_t _capacity;
    int32_t _head;
    int32_t _tail;
    int32_t _count;
    ssize_t _head_idx;

    void _set_add_idx(int32_t& idx) {
        if(unlikely(idx + 1 == _capacity)) {
            idx = 0;
        } else {
            ++idx;
        }
    }

    void _set_sub_idx(int32_t& idx) {
        if(unlikely(idx == 0)) {
            idx = _capacity - 1;
        } else {
            --idx;
        }
    }

    void _throw_empty() const{
        if (unlikely(_count == 0)) {
            throw std::out_of_range("ring buffer is empty");
        }
    }

    void _recapacity() {
        if (unlikely(_count == _capacity)) {
            resize();
        }
    }

public:
    // 默认构造，提供合理的初始容量
    RingBuffer() : RingBuffer(1024) {}

    RingBuffer(int32_t initial_capacity) :
        _capacity(initial_capacity), _head(0), _tail(0), _count(0), _head_idx(0){
        _data = (T*)malloc(_capacity * sizeof(T));
        if (!_data) {
            throw std::bad_alloc();
        }
    }

    ~RingBuffer() {
        free(_data);
        _data = nullptr;
    }

    T& front() const{
        _throw_empty();

        return _data[_head];
    }

    T& back() const{
        _throw_empty();

        if(unlikely(_tail == 0)) {
            return _data[_capacity - 1];
        }
        return _data[_tail-1];
    }

    void push_front(T& value) {
        _recapacity();

        _set_sub_idx(_head);
        _data[_head] = value;

        ++_count;
        --_head_idx;
    }

    void push_front(T&& value) {
        push_front(value);
    }

    void push_back(const T& value) {
        _recapacity();

        _data[_tail] = value;
        _set_add_idx(_tail);

        ++_count;
    }

    void push_back(T&& value) {
        _recapacity();

        _data[_tail] = std::move(value);
        _set_add_idx(_tail);

        ++_count;
    }

    template<typename U>
    void emplace_back(U&& value) {
        _recapacity();

        _data[_tail] = std::forward<U>(value);
        _set_add_idx(_tail);

        ++_count;
    }

    void pop_front() {
        _throw_empty();

        _set_add_idx(_head);
        --_count;
        ++_head_idx;
    }

    void pop_back() {
        _throw_empty();

        _set_sub_idx(_tail);
        --_count;
    }

    // 有可能为负数，仅用于保存index，通过[index]找值使用
    ssize_t front_index() const {
        return _head_idx;
    }

    // 有可能为负数，仅用于保存index，通过[index]找值使用
    ssize_t back_index() const {
        return _head_idx + _count - 1;
    }

    int32_t size() const {
        return _count;
    }

    int32_t capacity() const {
        return _capacity;
    }

    bool empty() const {
        return _count == 0;
    }

    T& operator[](ssize_t pos) const {
        if (unlikely(pos >= _head_idx + _count)) {
            throw std::out_of_range("Index out of range");
        }

        if ((_capacity - _head) > (pos - _head_idx)) {
           return _data[_head + pos - _head_idx];
        }

        return _data[pos - _head_idx + _head - _capacity];
    }

    void resize() {
        int32_t new_capacity = _capacity * 2;
        T* new_data = (T*)realloc(_data, new_capacity * sizeof(T));

        if (!new_data) {
            throw std::bad_alloc();
        }

        _data = new_data;

        if (_count > 0 && _head >= _tail) {
            memcpy(_data + _capacity, _data, _tail * sizeof(T));
            _tail += _capacity;
        }

        _capacity = new_capacity;
    }

    RingBuffer(const RingBuffer& other):
        _capacity(other._capacity),
        _head(other._head),
        _tail(other._tail),
        _count(other._count),
        _head_idx(other._head_idx) {

        _data = (T*)malloc(_capacity * sizeof(T));
        if (!_data) {
            throw std::bad_alloc();
        }

        if(other.empty()) {
            return;
        }

        for(int32_t i = 0; i < other._count; ++i) {
            int32_t src_idx = other._head + i;
            if(src_idx >= other._capacity) {
                src_idx -= other._capacity;
            }
            _data[src_idx] = other._data[src_idx];
        }
    }

    RingBuffer& operator=(const RingBuffer& other) {
        if (this != &other) {  // 避免自赋值
            RingBuffer temp(other);  // 拷贝构造一个临时对象

            // 交换临时对象和当前对象的内容
            std::swap(_data, temp._data);
            std::swap(_capacity, temp._capacity);
            std::swap(_head, temp._head);
            std::swap(_tail, temp._tail);
            std::swap(_count, temp._count);
            std::swap(_head_idx, temp._head_idx);
        }
        return *this;
    }

    RingBuffer(RingBuffer&& other) noexcept :
        _data(other._data),
        _capacity(other._capacity),
        _head(other._head),
        _tail(other._tail),
        _count(other._count),
        _head_idx(other._head_idx) {

        other._data = nullptr;  // 防止其他对象释放内存
        other._capacity = 0;
        other._head = 0;
        other._tail = 0;
        other._count = 0;
        other._head_idx = 0;
    }

    // 移动赋值操作符
    RingBuffer& operator=(RingBuffer&& other) noexcept {
        if (this != &other) {
            if(_data) {
                free(_data);  // 释放当前对象的内存
                _data = nullptr;
            }

            _data = other._data;
            _capacity = other._capacity;
            _head = other._head;
            _tail = other._tail;
            _count = other._count;
            _head_idx = other._head_idx;

            other._data = nullptr;
            other._capacity = 0;
            other._head = 0;
            other._tail = 0;
            other._count = 0;
            other._head_idx = 0;
        }
        return *this;
    }

    void clear() {
        _head = 0;
        _tail = 0;
        _count = 0;
        _head_idx = 0;
    }
};

}  // namespace base
}  // namespace tools
}  // namespace demofw00
}  // namespace factors
