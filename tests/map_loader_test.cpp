#include "src/core/map_loader.h"
#include <gtest/gtest.h>
#include "OpenDriveMap.h"
#include "Road.h"
#include "tests/test_helpers.h"

TEST(MapLoaderTest, PerRoadMeshCachingAndMerging) {
  std::string map_path = geoviewer::test::FindTestData("data/test2.xodr");
  OpenDriveMapSceneLoader loader;
  MapSceneData data = loader.Load(map_path);

  ASSERT_TRUE(data.IsValid());
  EXPECT_FALSE(data.road_id_to_mesh.empty());

  // Check that merging the cache equals the combined mesh
  odr::RoadNetworkMesh combined_from_cache = geoviewer::core::MergeRoadMeshes(data.road_id_to_mesh);
  
  // Verify vertices size match
  EXPECT_EQ(data.mesh.lanes_mesh.vertices.size(), combined_from_cache.lanes_mesh.vertices.size());
  EXPECT_EQ(data.mesh.roadmarks_mesh.vertices.size(), combined_from_cache.roadmarks_mesh.vertices.size());

  // Pick a road and regenerate its mesh
  std::string road_id = data.map->id_to_road.begin()->first;
  const odr::Road& road = data.map->id_to_road.begin()->second;

  odr::RoadNetworkMesh single_mesh = geoviewer::core::GenerateSingleRoadMesh(road, 0.75);

  // Update the cache for that road
  data.road_id_to_mesh[road_id] = single_mesh;

  // Merge again
  odr::RoadNetworkMesh merged_after_update = geoviewer::core::MergeRoadMeshes(data.road_id_to_mesh);
  EXPECT_EQ(data.mesh.lanes_mesh.vertices.size(), merged_after_update.lanes_mesh.vertices.size());
}
