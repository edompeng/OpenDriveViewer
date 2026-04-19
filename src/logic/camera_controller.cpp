#include "src/logic/camera_controller.h"

#include <QtMath>
#include <algorithm>

QMatrix4x4 CameraController::GetViewMatrix() const {
  const float yaw_rad = qDegreesToRadians(yaw_);
  const float pitch_rad = qDegreesToRadians(pitch_);

  QVector3D dir;
  dir.setX(qCos(pitch_rad) * qCos(yaw_rad));
  dir.setY(qSin(pitch_rad));
  dir.setZ(qCos(pitch_rad) * qSin(yaw_rad));

  QMatrix4x4 view;
  view.lookAt(target_ - dir * distance_, target_, QVector3D(0, 1, 0));
  return view;
}

void CameraController::BeginDrag(const QPoint& pos, Qt::MouseButton button) {
  last_pos_ = pos;
  pressed_button_ = button;
}

void CameraController::EndDrag() { pressed_button_ = Qt::NoButton; }

void CameraController::PanByDelta(const QPoint& delta) {
  const float factor = distance_ * 0.002f;
  const QMatrix4x4 view = GetViewMatrix();
  const QVector3D right(view(0, 0), view(0, 1), view(0, 2));
  const QVector3D up(view(1, 0), view(1, 1), view(1, 2));
  target_ -= right * static_cast<float>(delta.x()) * factor;
  target_ += up * static_cast<float>(delta.y()) * factor;
}

void CameraController::OrbitByDelta(const QPoint& delta) {
  constexpr float kSensitivity = 0.3f;
  yaw_ += static_cast<float>(delta.x()) * kSensitivity;
  pitch_ += static_cast<float>(delta.y()) * kSensitivity;
  pitch_ = qBound(-89.0f, pitch_, 89.0f);
}

void CameraController::ZoomToward(float wheel_delta, float max_dist,
                                  const QVector3D& world_focus_point,
                                  bool has_focus_point) {
  const float zoom_factor = std::pow(0.9f, wheel_delta);
  const float old_dist = distance_;
  distance_ = qBound(1e-6f, distance_ * zoom_factor, max_dist);

  if (has_focus_point) {
    const float actual_factor = distance_ / old_dist;
    target_ = world_focus_point + (target_ - world_focus_point) * actual_factor;
  }
}

void CameraController::FitToScene(const QVector3D& scene_min,
                                  const QVector3D& scene_max) {
  target_ = (scene_min + scene_max) * 0.5f;
  const float max_dim =
      QVector3D(scene_max.x() - scene_min.x(), scene_max.y() - scene_min.y(),
                scene_max.z() - scene_min.z())
          .length();
  mesh_radius_ = max_dim * 0.5f;
  distance_ = max_dim * 1.5f;
}
