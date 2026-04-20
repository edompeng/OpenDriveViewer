#include "src/logic/measure_tool_controller.h"
#include <gtest/gtest.h>

// ============================================================
// MeasureToolController - Initial State
// ============================================================

TEST(MeasureToolControllerTest, DefaultConstructionHasSaneValues) {
  MeasureToolController ctrl;
  EXPECT_FALSE(ctrl.IsActive());
  EXPECT_TRUE(ctrl.Points().empty());
  EXPECT_DOUBLE_EQ(ctrl.TotalDistance(), 0.0);
}

// ============================================================
// MeasureToolController - State Control
// ============================================================

TEST(MeasureToolControllerTest, SetActiveUpdatesStateAndEmitsSignal) {
  MeasureToolController ctrl;
  int emitCount = 0;
  bool lastEmittedVal = false;
  QObject::connect(&ctrl, &MeasureToolController::activeChanged,
                   [&](bool active) {
                     emitCount++;
                     lastEmittedVal = active;
                   });

  ctrl.SetActive(true);
  EXPECT_TRUE(ctrl.IsActive());
  ASSERT_EQ(emitCount, 1);
  EXPECT_TRUE(lastEmittedVal);

  // Calling with same state should not emit signal
  ctrl.SetActive(true);
  ASSERT_EQ(emitCount, 1);

  ctrl.SetActive(false);
  EXPECT_FALSE(ctrl.IsActive());
  ASSERT_EQ(emitCount, 2);
  EXPECT_FALSE(lastEmittedVal);
}

// ============================================================
// MeasureToolController - Points & Distance
// ============================================================

TEST(MeasureToolControllerTest, AddPointUpdatesPointsDistanceAndEmitsSignals) {
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
  EXPECT_EQ(ctrl.Points().size(), std::size_t(1));
  EXPECT_DOUBLE_EQ(ctrl.TotalDistance(), 0.0);
  ASSERT_EQ(distanceEmitCount, 1);
  EXPECT_DOUBLE_EQ(lastDistance, 0.0);
  ASSERT_EQ(pointsEmitCount, 1);

  ctrl.AddPoint(QVector3D(10.0f, 0.0f, 0.0f));
  EXPECT_EQ(ctrl.Points().size(), std::size_t(2));
  EXPECT_DOUBLE_EQ(ctrl.TotalDistance(), 10.0);
  ASSERT_EQ(distanceEmitCount, 2);
  EXPECT_DOUBLE_EQ(lastDistance, 10.0);
  ASSERT_EQ(pointsEmitCount, 2);

  ctrl.AddPoint(QVector3D(10.0f, 10.0f, 0.0f));
  EXPECT_EQ(ctrl.Points().size(), std::size_t(3));
  EXPECT_DOUBLE_EQ(ctrl.TotalDistance(), 20.0);
}

TEST(MeasureToolControllerTest, ClearPointsRemovesAllAndEmitsSignals) {
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
  EXPECT_TRUE(ctrl.Points().empty());
  EXPECT_DOUBLE_EQ(ctrl.TotalDistance(), 0.0);
  ASSERT_EQ(distanceEmitCount, 1);
  EXPECT_DOUBLE_EQ(lastDistance, 0.0);
  ASSERT_EQ(pointsEmitCount, 1);
}
