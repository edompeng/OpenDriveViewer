#include "src/ui/widgets/layer_tree_model.h"
#include <gtest/gtest.h>
#include "OpenDriveMap.h"

#include <fstream>
#include <vector>
#include <string>

namespace {
std::string FindTestData(const std::string& filename) {
  std::vector<std::string> search_paths = {
      std::string(GEOVIEWER_SOURCE_DIR) + "/" + filename,
      std::string(GEOVIEWER_SOURCE_DIR) + "/_main/" + filename,
      std::string(GEOVIEWER_SOURCE_DIR) + "/geoviewer/" + filename,
      filename,
      "../" + filename
  };

  if (const char* test_srcdir = std::getenv("TEST_SRCDIR")) {
      std::string srcdir = test_srcdir;
      search_paths.push_back(srcdir + "/geoviewer/" + filename);
      search_paths.push_back(srcdir + "/_main/" + filename);
      if (const char* test_workspace = std::getenv("TEST_WORKSPACE")) {
          search_paths.push_back(srcdir + "/" + test_workspace + "/" + filename);
      }
  }
  
  for (const auto& path : search_paths) {
      std::ifstream f(path);
      if (f.good()) {
          return path;
      }
  }
  
  if (const char* manifest_path = std::getenv("RUNFILES_MANIFEST_FILE")) {
      std::ifstream manifest(manifest_path);
      std::string line;
      while (std::getline(manifest, line)) {
          if (!line.empty() && line.back() == '\r') {
              line.pop_back();
          }
          auto space_pos = line.find(' ');
          if (space_pos != std::string::npos) {
              std::string runfile_path = line.substr(0, space_pos);
              std::string abs_path = line.substr(space_pos + 1);
              if (runfile_path.size() >= filename.size() && 
                  runfile_path.compare(runfile_path.size() - filename.size(), filename.size(), filename) == 0) {
                  std::ifstream f(abs_path);
                  if (f.good()) return abs_path;
              }
          }
      }
  }
  
  throw std::runtime_error("Could not find test data file: " + filename);
}
} // namespace

TEST(LayerTreeModelTest, FullIdBuilderMapsNodeTypesConsistently) {
  EXPECT_EQ(BuildLayerTreeFullId("r1", TreeNodeType::kRoad, "r1"), "R:r1");
  EXPECT_EQ(BuildLayerTreeFullId("r1", TreeNodeType::kLane, "0:1"), "E:r1:lane:0:1");
  EXPECT_EQ(BuildLayerTreeFullId("jg", TreeNodeType::kJunction, "j1"), "J:jg:j1");
}

TEST(LayerTreeModelTest, CheckStateReflectsHiddenDescendants) {
  RoadSnapshot road;
  road.road_id = "r1";
  road.lanes.push_back({"0:1", "lane", TreeNodeType::kLane});
  std::unordered_set<std::string> hidden = {"E:r1:lane:0:1"};
  EXPECT_EQ(ComputeRoadCheckState(road, hidden), Qt::PartiallyChecked);

  JunctionGroupSnapshot group;
  group.group_id = "jg1";
  group.junction_ids = {"j1"};
  hidden = {"J:jg1:j1"};
  EXPECT_EQ(ComputeJunctionGroupCheckState(group, hidden), Qt::PartiallyChecked);
}

TEST(LayerTreeModelTest, SnapshotBuilderCreatesRoadsAndJunctionGroups) {
  const std::string map_path = FindTestData("data/test2.xodr");
  odr::OpenDriveMap map(map_path);
  const JunctionClusterResult result = JunctionClusterUtil::Analyze(map);
  const auto snapshot = BuildLayerTreeSnapshot(
      std::make_shared<odr::OpenDriveMap>(map_path), result);

  ASSERT_TRUE(snapshot);
  EXPECT_EQ(snapshot->junction_count, static_cast<int>(map.id_to_junction.size()));
  EXPECT_FALSE(snapshot->roads.empty());
  EXPECT_FALSE(snapshot->junction_groups.empty());
}
