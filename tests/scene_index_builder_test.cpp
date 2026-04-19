#include "src/logic/scene_index_builder.h"

#include "third_party/Catch2/src/catch2/catch_test_macros.hpp"

namespace {

odr::Mesh3D MakeQuadMesh() {
  odr::Mesh3D mesh;
  mesh.vertices = {
      {0.0, 0.0, 0.0},
      {1.0, 0.0, 0.0},
      {0.0, 0.0, 1.0},
      {1.0, 0.0, 1.0},
  };
  mesh.indices = {0, 1, 2, 1, 3, 2};
  return mesh;
}

}  // namespace

TEST_CASE("Scene index builder collects only matching element ranges",
          "[scene-index-builder]") {
  std::vector<SceneCachedElement> elements = {
      SceneCachedElement{"R:a", "G:a:section", "E:a:lane:0:1", {{0, 1}}},
      SceneCachedElement{"R:b", "G:b:section", "E:b:lane:0:1", {{1, 1}}},
  };
  const std::vector<uint32_t> indices = {0, 1, 2, 1, 3, 2};

  const auto result = CollectSceneIndices(
      elements, indices, 10, [](const SceneCachedElement& element) {
        return element.road_key == "R:b";
      });

  REQUIRE(result == std::vector<uint32_t>{11, 13, 12});
}

TEST_CASE("Scene chunk builder computes chunk bounds from mesh vertices",
          "[scene-index-builder]") {
  const odr::Mesh3D mesh = MakeQuadMesh();
  const std::vector<uint32_t> indices = {0, 1, 2, 1, 3, 2};

  const auto chunks = BuildSceneMeshChunks(indices, 0, mesh, 6);

  REQUIRE(chunks.size() == 1);
  CHECK(chunks.front().index_offset == 0);
  CHECK(chunks.front().index_count == 6);
  CHECK(chunks.front().min_bound.x() == 0.0f);
  CHECK(chunks.front().min_bound.z() == 0.0f);
  CHECK(chunks.front().max_bound.x() == 1.0f);
  CHECK(chunks.front().max_bound.z() == 1.0f);
}

TEST_CASE("Scene layer builder returns indices and chunks together",
          "[scene-index-builder]") {
  const odr::Mesh3D mesh = MakeQuadMesh();
  std::vector<SceneCachedElement> elements = {
      SceneCachedElement{"R:a", "G:a:section", "E:a:lane:0:1", {{0, 2}}},
  };

  const auto result = BuildSceneLayerIndex(
      elements, mesh.indices, 5, mesh,
      [](const SceneCachedElement&) { return true; }, 6);

  REQUIRE(result.indices == std::vector<uint32_t>{5, 6, 7, 6, 8, 7});
  REQUIRE(result.chunks.size() == 1);
  CHECK(result.chunks.front().index_count == 6);
}
