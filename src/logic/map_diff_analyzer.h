#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Lane.h"
#include "OpenDriveMap.h"
#include "src/geo_viewer_export.h"

namespace geoviewer::logic {

/// @brief Holds the comparison result between two map versions.
struct GEOVIEWER_EXPORT DiffResult {
  std::vector<odr::LaneKey> added_lanes;
  std::vector<odr::LaneKey> removed_lanes;
  std::vector<odr::LaneKey> modified_lanes;
};

/// @brief Computes structural and geometric diffs between two OpenDRIVE maps.
class GEOVIEWER_EXPORT MapDiffAnalyzer {
 public:
  static DiffResult Analyze(
      const std::shared_ptr<odr::OpenDriveMap>& base_map,
      const std::shared_ptr<odr::OpenDriveMap>& target_map);
};

}  // namespace geoviewer::logic
