#include "src/core/thread_pool.h"
#include <iostream>

namespace geoviewer::utility {

ThreadPool& ThreadPool::Instance() {
  static ThreadPool instance(std::thread::hardware_concurrency());
  return instance;
}

ThreadPool::ThreadPool(size_t threads) : stop_(false) {
  for (size_t i = 0; i < threads; ++i) {
    workers_.emplace_back([this] {
      for (;;) {
        std::function<void()> task;

        {
          std::unique_lock<std::mutex> lock(this->queue_mutex_);
          this->condition_.wait(
              lock, [this] { return this->stop_ || !this->tasks_.empty(); });
          if (this->stop_ && this->tasks_.empty()) return;
          task = std::move(this->tasks_.front());
          this->tasks_.pop();
        }

        try {
          task();
        } catch (const std::exception& e) {
          std::cerr << "ThreadPool task threw exception: " << e.what()
                    << std::endl;
        } catch (...) {
          std::cerr << "ThreadPool task threw unknown exception" << std::endl;
        }
      }
    });
  }
}

ThreadPool::~ThreadPool() {
  {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    stop_ = true;
  }
  condition_.notify_all();
  for (std::thread& worker : workers_) {
    if (worker.joinable()) {
      worker.join();
    }
  }
}

}  // namespace geoviewer::utility
