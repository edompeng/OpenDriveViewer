#pragma once

#include <optional>
#include <string>
#include <vector>
#include "third_party/libOpenDRIVE/include/RoutingGraph.h"

struct ParsedUserPoint {
  double lon = 0.0;
  double lat = 0.0;
  std::optional<double> alt;
};

struct ParsedJumpLocation {
  double lon = 0.0;
  double lat = 0.0;
  double alt = 0.0;
};

class CoordinateInputParser {
 public:
  static std::vector<ParsedUserPoint> ParseUserPoints(const std::string& text);
  static std::optional<ParsedJumpLocation> ParseJumpLocation(
      const std::string& text);
  static std::optional<odr::LaneKey> ParseLaneKey(const std::string& text);
};
