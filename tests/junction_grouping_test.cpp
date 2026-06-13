#include "src/core/junction_grouping.h"
#include <gtest/gtest.h>
#include <cmath>
#include "OpenDriveMap.h"

#ifndef GEOVIEWER_SOURCE_DIR
#  define GEOVIEWER_SOURCE_DIR "."
#endif

// Compatibility helper since M_PI is standard but we want to avoid warnings
#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif

TEST(JunctionGroupingTest, ClassifyAngles) {
  // Test T-Intersection: 3 arms, typical angles: 0, pi/2, pi
  std::vector<double> t_angles = {0.0, M_PI * 0.5, M_PI};
  auto type_t = JunctionClusterUtil::ClassifyByAngles(t_angles);
  EXPECT_EQ(type_t, JunctionSemanticType::kTIntersection);

  // Test Crossroad: 4 arms, typical angles: 0, pi/2, pi, 3pi/2
  std::vector<double> cross_angles = {0.0, M_PI * 0.5, M_PI, M_PI * 1.5};
  auto type_cross = JunctionClusterUtil::ClassifyByAngles(cross_angles);
  EXPECT_EQ(type_cross, JunctionSemanticType::kCrossroad);
}

TEST(JunctionGroupingTest, BoxDistanceAndOverlap) {
  JunctionBox3D b1, b2;
  b1.min = {0.0, 0.0, 0.0};
  b1.max = {10.0, 10.0, 10.0};
  b1.valid = true;

  b2.min = {5.0, 5.0, 5.0};
  b2.max = {15.0, 15.0, 15.0};
  b2.valid = true;

  EXPECT_TRUE(JunctionClusterUtil::BoxesOverlap(b1, b2));

  // Shift b2 out
  b2.min = {12.0, 0.0, 0.0};
  b2.max = {22.0, 10.0, 10.0};
  EXPECT_FALSE(JunctionClusterUtil::BoxesOverlap(b1, b2));
  EXPECT_NEAR(JunctionClusterUtil::BoxHorizontalDistance(b1, b2), 2.0, 1e-6);
  EXPECT_NEAR(JunctionClusterUtil::BoxVerticalDistance(b1, b2), 0.0, 1e-6);
}

TEST(JunctionGroupingTest, AnalyzeTestMap) {
  std::string map_path = std::string(GEOVIEWER_SOURCE_DIR) + "/data/test2.xodr";
  auto map = std::make_shared<odr::OpenDriveMap>(map_path);

  auto result = JunctionClusterUtil::Analyze(*map);
  // Just verify it runs and returns valid indices
  for (const auto& [jid, idx] : result.junction_id_to_group_index) {
    EXPECT_LT(idx, result.groups.size());
  }
}
