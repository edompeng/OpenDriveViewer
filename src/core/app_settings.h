#pragma once

#include <QByteArray>
#include <QString>
#include <QVector3D>
#include <map>
#include <string>
#include "src/core/scene_enums.h"

namespace geoviewer::core {

struct AppSettings {
  // Sub-widget visibility
  bool layer_manager_visible = true;
  bool routing_visible = false;
  bool favorites_visible = false;
  bool coordinate_points_visible = false;
  bool topology_validator_visible = false;

  // Main window geometry and QDockWidget layout.
  QByteArray main_window_geometry;
  QByteArray main_window_state;

  // Stable command id -> portable QKeySequence string. Empty disables it.
  std::map<std::string, std::string> shortcuts;

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
