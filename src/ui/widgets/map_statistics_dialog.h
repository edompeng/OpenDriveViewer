#pragma once

#include <QDialog>
#include <map>
#include <memory>
#include <string>

namespace odr {
class OpenDriveMap;
}

namespace geoviewer::ui {

class MapStatisticsDialog : public QDialog {
  Q_OBJECT
 public:
  MapStatisticsDialog(std::shared_ptr<odr::OpenDriveMap> map,
                      const QString& map_path, QWidget* parent = nullptr);
  ~MapStatisticsDialog() override = default;

 private slots:
  void HandleCopyAll();
  void HandleExportJson();

 private:
  void SetupUi();
  void CalculateStats();
  QString GetFormattedStatsText() const;

  std::shared_ptr<odr::OpenDriveMap> map_;
  QString map_path_;

  // Calculated Stats
  size_t road_count_ = 0;
  size_t junction_count_ = 0;
  double total_road_length_ = 0.0;
  size_t total_lanes_count_ = 0;
  std::map<std::string, size_t> lane_type_distribution_;
  double min_x_ = 1e9;
  double max_x_ = -1e9;
  double min_y_ = 1e9;
  double max_y_ = -1e9;
};

}  // namespace geoviewer::ui
