#include "src/core/object_pool.h"
#include <gtest/gtest.h>
#include <cstddef>
#include <thread>
#include <vector>

TEST(ObjectPoolTest, AcquireAndReturn) {
  using namespace geoviewer::core;
  ObjectPool<std::vector<float>> pool(static_cast<size_t>(2));
  EXPECT_EQ(pool.Size(), static_cast<size_t>(2));

  {
    auto obj1 = pool.Acquire();
    EXPECT_EQ(pool.Size(), static_cast<size_t>(1));
    obj1->push_back(1.0f);
  }  // obj1 goes out of scope and is returned to the pool

  EXPECT_EQ(pool.Size(), static_cast<size_t>(2));
  auto obj2 = pool.Acquire();
  EXPECT_GE(obj2->capacity(), static_cast<size_t>(1));
}

TEST(ObjectPoolTest, ConcurrentAcquire) {
  using namespace geoviewer::core;
  ObjectPool<std::vector<float>> pool(static_cast<size_t>(0));

  std::vector<std::thread> threads;
  for (int i = 0; i < 10; ++i) {
    threads.emplace_back([&pool]() {
      for (int k = 0; k < 100; ++k) {
        auto obj = pool.Acquire();
        obj->push_back(static_cast<float>(k));
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  EXPECT_GT(pool.Size(), static_cast<size_t>(0));
}

TEST(ObjectPoolTest, PoolDestructionBeforeObjects) {
  using namespace geoviewer::core;
  {
    auto pool = std::make_shared<ObjectPool<std::vector<float>>>(
        static_cast<size_t>(1));
    auto obj = pool->Acquire();
    obj->push_back(42.0f);
    pool.reset();  // Destroy pool while obj is outstanding
    EXPECT_EQ(obj->size(), static_cast<size_t>(1));
    EXPECT_NEAR((*obj)[0], 42.0f, 1e-6);
  }  // obj goes out of scope and is safely deleted via fallback
}

TEST(ObjectPoolTest, AutoClearOnReturn) {
  using namespace geoviewer::core;
  ObjectPool<std::vector<float>> pool(static_cast<size_t>(1));
  {
    auto obj = pool.Acquire();
    obj->push_back(100.0f);
  }  // Returned to pool and cleared
  auto obj2 = pool.Acquire();
  EXPECT_TRUE(obj2->empty());
}
