#pragma once

#include <functional>
#include <vector>
#include "src/core/scene_geometry_types.h"
#include "third_party/libOpenDRIVE/include/Mesh.h"

struct SceneLayerIndexResult {
  std::vector<uint32_t> indices;
  std::vector<SceneMeshChunk> chunks;
};

/// @brief Collects scene geometry vertex indices meeting filter criteria
/// @param elements List of pre-partitioned scene elements
/// @param source_indices Original vertex indices from the global unified mesh
/// @param vertex_offset Start offset unique to each layer/chunk
/// @param predicate Predicate function determining if an object should be
/// included
/// @return Final rendering index data for all matching elements
std::vector<uint32_t> CollectSceneIndices(
    const std::vector<SceneCachedElement>& elements,
    const std::vector<uint32_t>& source_indices, size_t vertex_offset,
    const std::function<bool(const SceneCachedElement&)>& predicate);

std::vector<SceneMeshChunk> BuildSceneMeshChunks(
    const std::vector<uint32_t>& indices, size_t vertex_offset,
    const odr::Mesh3D& mesh, size_t chunk_size = 3000);

/// @brief Facade structure for scene building: combines collection and chunking
/// @param elements Cached scene entity objects grouped logically
/// @param source_indices Original indices containing all geometric primitive
/// sequences
/// @param vertex_offset Base address offset used by the processed batch/layer
/// @param mesh Reference to specific model vertex data
/// @param predicate Strategy defining which entities need to be included
/// @param chunk_size Maximum threshold for splitting draw operations by
/// bounding box
/// @return Layer index results split and meeting visibility requirements
SceneLayerIndexResult BuildSceneLayerIndex(
    const std::vector<SceneCachedElement>& elements,
    const std::vector<uint32_t>& source_indices, size_t vertex_offset,
    const odr::Mesh3D& mesh,
    const std::function<bool(const SceneCachedElement&)>& predicate,
    size_t chunk_size = 3000);
