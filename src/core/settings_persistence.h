#pragma once

#include <QString>
#include <QSettings>
#include "src/core/app_settings.h"

namespace geoviewer::core {

/// @brief Utility class to handle loading and saving of AppSettings to an INI file.
class SettingsPersistence {
 public:
  /// @brief Load settings from geoviewer_settings.ini in the application directory.
  /// @param app_dir_path Path to the directory where the binary is located.
  /// @return Loaded AppSettings, or default values if the file is missing or invalid.
  static AppSettings Load(const QString& app_dir_path);

  /// @brief Save settings to geoviewer_settings.ini in the application directory.
  /// @param settings The settings to save.
  /// @param app_dir_path Path to the directory where the binary is located.
  static void Save(const AppSettings& settings, const QString& app_dir_path);

 private:
  static QString GetConfigPath(const QString& app_dir_path);
};

}  // namespace geoviewer::core
