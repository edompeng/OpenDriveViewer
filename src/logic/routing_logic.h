#pragma once

#include <string>
#include <vector>
#include "Lane.h"

struct RouteHistoryEntry {
  std::string display_name;
  std::vector<std::string> road_sequence;
};

RouteHistoryEntry BuildRouteHistoryEntry(const std::string& start,
                                         const std::string& end,
                                         const std::vector<odr::LaneKey>& path);
