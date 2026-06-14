#include "src/logic/simulation_controller.h"
#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QThread>
#include "OpenDriveMap.h"
#include "tests/test_helpers.h"

TEST(SimulationControllerTest, BasicSimulationFlow) {
  int argc = 0;
  char* argv[] = {nullptr};
  QCoreApplication app(argc, argv);

  std::string map_path = geoviewer::test::FindTestData("data/test2.xodr");
  auto map = std::make_shared<odr::OpenDriveMap>(map_path);

  // Find a valid driving lane to simulate
  std::vector<odr::LaneKey> path;
  for (const auto& [road_id, road] : map->id_to_road) {
    for (const auto& [s0, section] : road.s_to_lanesection) {
      for (const auto& [lane_id, lane] : section.id_to_lane) {
        if (lane_id != 0 && lane.type == "driving") {
          path.push_back(odr::LaneKey(road_id, s0, lane_id));
          break;
        }
      }
      if (!path.empty()) break;
    }
    if (!path.empty()) break;
  }

  ASSERT_FALSE(path.empty())
      << "Test map must contain at least one driving lane";

  geoviewer::logic::SimulationController sim;

  // Test start
  sim.Start(path, map, true, 50.0f);  // Fast speed to finish quickly
  EXPECT_TRUE(sim.IsActive());

  // Set speed
  sim.SetSpeed(100.0f);

  // Spin event loop to let simulation tick
  QDeadlineTimer deadline(1000);  // max 1 second
  while (!deadline.hasExpired() && sim.IsActive()) {
    QCoreApplication::processEvents();
    QThread::msleep(10);
  }

  // Test explicit stop
  sim.Start(path, map, true, 10.0f);
  EXPECT_TRUE(sim.IsActive());
  sim.Stop();
  EXPECT_FALSE(sim.IsActive());
}
