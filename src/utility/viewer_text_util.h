#pragma once

#include <string>
#include <vector>
#include "src/utility/scene_enums.h"
#include "third_party/libOpenDRIVE/include/RoutingGraph.h"

std::string FormatSectionValue(double value);
std::string FavoriteTypeLabel(TreeNodeType type);
std::string BuildFavoriteDisplayName(const std::string& road_id,
                                     TreeNodeType type,
                                     const std::string& element_id,
                                     const std::string& explicit_name = "");
std::string BuildRouteDisplayName(const std::string& start,
                                  const std::string& end);
std::string BuildLanePosition(const std::string& road_id, const std::string& s0,
                              const std::string& lane_id);
std::vector<std::string> UniqueRoadSequence(
    const std::vector<odr::LaneKey>& path);
