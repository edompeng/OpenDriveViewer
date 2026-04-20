#include "src/logic/routing_logic.h"
#include <gtest/gtest.h>

TEST(RoutingLogicTest, BuildsRouteHistoryWithoutDuplicateRoadRuns) {
  std::vector<odr::LaneKey> path = {
      odr::LaneKey("r1", 0.0, 1),
      odr::LaneKey("r1", 10.0, 1),
      odr::LaneKey("r2", 0.0, -1),
  };

  const RouteHistoryEntry entry = BuildRouteHistoryEntry("A", "B", path);
  EXPECT_EQ(entry.display_name, "A -> B");
  ASSERT_EQ(entry.road_sequence.size(), std::size_t(2));
  EXPECT_EQ(entry.road_sequence[0], "r1");
  EXPECT_EQ(entry.road_sequence[1], "r2");
}
