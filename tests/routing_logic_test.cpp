#include "src/utility/routing_logic.h"

#include "third_party/Catch2/src/catch2/catch_test_macros.hpp"

TEST_CASE("Routing logic builds route history without duplicate road runs",
          "[routing-logic]") {
  std::vector<odr::LaneKey> path = {
      odr::LaneKey("r1", 0.0, 1),
      odr::LaneKey("r1", 10.0, 1),
      odr::LaneKey("r2", 0.0, -1),
  };

  const RouteHistoryEntry entry = BuildRouteHistoryEntry("A", "B", path);
  CHECK(entry.display_name == "A -> B");
  REQUIRE(entry.road_sequence.size() == 2);
  CHECK(entry.road_sequence[0] == "r1");
  CHECK(entry.road_sequence[1] == "r2");
}
