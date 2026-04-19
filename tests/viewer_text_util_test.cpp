#include "src/core/viewer_text_util.h"
#include <gtest/gtest.h>

TEST(ViewerTextUtilTest, FormatsSectionValuesAndLabels) {
  EXPECT_EQ(FormatSectionValue(12.3400), "12.34");
  EXPECT_EQ(FavoriteTypeLabel(TreeNodeType::kLane), "kLane");
  EXPECT_EQ(FavoriteTypeLabel(TreeNodeType::kLight), "kLight");
}

TEST(ViewerTextUtilTest, BuildsDisplayStrings) {
  EXPECT_EQ(BuildFavoriteDisplayName("42", TreeNodeType::kLane, "1:2"), "kLane 1:2 (kRoad 42)");
  EXPECT_EQ(BuildFavoriteDisplayName("42", TreeNodeType::kLane, "1:2", "Custom"), "Custom");
  EXPECT_EQ(BuildRouteDisplayName("a", "b"), "a -> b");
  EXPECT_EQ(BuildLanePosition("r1", "10.5", "-1"), "r1/10.5/-1");
}
