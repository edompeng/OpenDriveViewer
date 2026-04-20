#pragma once

#include <string>
#include <vector>
#include "Lane.h"
#include "src/geo_viewer_export.h"

struct GEOVIEWER_EXPORT RouteHistoryEntry {
  std::string display_name;
  std::vector<std::string> road_sequence;
};

GEOVIEWER_EXPORT RouteHistoryEntry
BuildRouteHistoryEntry(const std::string& start, const std::string& end,
                       const std::vector<odr::LaneKey>& path);
