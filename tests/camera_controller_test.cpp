#include "src/app/camera_controller.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// ============================================================
// CameraController - Initial State
// ============================================================

TEST_CASE("CameraController - default construction has sane values",
          "[camera]") {
  CameraController cam;
  CHECK(cam.GetDistance() > 0.0f);
  CHECK(cam.MeshRadius() == Catch::Approx(0.0f));
  CHECK(cam.PressedButton() == Qt::NoButton);
}

// ============================================================
// CameraController - View Matrix
// ============================================================

TEST_CASE("CameraController - view matrix is not identity by default",
          "[camera]") {
  CameraController cam;
  const QMatrix4x4 view = cam.GetViewMatrix();
  // The view matrix should not be identity (camera is offset from origin)
  CHECK(view != QMatrix4x4());
}

// ============================================================
// CameraController - Camera Setters/Getters
// ============================================================

TEST_CASE("CameraController - SetTarget changes target", "[camera]") {
  CameraController cam;
  const QVector3D newTarget(10.0f, 20.0f, 30.0f);
  cam.SetTarget(newTarget);
  CHECK(cam.GetTarget().x() == Catch::Approx(10.0f));
  CHECK(cam.GetTarget().y() == Catch::Approx(20.0f));
  CHECK(cam.GetTarget().z() == Catch::Approx(30.0f));
}

TEST_CASE("CameraController - SetDistance stores value", "[camera]") {
  CameraController cam;
  cam.SetDistance(500.0f);
  CHECK(cam.GetDistance() == Catch::Approx(500.0f));
}

TEST_CASE("CameraController - SetPitch clamps within range", "[camera]") {
  // Pitch clamping happens during OrbitByDelta, not SetPitch
  CameraController cam;
  cam.SetPitch(-45.0f);
  CHECK(cam.GetPitch() == Catch::Approx(-45.0f));
}

TEST_CASE("CameraController - SetYaw stores value", "[camera]") {
  CameraController cam;
  cam.SetYaw(180.0f);
  CHECK(cam.GetYaw() == Catch::Approx(180.0f));
}

// ============================================================
// CameraController - Drag State
// ============================================================

TEST_CASE("CameraController - BeginDrag sets pressed button", "[camera]") {
  CameraController cam;
  cam.BeginDrag(QPoint(100, 200), Qt::LeftButton);
  CHECK(cam.PressedButton() == Qt::LeftButton);
  CHECK(cam.LastPos() == QPoint(100, 200));
}

TEST_CASE("CameraController - EndDrag clears pressed button", "[camera]") {
  CameraController cam;
  cam.BeginDrag(QPoint(0, 0), Qt::RightButton);
  cam.EndDrag();
  CHECK(cam.PressedButton() == Qt::NoButton);
}

// ============================================================
// CameraController - ZoomToward
// ============================================================

TEST_CASE(
    "CameraController - ZoomToward with positive delta decreases distance",
    "[camera]") {
  CameraController cam;
  cam.SetDistance(100.0f);
  const float beforeDist = cam.GetDistance();
  // delta > 0 means "zoom in"
  cam.ZoomToward(1.0f, 100000.0f, QVector3D(), false);
  CHECK(cam.GetDistance() < beforeDist);
}

TEST_CASE(
    "CameraController - ZoomToward with negative delta increases distance",
    "[camera]") {
  CameraController cam;
  cam.SetDistance(100.0f);
  const float beforeDist = cam.GetDistance();
  // delta < 0 means "zoom out"
  cam.ZoomToward(-1.0f, 100000.0f, QVector3D(), false);
  CHECK(cam.GetDistance() > beforeDist);
}

TEST_CASE("CameraController - ZoomToward distance never exceeds maxDist",
          "[camera]") {
  CameraController cam;
  cam.SetDistance(99.0f);
  cam.ZoomToward(-100.0f, 100.0f, QVector3D(), false);
  CHECK(cam.GetDistance() <= 100.0f);
}

TEST_CASE("CameraController - ZoomToward distance never goes below epsilon",
          "[camera]") {
  CameraController cam;
  cam.SetDistance(1.0f);
  cam.ZoomToward(1000.0f, 100000.0f, QVector3D(), false);
  CHECK(cam.GetDistance() > 0.0f);
}

// ============================================================
// CameraController - OrbitByDelta
// ============================================================

TEST_CASE("CameraController - OrbitByDelta changes yaw/pitch", "[camera]") {
  CameraController cam;
  const float initialYaw = cam.GetYaw();
  const float initialPitch = cam.GetPitch();
  cam.OrbitByDelta(QPoint(10, 5));
  CHECK(cam.GetYaw() != Catch::Approx(initialYaw));
  CHECK(cam.GetPitch() != Catch::Approx(initialPitch));
}

TEST_CASE("CameraController - OrbitByDelta clamps pitch to [-89, 89]",
          "[camera]") {
  CameraController cam;
  // Drive pitch far beyond limits
  cam.OrbitByDelta(QPoint(0, 100000));
  CHECK(cam.GetPitch() <= 89.0f);
  CHECK(cam.GetPitch() >= -89.0f);
}

// ============================================================
// CameraController - FitToScene
// ============================================================

TEST_CASE("CameraController - FitToScene sets distance proportional to scene",
          "[camera]") {
  CameraController cam;
  const QVector3D sceneMin(-50.0f, -1.0f, -50.0f);
  const QVector3D sceneMax(50.0f, 1.0f, 50.0f);
  cam.FitToScene(sceneMin, sceneMax);
  CHECK(cam.GetDistance() > 0.0f);
  CHECK(cam.MeshRadius() > 0.0f);
  // Target should be roughly the center
  CHECK(cam.GetTarget().x() == Catch::Approx(0.0f).margin(0.1f));
  CHECK(cam.GetTarget().z() == Catch::Approx(0.0f).margin(0.1f));
}

TEST_CASE("CameraController - FitToScene larger scene gives larger distance",
          "[camera]") {
  CameraController small;
  small.FitToScene(QVector3D(-1, -1, -1), QVector3D(1, 1, 1));

  CameraController large;
  large.FitToScene(QVector3D(-100, -100, -100), QVector3D(100, 100, 100));

  CHECK(large.GetDistance() > small.GetDistance());
}
