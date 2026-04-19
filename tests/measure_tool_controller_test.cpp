#include "src/logic/measure_tool_controller.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// ============================================================
// MeasureToolController - Initial State
// ============================================================

TEST_CASE("MeasureToolController - default construction has sane values",
          "[measure_tool]") {
  MeasureToolController ctrl;
  CHECK(ctrl.IsActive() == false);
  CHECK(ctrl.Points().empty() == true);
  CHECK(ctrl.TotalDistance() == Catch::Approx(0.0));
}

// ============================================================
// MeasureToolController - State Control
// ============================================================

TEST_CASE("MeasureToolController - SetActive updates state and emits signal",
          "[measure_tool]") {
  MeasureToolController ctrl;
  int emitCount = 0;
  bool lastEmittedVal = false;
  QObject::connect(&ctrl, &MeasureToolController::activeChanged,
                   [&](bool active) {
                     emitCount++;
                     lastEmittedVal = active;
                   });

  ctrl.SetActive(true);
  CHECK(ctrl.IsActive() == true);
  REQUIRE(emitCount == 1);
  CHECK(lastEmittedVal == true);

  // Calling with same state should not emit signal
  ctrl.SetActive(true);
  REQUIRE(emitCount == 1);

  ctrl.SetActive(false);
  CHECK(ctrl.IsActive() == false);
  REQUIRE(emitCount == 2);
  CHECK(lastEmittedVal == false);
}

// ============================================================
// MeasureToolController - Points & Distance
// ============================================================

TEST_CASE(
    "MeasureToolController - AddPoint updates points, distance, and emits "
    "signals",
    "[measure_tool]") {
  MeasureToolController ctrl;

  int distanceEmitCount = 0;
  double lastDistance = -1.0;
  QObject::connect(&ctrl, &MeasureToolController::TotalDistanceChanged,
                   [&](double d) {
                     distanceEmitCount++;
                     lastDistance = d;
                   });

  int pointsEmitCount = 0;
  QObject::connect(&ctrl, &MeasureToolController::pointsChanged,
                   [&]() { pointsEmitCount++; });

  ctrl.AddPoint(QVector3D(0.0f, 0.0f, 0.0f));
  CHECK(ctrl.Points().size() == 1);
  CHECK(ctrl.TotalDistance() == Catch::Approx(0.0));
  REQUIRE(distanceEmitCount == 1);
  CHECK(lastDistance == Catch::Approx(0.0));
  REQUIRE(pointsEmitCount == 1);

  ctrl.AddPoint(QVector3D(10.0f, 0.0f, 0.0f));
  CHECK(ctrl.Points().size() == 2);
  CHECK(ctrl.TotalDistance() == Catch::Approx(10.0));
  REQUIRE(distanceEmitCount == 2);
  CHECK(lastDistance == Catch::Approx(10.0));
  REQUIRE(pointsEmitCount == 2);

  ctrl.AddPoint(QVector3D(10.0f, 10.0f, 0.0f));
  CHECK(ctrl.Points().size() == 3);
  CHECK(ctrl.TotalDistance() == Catch::Approx(20.0));
}

TEST_CASE("MeasureToolController - ClearPoints removes all and emits signals",
          "[measure_tool]") {
  MeasureToolController ctrl;
  ctrl.AddPoint(QVector3D(0.0f, 0.0f, 0.0f));
  ctrl.AddPoint(QVector3D(10.0f, 0.0f, 0.0f));

  int distanceEmitCount = 0;
  double lastDistance = -1.0;
  QObject::connect(&ctrl, &MeasureToolController::TotalDistanceChanged,
                   [&](double d) {
                     distanceEmitCount++;
                     lastDistance = d;
                   });

  int pointsEmitCount = 0;
  QObject::connect(&ctrl, &MeasureToolController::pointsChanged,
                   [&]() { pointsEmitCount++; });

  ctrl.ClearPoints();
  CHECK(ctrl.Points().empty() == true);
  CHECK(ctrl.TotalDistance() == Catch::Approx(0.0));
  REQUIRE(distanceEmitCount == 1);
  CHECK(lastDistance == Catch::Approx(0.0));
  REQUIRE(pointsEmitCount == 1);
}
