#include "src/logic/spatial_grid_index.h"
#include <gtest/gtest.h>

namespace {

odr::Mesh3D MakeSingleTriangleMesh() {
  odr::Mesh3D mesh;
  mesh.vertices = {
      {0.0, 0.0, 0.0},
      {1.0, 0.0, 0.0},
      {0.0, 0.0, 1.0},
  };
  mesh.indices = {0, 1, 2};
  return mesh;
}

}  // namespace

TEST(SpatialGridIndexTest, SpatialGridBuilderPlacesTrianglesIntoBoxes) {
  const odr::Mesh3D mesh = MakeSingleTriangleMesh();
  const auto grid_data =
      BuildSpatialGridBoxes(mesh, {SceneMeshLayerView{&mesh, 1, {}}}, 2);

  ASSERT_EQ(grid_data.boxes.size(), std::size_t(4));
  bool found_triangle = false;
  for (const auto& box : grid_data.boxes) {
    if (box.index_count > 0) {
      found_triangle = true;
      uint32_t encoded = grid_data.flat_indices[box.index_offset];
      EXPECT_EQ((encoded >> 28), uint32_t(1));
    }
  }
  EXPECT_TRUE(found_triangle);
}

TEST(SpatialGridIndexTest, SpatialPickReturnsNearestVisibleTriangle) {
  const odr::Mesh3D mesh = MakeSingleTriangleMesh();
  const auto grid_data =
      BuildSpatialGridBoxes(mesh, {SceneMeshLayerView{&mesh, 2, {}}}, 2);

  const auto result = PickFromSpatialGrid(
      grid_data, QVector3D(0.1f, 1.0f, 0.1f), QVector3D(0.0f, -1.0f, 0.0f),
      [&mesh](uint32_t layer_tag) -> const odr::Mesh3D* {
        return layer_tag == 2 ? &mesh : nullptr;
      },
      [](uint32_t layer_tag) { return layer_tag == 2; },
      [](uint32_t, uint32_t, size_t) { return true; });

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->layer_tag, uint32_t(2));
  EXPECT_EQ(result->vertex_index, std::size_t(0));
  EXPECT_NEAR(result->distance, 1.0f, 1e-5f);
}

TEST(SpatialGridIndexTest, ScreenRayBuilderConvertsViewportPositionToWorldRay) {
  QVector3D origin;
  QVector3D direction;
  BuildRayFromScreenPoint(50, 50, QSize(100, 100), QMatrix4x4(), origin,
                          direction);

  EXPECT_NEAR(origin.x(), 0.0f, 1e-5f);
  EXPECT_NEAR(origin.y(), 0.0f, 1e-5f);
  EXPECT_NEAR(origin.z(), -1.0f, 1e-5f);
  EXPECT_NEAR(direction.x(), 0.0f, 1e-5f);
  EXPECT_NEAR(direction.y(), 0.0f, 1e-5f);
  EXPECT_NEAR(direction.z(), 1.0f, 1e-5f);
}

TEST(SpatialGridIndexTest, RaycastAllHitsReturnsSingleHitOnOneTriangle) {
  const odr::Mesh3D mesh = MakeSingleTriangleMesh();
  const auto grid_data =
      BuildSpatialGridBoxes(mesh, {SceneMeshLayerView{&mesh, 2, {}}}, 2);

  const auto hits = RaycastAllHits(
      grid_data, QVector3D(0.1f, 1.0f, 0.1f), QVector3D(0.0f, -1.0f, 0.0f),
      [&mesh](uint32_t layer_tag) -> const odr::Mesh3D* {
        return layer_tag == 2 ? &mesh : nullptr;
      },
      [](uint32_t layer_tag) { return layer_tag == 2; },
      [](uint32_t, uint32_t, size_t) { return true; });

  ASSERT_EQ(hits.size(), std::size_t(1));
  EXPECT_EQ(hits[0].layer_tag, uint32_t(2));
  EXPECT_NEAR(hits[0].distance, 1.0f, 1e-5f);
}

TEST(SpatialGridIndexTest,
     RaycastAllHitsReturnsMultipleHitsOnStackedTriangles) {
  // Create two triangles at different Y heights (stacked vertically)
  odr::Mesh3D mesh;
  mesh.vertices = {
      // Bottom triangle at Y=0
      {0.0, 0.0, 0.0},
      {2.0, 0.0, 0.0},
      {0.0, 0.0, 2.0},
      // Top triangle at Y=5
      {0.0, 5.0, 0.0},
      {2.0, 5.0, 0.0},
      {0.0, 5.0, 2.0},
  };
  mesh.indices = {0, 1, 2, 3, 4, 5};

  const auto grid_data =
      BuildSpatialGridBoxes(mesh, {SceneMeshLayerView{&mesh, 1, {}}}, 2);

  // Ray from Y=10 looking straight down at (0.5, 0.5)
  const auto hits = RaycastAllHits(
      grid_data, QVector3D(0.5f, 10.0f, 0.5f), QVector3D(0.0f, -1.0f, 0.0f),
      [&mesh](uint32_t layer_tag) -> const odr::Mesh3D* {
        return layer_tag == 1 ? &mesh : nullptr;
      },
      [](uint32_t layer_tag) { return layer_tag == 1; },
      [](uint32_t, uint32_t, size_t) { return true; });

  ASSERT_EQ(hits.size(), std::size_t(2));
  // Closest hit is the top triangle (Y=5, distance=5)
  EXPECT_NEAR(hits[0].distance, 5.0f, 1e-5f);
  EXPECT_NEAR(hits[0].position.y(), 5.0f, 1e-5f);
  // Second hit is the bottom triangle (Y=0, distance=10)
  EXPECT_NEAR(hits[1].distance, 10.0f, 1e-5f);
  EXPECT_NEAR(hits[1].position.y(), 0.0f, 1e-5f);
}
