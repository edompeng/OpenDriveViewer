#include "src/app/spatial_grid_index.h"

#include "third_party/Catch2/src/catch2/catch_approx.hpp"
#include "third_party/Catch2/src/catch2/catch_test_macros.hpp"

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

TEST_CASE("Spatial grid builder places triangles into boxes",
          "[spatial-grid]") {
  const odr::Mesh3D mesh = MakeSingleTriangleMesh();
  const auto boxes =
      BuildSpatialGridBoxes(mesh, {SceneMeshLayerView{&mesh, 1, {}}}, 2);

  REQUIRE(boxes.size() == 4);
  bool found_triangle = false;
  for (const auto& box : boxes) {
    if (!box.triangle_indices.empty()) {
      found_triangle = true;
      CHECK((box.triangle_indices.front() >> 28) == 1);
    }
  }
  CHECK(found_triangle);
}

TEST_CASE("Spatial pick returns nearest visible triangle", "[spatial-grid]") {
  const odr::Mesh3D mesh = MakeSingleTriangleMesh();
  const auto boxes =
      BuildSpatialGridBoxes(mesh, {SceneMeshLayerView{&mesh, 2, {}}}, 2);

  const auto result = PickFromSpatialGrid(
      boxes, QVector3D(0.1f, 1.0f, 0.1f), QVector3D(0.0f, -1.0f, 0.0f),
      [&mesh](uint32_t layer_tag) -> const odr::Mesh3D* {
        return layer_tag == 2 ? &mesh : nullptr;
      },
      [](uint32_t layer_tag) { return layer_tag == 2; },
      [](uint32_t, uint32_t, size_t) { return true; });

  REQUIRE(result.has_value());
  CHECK(result->layer_tag == 2);
  CHECK(result->vertex_index == 0);
  CHECK(result->distance == Catch::Approx(1.0f));
}

TEST_CASE("Screen ray builder converts viewport position to world ray",
          "[spatial-grid]") {
  QVector3D origin;
  QVector3D direction;
  BuildRayFromScreenPoint(50, 50, QSize(100, 100), QMatrix4x4(), origin,
                          direction);

  CHECK(origin.x() == Catch::Approx(0.0f));
  CHECK(origin.y() == Catch::Approx(0.0f));
  CHECK(origin.z() == Catch::Approx(-1.0f));
  CHECK(direction.x() == Catch::Approx(0.0f));
  CHECK(direction.y() == Catch::Approx(0.0f));
  CHECK(direction.z() == Catch::Approx(1.0f));
}

TEST_CASE("RaycastAllHits returns single hit on one triangle",
          "[spatial-grid]") {
  const odr::Mesh3D mesh = MakeSingleTriangleMesh();
  const auto boxes =
      BuildSpatialGridBoxes(mesh, {SceneMeshLayerView{&mesh, 2, {}}}, 2);

  const auto hits = RaycastAllHits(
      boxes, QVector3D(0.1f, 1.0f, 0.1f), QVector3D(0.0f, -1.0f, 0.0f),
      [&mesh](uint32_t layer_tag) -> const odr::Mesh3D* {
        return layer_tag == 2 ? &mesh : nullptr;
      },
      [](uint32_t layer_tag) { return layer_tag == 2; },
      [](uint32_t, uint32_t, size_t) { return true; });

  REQUIRE(hits.size() == 1);
  CHECK(hits[0].layer_tag == 2);
  CHECK(hits[0].distance == Catch::Approx(1.0f));
}

TEST_CASE("RaycastAllHits returns multiple hits on stacked triangles",
          "[spatial-grid]") {
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

  const auto boxes =
      BuildSpatialGridBoxes(mesh, {SceneMeshLayerView{&mesh, 1, {}}}, 2);

  // Ray from Y=10 looking straight down at (0.5, 0.5)
  const auto hits = RaycastAllHits(
      boxes, QVector3D(0.5f, 10.0f, 0.5f), QVector3D(0.0f, -1.0f, 0.0f),
      [&mesh](uint32_t layer_tag) -> const odr::Mesh3D* {
        return layer_tag == 1 ? &mesh : nullptr;
      },
      [](uint32_t layer_tag) { return layer_tag == 1; },
      [](uint32_t, uint32_t, size_t) { return true; });

  REQUIRE(hits.size() == 2);
  // Closest hit is the top triangle (Y=5, distance=5)
  CHECK(hits[0].distance == Catch::Approx(5.0f));
  CHECK(hits[0].position.y() == Catch::Approx(5.0f));
  // Second hit is the bottom triangle (Y=0, distance=10)
  CHECK(hits[1].distance == Catch::Approx(10.0f));
  CHECK(hits[1].position.y() == Catch::Approx(0.0f));
}
