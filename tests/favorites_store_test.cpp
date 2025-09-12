#include "src/utility/favorites_store.h"

#include "third_party/Catch2/src/catch2/catch_test_macros.hpp"

TEST_CASE("Favorites store prevents duplicates and removes items",
          "[favorites-store]") {
  FavoritesStore store;

  CHECK(store.Add("r1", TreeNodeType::kLane, "0:1", "Lane"));
  CHECK_FALSE(store.Add("r1", TreeNodeType::kLane, "0:1", "Lane"));
  REQUIRE(store.Size() == 1);
  REQUIRE(store.At(0) != nullptr);
  CHECK(store.At(0)->display_name == "Lane");
  CHECK(store.Find("r1", TreeNodeType::kLane, "0:1") == 0);
  CHECK(store.RemoveAt(0));
  CHECK(store.Size() == 0);
  CHECK_FALSE(store.RemoveAt(0));
}
