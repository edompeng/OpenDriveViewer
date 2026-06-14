#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>
#include <vector>

namespace geoviewer::core {

template <typename, typename = std::void_t<>>
struct has_clear : std::false_type {};

template <typename T>
struct has_clear<T, std::void_t<decltype(std::declval<T&>().clear())>> : std::true_type {};

/// @brief Thread-safe Object Pool implementation using custom unique_ptr deleters.
/// @tparam T Type of object managed by the pool.
template <typename T>
class ObjectPool {
 public:
  explicit ObjectPool(size_t initial_size = 0, size_t max_size = 0)
      : lifetime_token_(std::make_shared<int>(0)), max_size_(max_size) {
    for (size_t i = 0; i < initial_size; ++i) {
      pool_.push_back(std::make_unique<T>());
    }
  }

  ~ObjectPool() = default;

  // Disable copy/move
  ObjectPool(const ObjectPool&) = delete;
  ObjectPool& operator=(const ObjectPool&) = delete;
  ObjectPool(ObjectPool&&) = delete;
  ObjectPool& operator=(ObjectPool&&) = delete;

  struct Deleter {
    std::weak_ptr<void> lifetime_token;
    ObjectPool* pool = nullptr;

    void operator()(T* ptr) const {
      if (ptr) {
        if (auto token = lifetime_token.lock()) {
          pool->ReturnObject(std::unique_ptr<T>(ptr));
        } else {
          delete ptr;
        }
      }
    }
  };

  using PtrType = std::unique_ptr<T, Deleter>;

  /// @brief Acquire an object from the pool. Creates a new one if the pool is empty.
  PtrType Acquire() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (pool_.empty()) {
      return PtrType(new T(), Deleter{lifetime_token_, this});
    }
    std::unique_ptr<T> obj = std::move(pool_.back());
    pool_.pop_back();
    return PtrType(obj.release(), Deleter{lifetime_token_, this});
  }

  /// @brief Return an object back to the pool.
  void ReturnObject(std::unique_ptr<T> obj) {
    if (!obj) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (max_size_ > 0 && pool_.size() >= max_size_) {
      return;
    }
    if constexpr (has_clear<T>::value) {
      obj->clear();
    }
    pool_.push_back(std::move(obj));
  }

  /// @brief Query the number of idle objects in the pool.
  size_t Size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pool_.size();
  }

  /// @brief Clear all idle objects in the pool.
  void Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    pool_.clear();
  }

 private:
  std::shared_ptr<void> lifetime_token_;
  mutable std::mutex mutex_;
  std::vector<std::unique_ptr<T>> pool_;
  size_t max_size_ = 0;
};

inline ObjectPool<std::vector<float>>& FloatVectorPool() {
  static ObjectPool<std::vector<float>> pool(4);
  return pool;
}

inline ObjectPool<std::vector<uint32_t>>& UintVectorPool() {
  static ObjectPool<std::vector<uint32_t>> pool(4);
  return pool;
}

}  // namespace geoviewer::core
