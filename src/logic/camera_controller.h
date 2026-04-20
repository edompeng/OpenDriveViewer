#pragma once

#include <QMatrix4x4>
#include <QPoint>
#include <QSize>
#include <QVector3D>
#include "src/geo_viewer_export.h"

/// @brief 管理3D轨道相机的状态与变换（SRP - 单一职责：相机控制）
///
/// 设计模式: Value Object + 策略组合
/// 六大原则: SRP（相机逻辑与渲染解耦）、OCP（可通过子类扩展相机类型）
class GEOVIEWER_EXPORT CameraController {
 public:
  CameraController() = default;

  // --- 状态获取 ---
  QMatrix4x4 GetViewMatrix() const;
  QVector3D GetTarget() const { return target_; }
  float GetDistance() const { return distance_; }
  float GetYaw() const { return yaw_; }
  float GetPitch() const { return pitch_; }

  // --- 交互处理 ---

  /// 开始拖拽（保存起始点）
  void BeginDrag(const QPoint& pos, Qt::MouseButton button);
  void EndDrag();

  /// 处理左键平移
  void PanByDelta(const QPoint& delta);
  /// 处理右键旋转
  void OrbitByDelta(const QPoint& delta);
  /// 滚轮缩放，同时以 worldFocusPoint 为锚点
  void ZoomToward(float wheel_delta, float max_dist,
                  const QVector3D& world_focus_point, bool has_focus_point);

  // --- 定位 ---
  void SetTarget(const QVector3D& target) { target_ = target; }
  void SetDistance(float d) { distance_ = d; }
  void SetPitch(float p) { pitch_ = p; }
  void SetYaw(float y) { yaw_ = y; }

  /// 根据mesh包围盒计算初始相机位置
  void FitToScene(const QVector3D& scene_min, const QVector3D& scene_max);

  /// 从 min/max 中计算 mesh_radius（供 paintGL 近远裁剪面使用）
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
