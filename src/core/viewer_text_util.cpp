#include "src/core/viewer_text_util.h"

#include <iomanip>
#include <sstream>

std::string FormatSectionValue(double value) {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(4) << value;
  std::string s = oss.str();
  while (s.empty() == false && s.back() == '0') {
    s.pop_back();
  }
  if (s.empty() == false && s.back() == '.') {
    s.pop_back();
  }
  if (s.empty()) {
    return "0";
  }
  return s;
}

std::string FavoriteTypeLabel(TreeNodeType type) {
  switch (type) {
    case TreeNodeType::kLane:
      return "kLane";
    case TreeNodeType::kObject:
      return "kObject";
    case TreeNodeType::kLight:
      return "kLight";
    case TreeNodeType::kSign:
      return "kSign";
    case TreeNodeType::kRoad:
      return "kRoad";
    case TreeNodeType::kSection:
      return "kSection";
    default:
      return "Element";
  }
}

std::string BuildFavoriteDisplayName(const std::string& road_id,
                                     TreeNodeType type,
                                     const std::string& element_id,
                                     const std::string& explicit_name) {
  if (explicit_name.empty() == false) {
    return explicit_name;
  }
  return FavoriteTypeLabel(type) + " " + element_id + " (kRoad " + road_id +
         ")";
}

std::string BuildRouteDisplayName(const std::string& start,
                                  const std::string& end) {
  return start + " -> " + end;
}

std::string BuildLanePosition(const std::string& road_id, const std::string& s0,
                              const std::string& lane_id) {
  return road_id + "/" + s0 + "/" + lane_id;
}

std::vector<std::string> UniqueRoadSequence(
    const std::vector<odr::LaneKey>& path) {
  std::vector<std::string> roads;
  for (const auto& key : path) {
    if (roads.empty() || roads.back() != key.road_id) {
      roads.push_back(key.road_id);
    }
  }
  return roads;
}
