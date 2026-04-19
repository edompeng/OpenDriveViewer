#include "src/logic/routing_logic.h"

#include "src/core/viewer_text_util.h"

RouteHistoryEntry BuildRouteHistoryEntry(
    const std::string& start, const std::string& end,
    const std::vector<odr::LaneKey>& path) {
  return RouteHistoryEntry{BuildRouteDisplayName(start, end),
                           UniqueRoadSequence(path)};
}
