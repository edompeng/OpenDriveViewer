#pragma once

#include <QMatrix4x4>
#include <QPoint>
#include <QSize>
#include <QVector3D>
#include "src/geo_viewer_export.h"

class GEOVIEWER_EXPORT CameraController {
 public:
  CameraController() = default;

  // --- State Getters ---
  QMatrix4x4 GetViewMatrix() const;
  QVector3D GetTarget() const { return target_; }
  float GetDistance() const { return distance_; }
  float GetYaw() const { return yaw_; }
  float GetPitch() const { return pitch_; }

  // --- Interaction Handling ---

  /// Begin dragging (store the starting point)
  void BeginDrag(const QPoint& pos, Qt::MouseButton button);
  void EndDrag();

  /// Handle left-click panning
  void PanByDelta(const QPoint& delta, const QSize& viewport_size);
  /// Handle right-click orbiting
  void OrbitByDelta(const QPoint& delta);
  /// Wheel zoom using world_focus_point as the anchor
  void ZoomToward(float wheel_delta, float max_dist,
                  const QVector3D& world_focus_point, bool has_focus_point);

  // --- Positioning ---
  void SetTarget(const QVector3D& target) { target_ = target; }
  void SetDistance(float d) { distance_ = d; }
  void SetPitch(float p) { pitch_ = p; }
  void SetYaw(float y) { yaw_ = y; }

  /// Calculate initial camera position based on the scene's bounding box
  void FitToScene(const QVector3D& scene_min, const QVector3D& scene_max);

  /// Calculate mesh_radius from min/max (used for near/far clipping planes in paintGL)
  float MeshRadius() const { return mesh_radius_; }

  Qt::MouseButton PressedButton() const { return pressed_button_; }
  QPoint LastPos() const { return last_pos_; }

 private:
  float yaw_ = 45.0f;
  float pitch_ = -30.0f;
  float distance_ = 100.0f;
  float mesh_radius_ = 0.0f;
  QVector3D target_{0.0f, 0.0f, 0.0f};

  QPoint last_pos_;
  Qt::MouseButton pressed_button_ = Qt::NoButton;
};
