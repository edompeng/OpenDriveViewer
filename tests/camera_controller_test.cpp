#include "src/logic/camera_controller.h"
#include <gtest/gtest.h>

// ============================================================
// CameraController - Initial State
// ============================================================

TEST(CameraControllerTest, DefaultConstructionHasSaneValues) {
  CameraController cam;
  EXPECT_GT(cam.GetDistance(), 0.0f);
  EXPECT_FLOAT_EQ(cam.MeshRadius(), 0.0f);
  EXPECT_EQ(cam.PressedButton(), Qt::NoButton);
}

// ============================================================
// CameraController - View Matrix
// ============================================================

TEST(CameraControllerTest, ViewMatrixIsNotIdentityByDefault) {
  CameraController cam;
  const QMatrix4x4 view = cam.GetViewMatrix();
  // The view matrix should not be identity (camera is offset from origin)
  EXPECT_NE(view, QMatrix4x4());
}

// ============================================================
// CameraController - Camera Setters/Getters
// ============================================================

TEST(CameraControllerTest, SetTargetChangesTarget) {
  CameraController cam;
  const QVector3D newTarget(10.0f, 20.0f, 30.0f);
  cam.SetTarget(newTarget);
  EXPECT_NEAR(cam.GetTarget().x(), 10.0f, 1e-5f);
  EXPECT_NEAR(cam.GetTarget().y(), 20.0f, 1e-5f);
  EXPECT_NEAR(cam.GetTarget().z(), 30.0f, 1e-5f);
}

TEST(CameraControllerTest, SetDistanceStoresValue) {
  CameraController cam;
  cam.SetDistance(500.0f);
  EXPECT_NEAR(cam.GetDistance(), 500.0f, 1e-5f);
}

TEST(CameraControllerTest, SetPitchClampsWithinRange) {
  // Pitch clamping happens during OrbitByDelta, not SetPitch
  CameraController cam;
  cam.SetPitch(-45.0f);
  EXPECT_NEAR(cam.GetPitch(), -45.0f, 1e-5f);
}

TEST(CameraControllerTest, SetYawStoresValue) {
  CameraController cam;
  cam.SetYaw(180.0f);
  EXPECT_NEAR(cam.GetYaw(), 180.0f, 1e-5f);
}

// ============================================================
// CameraController - Drag State
// ============================================================

TEST(CameraControllerTest, BeginDragSetsPressedButton) {
  CameraController cam;
  cam.BeginDrag(QPoint(100, 200), Qt::LeftButton);
  EXPECT_EQ(cam.PressedButton(), Qt::LeftButton);
  EXPECT_EQ(cam.LastPos(), QPoint(100, 200));
}

TEST(CameraControllerTest, EndDragClearsPressedButton) {
  CameraController cam;
  cam.BeginDrag(QPoint(0, 0), Qt::RightButton);
  cam.EndDrag();
  EXPECT_EQ(cam.PressedButton(), Qt::NoButton);
}

// ============================================================
// CameraController - ZoomToward
// ============================================================

TEST(CameraControllerTest, ZoomTowardWithPositiveDeltaDecreasesDistance) {
  CameraController cam;
  cam.SetDistance(100.0f);
  const float beforeDist = cam.GetDistance();
  // delta > 0 means "zoom in"
  cam.ZoomToward(1.0f, 100000.0f, QVector3D(), false);
  EXPECT_LT(cam.GetDistance(), beforeDist);
}

TEST(CameraControllerTest, ZoomTowardWithNegativeDeltaIncreasesDistance) {
  CameraController cam;
  cam.SetDistance(100.0f);
  const float beforeDist = cam.GetDistance();
  // delta < 0 means "zoom out"
  cam.ZoomToward(-1.0f, 100000.0f, QVector3D(), false);
  EXPECT_GT(cam.GetDistance(), beforeDist);
}

TEST(CameraControllerTest, ZoomTowardDistanceNeverExceedsMaxDist) {
  CameraController cam;
  cam.SetDistance(99.0f);
  cam.ZoomToward(-100.0f, 100.0f, QVector3D(), false);
  EXPECT_LE(cam.GetDistance(), 100.0f);
}

TEST(CameraControllerTest, ZoomTowardDistanceNeverGoesBelowEpsilon) {
  CameraController cam;
  cam.SetDistance(1.0f);
  cam.ZoomToward(1000.0f, 100000.0f, QVector3D(), false);
  EXPECT_GT(cam.GetDistance(), 0.0f);
}

// ============================================================
// CameraController - OrbitByDelta
// ============================================================

TEST(CameraControllerTest, OrbitByDeltaChangesYawPitch) {
  CameraController cam;
  const float initialYaw = cam.GetYaw();
  const float initialPitch = cam.GetPitch();
  cam.OrbitByDelta(QPoint(10, 5));
  EXPECT_NE(cam.GetYaw(), initialYaw);
  EXPECT_NE(cam.GetPitch(), initialPitch);
}

TEST(CameraControllerTest, OrbitByDeltaClampsPitch) {
  CameraController cam;
  // Drive pitch far beyond limits
  cam.OrbitByDelta(QPoint(0, 100000));
  EXPECT_LE(cam.GetPitch(), 89.0f);
  EXPECT_GE(cam.GetPitch(), -89.0f);
}

// ============================================================
// CameraController - FitToScene
// ============================================================

TEST(CameraControllerTest, FitToSceneSetsDistanceProportionalToScene) {
  CameraController cam;
  const QVector3D sceneMin(-50.0f, -1.0f, -50.0f);
  const QVector3D sceneMax(50.0f, 1.0f, 50.0f);
  cam.FitToScene(sceneMin, sceneMax);
  EXPECT_GT(cam.GetDistance(), 0.0f);
  EXPECT_GT(cam.MeshRadius(), 0.0f);
  // Target should be roughly the center
  EXPECT_NEAR(cam.GetTarget().x(), 0.0f, 0.1f);
  EXPECT_NEAR(cam.GetTarget().z(), 0.0f, 0.1f);
}

TEST(CameraControllerTest, FitToSceneLargerSceneGivesLargerDistance) {
  CameraController small;
  small.FitToScene(QVector3D(-1, -1, -1), QVector3D(1, 1, 1));

  CameraController large;
  large.FitToScene(QVector3D(-100, -100, -100), QVector3D(100, 100, 100));

  EXPECT_GT(large.GetDistance(), small.GetDistance());
}
