#include "src/logic/scene_index_builder.h"

#include <algorithm>

std::vector<uint32_t> CollectSceneIndices(
    const std::vector<SceneCachedElement>& elements,
    const std::vector<uint32_t>& source_indices, size_t vertex_offset,
    const std::function<bool(const SceneCachedElement&)>& predicate) {
  std::vector<uint32_t> indices;
  for (const auto& element : elements) {
    if (!predicate(element)) continue;
    for (const auto& range : element.ranges) {
      const std::size_t base = static_cast<std::size_t>(range.start) * 3;
      for (uint32_t k = 0; k < range.count * 3; ++k) {
        indices.push_back(source_indices[base + k] +
                          static_cast<uint32_t>(vertex_offset));
      }
    }
  }
  return indices;
}

std::vector<SceneMeshChunk> BuildSceneMeshChunks(
    const std::vector<uint32_t>& indices, size_t vertex_offset,
    const odr::Mesh3D& mesh, size_t chunk_size) {
  std::vector<SceneMeshChunk> chunks;
  for (size_t i = 0; i < indices.size(); i += chunk_size) {
    SceneMeshChunk chunk;
    chunk.index_offset = i;
    chunk.index_count = std::min(chunk_size, indices.size() - i);
    QVector3D min_bound(1e9f, 1e9f, 1e9f);
    QVector3D max_bound(-1e9f, -1e9f, -1e9f);
    for (size_t j = 0; j < chunk.index_count; ++j) {
      const uint32_t global_index = indices[i + j];
      if (global_index < vertex_offset) continue;
      const size_t local_index =
          static_cast<size_t>(global_index - vertex_offset);
      if (local_index >= mesh.vertices.size()) continue;
      const auto& vertex = mesh.vertices[local_index];
      min_bound.setX(std::min(min_bound.x(), static_cast<float>(vertex[0])));
      min_bound.setY(std::min(min_bound.y(), static_cast<float>(vertex[1])));
      min_bound.setZ(std::min(min_bound.z(), static_cast<float>(vertex[2])));
      max_bound.setX(std::max(max_bound.x(), static_cast<float>(vertex[0])));
      max_bound.setY(std::max(max_bound.y(), static_cast<float>(vertex[1])));
      max_bound.setZ(std::max(max_bound.z(), static_cast<float>(vertex[2])));
    }
    if (min_bound.x() <= max_bound.x()) {
      chunk.min_bound = min_bound;
      chunk.max_bound = max_bound;
      chunks.push_back(chunk);
    }
  }
  return chunks;
}

SceneLayerIndexResult BuildSceneLayerIndex(
    const std::vector<SceneCachedElement>& elements,
    const std::vector<uint32_t>& source_indices, size_t vertex_offset,
    const odr::Mesh3D& mesh,
    const std::function<bool(const SceneCachedElement&)>& predicate,
    size_t chunk_size) {
  SceneLayerIndexResult result;
  result.indices =
      CollectSceneIndices(elements, source_indices, vertex_offset, predicate);
  result.chunks =
      BuildSceneMeshChunks(result.indices, vertex_offset, mesh, chunk_size);
  return result;
}
