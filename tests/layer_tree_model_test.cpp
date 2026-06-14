#include "src/ui/widgets/layer_tree_model.h"
#include <gtest/gtest.h>
#include "OpenDriveMap.h"
#include "tests/test_helpers.h"

namespace {

TEST(LayerTreeModelTest, FullIdBuilderMapsNodeTypesConsistently) {
  EXPECT_EQ(BuildLayerTreeFullId("r1", TreeNodeType::kRoad, "r1"), "R:r1");
  EXPECT_EQ(BuildLayerTreeFullId("r1", TreeNodeType::kLane, "0:1"),
            "E:r1:lane:0:1");
  EXPECT_EQ(BuildLayerTreeFullId("jg", TreeNodeType::kJunction, "j1"),
            "J:jg:j1");
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
  EXPECT_EQ(ComputeJunctionGroupCheckState(group, hidden),
            Qt::PartiallyChecked);
}

TEST(LayerTreeModelTest, SnapshotBuilderCreatesRoadsAndJunctionGroups) {
  const std::string map_path = geoviewer::test::FindTestData("data/test2.xodr");
  odr::OpenDriveMap map(map_path);
  const JunctionClusterResult result = JunctionClusterUtil::Analyze(map);
  const auto snapshot = BuildLayerTreeSnapshot(
      std::make_shared<odr::OpenDriveMap>(map_path), result);

  ASSERT_TRUE(snapshot);
  EXPECT_EQ(snapshot->junction_count,
            static_cast<int>(map.id_to_junction.size()));
  EXPECT_FALSE(snapshot->roads.empty());
  EXPECT_FALSE(snapshot->junction_groups.empty());
}
}  // namespace
