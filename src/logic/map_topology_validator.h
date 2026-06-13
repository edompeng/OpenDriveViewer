#pragma once

#include <memory>
#include <string>
#include <vector>
#include "src/core/scene_enums.h"
#include "src/geo_viewer_export.h"

namespace odr {
class OpenDriveMap;
class RoutingGraph;
}  // namespace odr

namespace geoviewer::logic {

enum class TopologySeverity { kInfo, kWarning, kError };

struct GEOVIEWER_EXPORT TopologyIssue {
  TopologySeverity severity = TopologySeverity::kWarning;
  std::string type;  // Short category, e.g., "Dangling Road"
  std::string road_id;
  std::string lane_id;  // optional, e.g. "s0:lane_id"
  std::string message;  // detailed description
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
  TreeNodeType node_type = TreeNodeType::kRoad;
};

class GEOVIEWER_EXPORT MapTopologyValidator {
 public:
  static std::vector<TopologyIssue> Validate(
      const std::shared_ptr<odr::OpenDriveMap>& map,
      const odr::RoutingGraph* graph);
};

}  // namespace geoviewer::logic
