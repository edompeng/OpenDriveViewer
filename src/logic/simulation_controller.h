#pragma once

#include <QObject>
#include <QTimer>
#include <QVector3D>
#include <memory>
#include <vector>

#include "Lane.h"
#include "OpenDriveMap.h"
#include "src/geo_viewer_export.h"

namespace geoviewer::logic {

/// @brief Controls vehicle cruise simulation along a route.
class GEOVIEWER_EXPORT SimulationController : public QObject {
  Q_OBJECT
 public:
  explicit SimulationController(QObject* parent = nullptr);
  ~SimulationController() override;

  /// @brief Starts vehicle simulation along the given lane route path.
  void Start(const std::vector<odr::LaneKey>& path,
             const std::shared_ptr<odr::OpenDriveMap>& map,
             bool right_hand_traffic, float speed_mps = 15.0f);

  /// @brief Stops the simulation.
  void Stop();

  /// @brief Set playback speed.
  void SetSpeed(float speed_mps);

  bool IsActive() const { return is_active_; }
  QVector3D CurrentPosition() const { return current_pos_; }
  float CurrentHeading() const { return current_heading_; }

 signals:
  void PoseUpdated(const QVector3D& position, float heading);
  void StateChanged(bool active);

 private slots:
  void OnTick();

 private:
  bool is_active_ = false;
  QTimer timer_;
  std::vector<std::pair<QVector3D, float>> trajectory_;
  size_t current_index_ = 0;
  float speed_mps_ = 15.0f;
  QVector3D current_pos_{0, 0, 0};
  float current_heading_ = 0.0f;
  qint64 last_tick_time_ = 0;
  double distance_travelled_ = 0.0;
  std::vector<double> traj_distances_;
};

}  // namespace geoviewer::logic
