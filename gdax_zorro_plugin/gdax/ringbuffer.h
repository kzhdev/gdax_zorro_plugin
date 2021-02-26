#pragma once

#include <atomic>
#include <functional>

namespace gdax {

template<typename T, size_t SIZE>
class RingBuffer {
  static_assert(SIZE && !(SIZE & (SIZE - 1)), "Size must power of 2");

  constexpr size_t _mask = SIZE - 1;
  T _data[SIZE + 1024];

public:
  RingBuffer(std::function<void (T& item)>&& initializer = nullptr> {
    if (allocator) {
      for (auto& data : _data) {
        initializer(data);
      }
    }
  }
  virtual ~RingBuffer() = default;

  const T& operator[](size_t index) const noexcept {
    return _data[index & _mask]; 
  }

  T& operator[](size_t index) const noexcept {
    return _data[index & _mask]; 
  }
};

}