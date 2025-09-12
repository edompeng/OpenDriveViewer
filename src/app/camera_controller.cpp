#include "src/app/camera_controller.h"

#include <QtMath>
#include <algorithm>

QMatrix4x4 CameraController::GetViewMatrix() const {
  const float yawRad = qDegreesToRadians(yaw_);
  const float pitchRad = qDegreesToRadians(pitch_);

  QVector3D dir;
  dir.setX(qCos(pitchRad) * qCos(yawRad));
  dir.setY(qSin(pitchRad));
  dir.setZ(qCos(pitchRad) * qSin(yawRad));

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

void CameraController::ZoomToward(float wheelDelta, float maxDist,
                                  const QVector3D& worldFocusPoint,
                                  bool hasFocusPoint) {
  const float zoomFactor = std::pow(0.9f, wheelDelta);
  const float oldDist = distance_;
  distance_ = qBound(1e-6f, distance_ * zoomFactor, maxDist);

  if (hasFocusPoint) {
    const float actualFactor = distance_ / oldDist;
    target_ = worldFocusPoint + (target_ - worldFocusPoint) * actualFactor;
  }
}

void CameraController::FitToScene(const QVector3D& sceneMin,
                                  const QVector3D& sceneMax) {
  target_ = (sceneMin + sceneMax) * 0.5f;
  const float maxDim =
      QVector3D(sceneMax.x() - sceneMin.x(), sceneMax.y() - sceneMin.y(),
                sceneMax.z() - sceneMin.z())
          .length();
  mesh_radius_ = maxDim * 0.5f;
  distance_ = maxDim * 1.5f;
}
