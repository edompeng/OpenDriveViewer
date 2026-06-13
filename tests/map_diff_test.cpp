#include <gtest/gtest.h>
#include "OpenDriveMap.h"
#include "src/logic/map_diff_analyzer.h"

TEST(MapDiffTest, EmptyAnalyze) {
  using namespace geoviewer::logic;
  std::string map_path = std::string(GEOVIEWER_SOURCE_DIR) + "/data/test2.xodr";
  auto base = std::make_shared<odr::OpenDriveMap>(map_path);
  auto target = std::make_shared<odr::OpenDriveMap>(map_path);

  auto diff = MapDiffAnalyzer::Analyze(base, target);
  EXPECT_TRUE(diff.added_lanes.empty());
  EXPECT_TRUE(diff.removed_lanes.empty());
  EXPECT_TRUE(diff.modified_lanes.empty());
}

TEST(MapDiffTest, NullInputs) {
  using namespace geoviewer::logic;
  std::string map_path = std::string(GEOVIEWER_SOURCE_DIR) + "/data/test2.xodr";
  auto base = std::make_shared<odr::OpenDriveMap>(map_path);
  
  auto diff1 = MapDiffAnalyzer::Analyze(nullptr, base);
  EXPECT_TRUE(diff1.added_lanes.empty());
  EXPECT_TRUE(diff1.removed_lanes.empty());
  EXPECT_TRUE(diff1.modified_lanes.empty());

  auto diff2 = MapDiffAnalyzer::Analyze(base, nullptr);
  EXPECT_TRUE(diff2.added_lanes.empty());
  EXPECT_TRUE(diff2.removed_lanes.empty());
  EXPECT_TRUE(diff2.modified_lanes.empty());
}

