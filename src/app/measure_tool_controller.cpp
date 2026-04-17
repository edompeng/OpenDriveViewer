#include "src/app/measure_tool_controller.h"

MeasureToolController::MeasureToolController(QObject* parent)
    : QObject(parent) {}

MeasureToolController::~MeasureToolController() = default;

void MeasureToolController::SetActive(bool active) {
  if (active_ == active) return;
  active_ = active;
  emit activeChanged(active_);
}

void MeasureToolController::AddPoint(const QVector3D& world_pos) {
  points_.push_back(world_pos);
  emit TotalDistanceChanged(TotalDistance());
  emit pointsChanged();
}

void MeasureToolController::ClearPoints() {
  points_.clear();
  emit TotalDistanceChanged(0.0);
  emit pointsChanged();
}

double MeasureToolController::TotalDistance() const {
  double total = 0.0;
  for (size_t i = 1; i < points_.size(); ++i) {
    total += static_cast<double>(points_[i].distanceToPoint(points_[i - 1]));
  }
  return total;
}
