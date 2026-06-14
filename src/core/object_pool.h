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
 private:
  struct PoolState {
    std::mutex mutex;
    std::vector<std::unique_ptr<T>> pool;
    size_t max_size = 0;
  };

 public:
  struct Deleter {
    std::weak_ptr<PoolState> state;

    void operator()(T* ptr) const {
      if (ptr) {
        if (auto locked_state = state.lock()) {
          std::lock_guard<std::mutex> lock(locked_state->mutex);
          if (locked_state->max_size > 0 && locked_state->pool.size() >= locked_state->max_size) {
            delete ptr;
            return;
          }
          if constexpr (has_clear<T>::value) {
            ptr->clear();
          }
          locked_state->pool.push_back(std::unique_ptr<T>(ptr));
        } else {
          delete ptr;
        }
      }
    }
  };

  using PtrType = std::unique_ptr<T, Deleter>;

  explicit ObjectPool(size_t initial_size = 0, size_t max_size = 0)
      : state_(std::make_shared<PoolState>()) {
    state_->max_size = max_size;
    for (size_t i = 0; i < initial_size; ++i) {
      state_->pool.push_back(std::make_unique<T>());
    }
  }

  ~ObjectPool() = default;

  // Disable copy/move
  ObjectPool(const ObjectPool&) = delete;
  ObjectPool& operator=(const ObjectPool&) = delete;
  ObjectPool(ObjectPool&&) = delete;
  ObjectPool& operator=(ObjectPool&&) = delete;

  /// @brief Acquire an object from the pool. Creates a new one if the pool is empty.
  PtrType Acquire() {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->pool.empty()) {
      return PtrType(new T(), Deleter{state_});
    }
    std::unique_ptr<T> obj = std::move(state_->pool.back());
    state_->pool.pop_back();
    return PtrType(obj.release(), Deleter{state_});
  }

  /// @brief Query the number of idle objects in the pool.
  size_t Size() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->pool.size();
  }

  /// @brief Clear all idle objects in the pool.
  void Clear() {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->pool.clear();
  }

 private:
  std::shared_ptr<PoolState> state_;
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
