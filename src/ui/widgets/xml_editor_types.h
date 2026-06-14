#pragma once

#include <string>

namespace geoviewer::ui {

enum class XmlTargetType {
  kRoad,
  kLane,
  kJunction,
  kObject,
  kSignal
};

struct XmlTarget {
  XmlTargetType type;
  std::string road_id;      // for road, lane, object, signal
  std::string element_id;   // for junction, object, signal
  double lane_s0 = 0.0;     // for lane
  int lane_id = 0;          // for lane
};

}  // namespace geoviewer::ui
