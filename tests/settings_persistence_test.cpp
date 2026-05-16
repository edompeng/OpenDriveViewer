#include "src/core/settings_persistence.h"
#include <gtest/gtest.h>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>
#include "src/core/app_settings.h"
#include "src/core/scene_enums.h"

namespace geoviewer::core {

class SettingsPersistenceTest : public ::testing::Test {
 protected:
  QTemporaryDir temp_dir_;
};

TEST_F(SettingsPersistenceTest, RoundTripDefaults) {
  AppSettings original;
  QString path = temp_dir_.path();

  SettingsPersistence::Save(original, path);
  AppSettings loaded = SettingsPersistence::Load(path);

  EXPECT_EQ(loaded.layer_manager_visible, original.layer_manager_visible);
  EXPECT_EQ(loaded.routing_visible, original.routing_visible);
  EXPECT_EQ(loaded.favorites_visible, original.favorites_visible);
  EXPECT_EQ(loaded.coordinate_points_visible,
            original.coordinate_points_visible);
  EXPECT_EQ(loaded.coordinate_mode, original.coordinate_mode);
  EXPECT_EQ(loaded.language, original.language);
  EXPECT_EQ(loaded.default_point_color, original.default_point_color);

  EXPECT_EQ(loaded.global_layer_visibility.size(),
            original.global_layer_visibility.size());
  for (const auto& [layer, visibility] : loaded.global_layer_visibility) {
    EXPECT_EQ(visibility, original.global_layer_visibility[layer]);
  }
}

TEST_F(SettingsPersistenceTest, ModifyAndSave) {
  AppSettings original;
  original.layer_manager_visible = false;
  original.routing_visible = true;
  original.coordinate_mode = CoordinateMode::kLocal;
  original.language = "en_US";
  original.default_point_color = QVector3D(0.1f, 0.2f, 0.3f);
  original.global_layer_visibility[LayerType::kLanes] =
      !original.global_layer_visibility[LayerType::kLanes];

  QString path = temp_dir_.path();
  SettingsPersistence::Save(original, path);
  AppSettings loaded = SettingsPersistence::Load(path);

  EXPECT_EQ(loaded.layer_manager_visible, false);
  EXPECT_EQ(loaded.routing_visible, true);
  EXPECT_EQ(loaded.coordinate_mode, CoordinateMode::kLocal);
  EXPECT_EQ(loaded.language, "en_US");
  EXPECT_NEAR(loaded.default_point_color.x(), 0.1f, 1e-5);
  EXPECT_NEAR(loaded.default_point_color.y(), 0.2f, 1e-5);
  EXPECT_NEAR(loaded.default_point_color.z(), 0.3f, 1e-5);
  EXPECT_EQ(loaded.global_layer_visibility[LayerType::kLanes],
            original.global_layer_visibility[LayerType::kLanes]);
}

TEST_F(SettingsPersistenceTest, HandleInvalidValues) {
  QString path = temp_dir_.path();
  QString config_path = QDir(path).filePath("geoviewer_settings.ini");

  {
    QSettings s(config_path, QSettings::IniFormat);
    s.beginGroup("General");
    s.setValue("CoordinateMode", "InvalidMode");
    s.setValue("Language", "fr_FR");
    s.setValue("PointColor", "2.0,-1.0,0.5");
    s.endGroup();
    s.sync();
  }

  AppSettings loaded = SettingsPersistence::Load(path);
  AppSettings defaults;

  // Invalid mode should fallback to default (WGS84)
  EXPECT_EQ(loaded.coordinate_mode, defaults.coordinate_mode);
  // Invalid language should fallback to default (zh_CN)
  EXPECT_EQ(loaded.language, defaults.language);
  // Color components should be clamped
  EXPECT_NEAR(loaded.default_point_color.x(), 1.0f, 1e-5);  // Clamped from 2.0
  EXPECT_NEAR(loaded.default_point_color.y(), 0.0f, 1e-5);  // Clamped from -1.0
  EXPECT_NEAR(loaded.default_point_color.z(), 0.5f, 1e-5);
}

TEST_F(SettingsPersistenceTest, HandleMissingFile) {
  AppSettings loaded = SettingsPersistence::Load("/non/existent/path");
  AppSettings defaults;
  EXPECT_EQ(loaded.language, defaults.language);
  EXPECT_EQ(loaded.coordinate_mode, defaults.coordinate_mode);
}

}  // namespace geoviewer::core
