#pragma once

#include <optional>
#include <string>
#include <vector>
#include "Lane.h"
#include "RoutingGraph.h"

struct ParsedUserPoint {
  double x = 0.0;
  double y = 0.0;
  std::optional<double> z;
};

struct ParsedJumpLocation {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};


class CoordinateInputParser {
 public:
  static std::vector<ParsedUserPoint> ParseUserPoints(const std::string& text);
  static std::optional<ParsedJumpLocation> ParseJumpLocation(
      const std::string& text);
  static std::optional<odr::LaneKey> ParseLaneKey(const std::string& text);
};
