#include "src/core/settings_persistence.h"
#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include "src/core/scene_enums.h"
#include "src/core/viewer_text_util.h"

namespace geoviewer::core {

namespace {
const char* kConfigFileName = "geoviewer_settings.ini";
const char* kSectionWindows = "Windows";
const char* kSectionLayers = "Layers";
const char* kSectionGeneral = "General";
}  // namespace

QString SettingsPersistence::GetConfigPath(const QString& app_dir_path) {
  return QDir(app_dir_path).filePath(kConfigFileName);
}

AppSettings SettingsPersistence::Load(const QString& app_dir_path) {
  AppSettings settings;
  QString path = GetConfigPath(app_dir_path);
  if (!QFileInfo::exists(path)) {
    return settings;
  }

  QSettings s(path, QSettings::IniFormat);

  // Load Window Visibility
  s.beginGroup(kSectionWindows);
  settings.layer_manager_visible =
      s.value("LayerManager", settings.layer_manager_visible).toBool();
  settings.routing_visible =
      s.value("Routing", settings.routing_visible).toBool();
  settings.favorites_visible =
      s.value("Favorites", settings.favorites_visible).toBool();
  settings.coordinate_points_visible =
      s.value("CoordinatePoints", settings.coordinate_points_visible).toBool();
  s.endGroup();

  // Load Global Layer Visibility
  s.beginGroup(kSectionLayers);
  for (auto& [type, visibility] : settings.global_layer_visibility) {
    auto str = LayerTypeToString(type);
    if (str.empty()) {
      continue;
    }
    visibility = s.value(str, visibility).toBool();
  }
  s.endGroup();

  // Load General Settings
  s.beginGroup(kSectionGeneral);

  // Coordinate Mode
  QString coord_mode_str = s.value("CoordinateMode", "").toString();
  if (coord_mode_str == "WGS84") {
    settings.coordinate_mode = CoordinateMode::kWGS84;
  } else if (coord_mode_str == "Local") {
    settings.coordinate_mode = CoordinateMode::kLocal;
  }

  // Language
  QString lang = s.value("Language", "").toString();
  if (lang == "zh_CN" || lang == "en_US") {
    settings.language = lang;
  }

  // Default Point Color
  if (s.contains("PointColor")) {
    QStringList parts = s.value("PointColor").toString().split(',');
    if (parts.size() == 3) {
      bool ok_r, ok_g, ok_b;
      float r = parts[0].toFloat(&ok_r);
      float g = parts[1].toFloat(&ok_g);
      float b = parts[2].toFloat(&ok_b);
      if (ok_r && ok_g && ok_b) {
        settings.default_point_color =
            QVector3D(std::clamp(r, 0.0f, 1.0f), std::clamp(g, 0.0f, 1.0f),
                      std::clamp(b, 0.0f, 1.0f));
      }
    }
  }
  s.endGroup();

  return settings;
}

void SettingsPersistence::Save(const AppSettings& settings,
                               const QString& app_dir_path) {
  QSettings s(GetConfigPath(app_dir_path), QSettings::IniFormat);

  s.beginGroup(kSectionWindows);
  s.setValue("LayerManager", settings.layer_manager_visible);
  s.setValue("Routing", settings.routing_visible);
  s.setValue("Favorites", settings.favorites_visible);
  s.setValue("CoordinatePoints", settings.coordinate_points_visible);
  s.endGroup();

  s.beginGroup(kSectionLayers);
  for (const auto& [type, visibility] : settings.global_layer_visibility) {
    auto str = LayerTypeToString(type);
    if (str.empty()) {
      continue;
    }
    s.setValue(str, visibility);
  }
  s.endGroup();

  s.beginGroup(kSectionGeneral);
  s.setValue(
      "CoordinateMode",
      settings.coordinate_mode == CoordinateMode::kWGS84 ? "WGS84" : "Local");
  s.setValue("Language", settings.language);
  s.setValue("PointColor", QString("%1,%2,%3")
                               .arg(settings.default_point_color.x())
                               .arg(settings.default_point_color.y())
                               .arg(settings.default_point_color.z()));
  s.endGroup();

  s.sync();
}

}  // namespace geoviewer::core
