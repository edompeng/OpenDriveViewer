#include "src/logic/simulation_controller.h"

#include <QDateTime>
#include <QDebug>
#include <algorithm>
#include <cmath>

namespace geoviewer::logic {

SimulationController::SimulationController(QObject* parent) : QObject(parent) {
  connect(&timer_, &QTimer::timeout, this, &SimulationController::OnTick);
}

SimulationController::~SimulationController() { Stop(); }

void SimulationController::Start(const std::vector<odr::LaneKey>& path,
                                 const std::shared_ptr<odr::OpenDriveMap>& map,
                                 bool right_hand_traffic, float speed_mps) {
  if (!map || path.empty()) return;

  Stop();

  speed_mps_ = speed_mps;
  trajectory_.clear();
  traj_distances_.clear();

  // Generate trajectory points from routing path
  for (const auto& key : path) {
    const auto road_it = map->id_to_road.find(key.road_id);
    if (road_it == map->id_to_road.end()) continue;
    const auto& road = road_it->second;
    const auto& section = road.get_lanesection(key.lanesection_s0);
    if (!section.id_to_lane.count(key.lane_id)) continue;

    const double s_start = section.s0;
    const double s_end = road.get_lanesection_end(key.lanesection_s0);

    // Sample points along lane centerline every 0.5 meters
    const double step = 0.5;
    const int num_samples =
        std::max(2, static_cast<int>((s_end - s_start) / step));

    for (int i = 0; i <= num_samples; ++i) {
      const double s = s_start + (s_end - s_start) * i / num_samples;
      const auto& lane = section.id_to_lane.at(key.lane_id);
      const double lane_w = lane.lane_width.get(s);
      const double t_outer = lane.outer_border.get(s);
      const double t_center =
          t_outer - (key.lane_id > 0 ? 0.5 : -0.5) * lane_w;

      const odr::Vec3D pt = road.get_xyz(s, t_center, 0.0);
      double gl_x = pt[0];
      double gl_y = pt[2];  // OpenGL Y is up
      double gl_z = right_hand_traffic ? -pt[1] : pt[1];

      trajectory_.push_back({QVector3D(gl_x, gl_y, gl_z), 0.0f});
    }
  }

  if (trajectory_.size() < 2) return;

  // Calculate headings and cumulative distances
  double accum_dist = 0.0;
  traj_distances_.push_back(accum_dist);

  for (size_t i = 0; i < trajectory_.size(); ++i) {
    QVector3D current = trajectory_[i].first;
    QVector3D next;
    if (i < trajectory_.size() - 1) {
      next = trajectory_[i + 1].first;
    } else {
      // For the last point, reuse the heading of the previous segment
      next = current + (current - trajectory_[i - 1].first);
    }

    float dx = next.x() - current.x();
    float dz = next.z() - current.z();
    float heading = std::atan2(dz, dx);  // Radians
    trajectory_[i].second = heading;

    if (i > 0) {
      accum_dist +=
          (current - trajectory_[i - 1].first).length();
      traj_distances_.push_back(accum_dist);
    }
  }

  is_active_ = true;
  current_index_ = 0;
  distance_travelled_ = 0.0;
  current_pos_ = trajectory_[0].first;
  current_heading_ = trajectory_[0].second;
  last_tick_time_ = QDateTime::currentMSecsSinceEpoch();

  emit StateChanged(is_active_);
  emit PoseUpdated(current_pos_, current_heading_);

  timer_.start(33);  // ~30 FPS
}

void SimulationController::Stop() {
  if (!is_active_) return;
  timer_.stop();
  is_active_ = false;
  emit StateChanged(is_active_);
}

void SimulationController::SetSpeed(float speed_mps) {
  speed_mps_ = speed_mps;
}

void SimulationController::OnTick() {
  if (!is_active_ || trajectory_.size() < 2) return;

  qint64 now = QDateTime::currentMSecsSinceEpoch();
  double dt = (now - last_tick_time_) / 1000.0;
  last_tick_time_ = now;

  distance_travelled_ += speed_mps_ * dt;
  double total_dist = traj_distances_.back();

  if (distance_travelled_ >= total_dist) {
    // End of route
    current_pos_ = trajectory_.back().first;
    current_heading_ = trajectory_.back().second;
    emit PoseUpdated(current_pos_, current_heading_);
    Stop();
    return;
  }

  // Find the segment we are in
  auto it = std::upper_bound(traj_distances_.begin(), traj_distances_.end(),
                             distance_travelled_);
  size_t idx = std::distance(traj_distances_.begin(), it);
  if (idx == 0) idx = 1;
  if (idx >= trajectory_.size()) idx = trajectory_.size() - 1;

  size_t p0 = idx - 1;
  size_t p1 = idx;

  double d0 = traj_distances_[p0];
  double d1 = traj_distances_[p1];
  double factor = 0.0;
  if (std::abs(d1 - d0) > 1e-6) {
    factor = (distance_travelled_ - d0) / (d1 - d0);
  }

  QVector3D pos0 = trajectory_[p0].first;
  QVector3D pos1 = trajectory_[p1].first;
  current_pos_ = pos0 + factor * (pos1 - pos0);

  // Smooth heading interpolation (handling angle wrap-around)
  float h0 = trajectory_[p0].second;
  float h1 = trajectory_[p1].second;
  float dh = h1 - h0;
  constexpr double kPi = 3.14159265358979323846;
  if (std::isfinite(dh)) {
    while (dh < -kPi) dh += 2.0 * kPi;
    while (dh > kPi) dh -= 2.0 * kPi;
  } else {
    dh = 0.0f;
  }
  current_heading_ = h0 + factor * dh;

  emit PoseUpdated(current_pos_, current_heading_);
}

}  // namespace geoviewer::logic
