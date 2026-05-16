#pragma once

#include <QObject>
#include <QVector3D>
#include <vector>

#include "src/geo_viewer_export.h"

/// @brief Logic and core state management for the measurement tool (SRP)
///
/// Following the SRP principle, this class is only responsible for point data
/// management, distance measurement state control, and distance calculation,
/// without involving any hardware or graphics APIs, ensuring a clean business
/// model and strong testability.
class GEOVIEWER_EXPORT MeasureToolController : public QObject {
  Q_OBJECT
 public:
  explicit MeasureToolController(QObject* parent = nullptr);
  ~MeasureToolController() override;

  /// @brief Get the current activation state of the tool
  bool IsActive() const { return active_; }

  /// @brief Activate or deactivate measurement mode
  void SetActive(bool active);

  /// @brief Add a new measurement point and emit a signal if it changes
  void AddPoint(const QVector3D& world_pos);

  /// @brief Clear all selected measurement points
  void ClearPoints();

  /// @brief Return all selected measurement points
  const std::vector<QVector3D>& Points() const { return points_; }

  /// @brief Calculate the total length of all segments formed by the
  /// measurement points
  double TotalDistance() const;

 signals:
  void activeChanged(bool active);
  void TotalDistanceChanged(double distance);
  void pointsChanged();

 private:
  bool active_ = false;
  std::vector<QVector3D> points_;
};
