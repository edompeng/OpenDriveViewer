#include "src/logic/favorites_store.h"
#include <gtest/gtest.h>

TEST(FavoritesStoreTest, PreventsDuplicatesAndRemovesItems) {
  FavoritesStore store;

  EXPECT_TRUE(store.Add("r1", TreeNodeType::kLane, "0:1", "Lane"));
  EXPECT_FALSE(store.Add("r1", TreeNodeType::kLane, "0:1", "Lane"));
  ASSERT_EQ(store.Size(), std::size_t(1));
  ASSERT_NE(store.At(0), nullptr);
  EXPECT_EQ(store.At(0)->display_name, "Lane");
  EXPECT_EQ(store.Find("r1", TreeNodeType::kLane, "0:1"), std::size_t(0));
  EXPECT_TRUE(store.RemoveAt(0));
  EXPECT_EQ(store.Size(), std::size_t(0));
  EXPECT_FALSE(store.RemoveAt(0));
}
