#pragma once

#include <QString>
#include <QVector3D>
#include <map>
#include "src/core/scene_enums.h"

namespace geoviewer::core {

struct AppSettings {
  // Sub-widget visibility
  bool layer_manager_visible = true;
  bool routing_visible = false;
  bool favorites_visible = false;
  bool coordinate_points_visible = false;

  // clang-format off
  std::map<LayerType, bool> global_layer_visibility = {
      {LayerType::kLanes, true},
      {LayerType::kLaneLines, true},
      {LayerType::kRoadmarks, true},
      {LayerType::kObjects, true},
      {LayerType::kFacilities, true},
      {LayerType::kSignalLights, true},
      {LayerType::kSignalSigns, true},
      {LayerType::kReferenceLines, true},
      {LayerType::kJunctions, true},
    };
  // clang-format on

  // Coordinate points default color (R,G,B in 0..1)
  QVector3D default_point_color = QVector3D(1.0f, 0.3f, 0.3f);

  // Coordinate mode
  CoordinateMode coordinate_mode = CoordinateMode::kWGS84;

  // Language locale string
  QString language = "zh_CN";
};

}  // namespace geoviewer::core
