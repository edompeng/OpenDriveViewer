#include "src/logic/scene_index_builder.h"
#include <gtest/gtest.h>

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

TEST(SceneIndexBuilderTest, CollectsOnlyMatchingElementRanges) {
  std::vector<SceneCachedElement> elements = {
      SceneCachedElement{"R:a", "G:a:section", "E:a:lane:0:1", {{0, 1}}},
      SceneCachedElement{"R:b", "G:b:section", "E:b:lane:0:1", {{1, 1}}},
  };
  const std::vector<uint32_t> indices = {0, 1, 2, 1, 3, 2};

  const auto result = CollectSceneIndices(
      elements, indices, 10, [](const SceneCachedElement& element) {
        return element.road_key == "R:b";
      });

  const std::vector<uint32_t> expected = {11, 13, 12};
  EXPECT_EQ(result, expected);
}

TEST(SceneIndexBuilderTest, ComputesChunkBoundsFromMeshVertices) {
  const odr::Mesh3D mesh = MakeQuadMesh();
  const std::vector<uint32_t> indices = {0, 1, 2, 1, 3, 2};

  const auto chunks = BuildSceneMeshChunks(indices, 0, mesh, 6);

  ASSERT_EQ(chunks.size(), std::size_t(1));
  EXPECT_EQ(chunks.front().index_offset, std::size_t(0));
  EXPECT_EQ(chunks.front().index_count, std::size_t(6));
  EXPECT_FLOAT_EQ(chunks.front().min_bound.x(), 0.0f);
  EXPECT_FLOAT_EQ(chunks.front().min_bound.z(), 0.0f);
  EXPECT_FLOAT_EQ(chunks.front().max_bound.x(), 1.0f);
  EXPECT_FLOAT_EQ(chunks.front().max_bound.z(), 1.0f);
}

TEST(SceneIndexBuilderTest, ReturnsIndicesAndChunksTogether) {
  const odr::Mesh3D mesh = MakeQuadMesh();
  std::vector<SceneCachedElement> elements = {
      SceneCachedElement{"R:a", "G:a:section", "E:a:lane:0:1", {{0, 2}}},
  };

  const auto result = BuildSceneLayerIndex(
      elements, mesh.indices, 5, mesh,
      [](const SceneCachedElement&) { return true; }, 6);

  const std::vector<uint32_t> expected_indices = {5, 6, 7, 6, 8, 7};
  EXPECT_EQ(result.indices, expected_indices);
  ASSERT_EQ(result.chunks.size(), std::size_t(1));
  EXPECT_EQ(result.chunks.front().index_count, std::size_t(6));
}
