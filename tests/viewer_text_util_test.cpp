#include "src/core/viewer_text_util.h"

#include "third_party/Catch2/src/catch2/catch_test_macros.hpp"

TEST_CASE("Viewer text utility formats section values and labels",
          "[viewer-text]") {
  CHECK(FormatSectionValue(12.3400) == "12.34");
  CHECK(FavoriteTypeLabel(TreeNodeType::kLane) == "kLane");
  CHECK(FavoriteTypeLabel(TreeNodeType::kLight) == "kLight");
}

TEST_CASE("Viewer text utility builds display strings", "[viewer-text]") {
  CHECK(BuildFavoriteDisplayName("42", TreeNodeType::kLane, "1:2") ==
        "kLane 1:2 (kRoad 42)");
  CHECK(BuildFavoriteDisplayName("42", TreeNodeType::kLane, "1:2", "Custom") ==
        "Custom");
  CHECK(BuildRouteDisplayName("a", "b") == "a -> b");
  CHECK(BuildLanePosition("r1", "10.5", "-1") == "r1/10.5/-1");
}
