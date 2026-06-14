#include "src/logic/scene_index_builder.h"

#include <algorithm>

std::vector<uint32_t> CollectSceneIndices(
    const std::vector<SceneCachedElement>& elements,
    const std::vector<uint32_t>& source_indices, size_t vertex_offset,
    const std::function<bool(const SceneCachedElement&)>& predicate) {
  std::size_t total_count = 0;
  std::vector<char> valid_elements(elements.size());
  for (std::size_t i = 0; i < elements.size(); ++i) {
    char valid = predicate(elements[i]) ? 1 : 0;
    valid_elements[i] = valid;
    if (valid) {
      for (const auto& range : elements[i].ranges) {
        total_count += static_cast<std::size_t>(range.count) * 3;
      }
    }
  }

  std::vector<uint32_t> indices;
  indices.reserve(total_count);
  for (std::size_t i = 0; i < elements.size(); ++i) {
    if (!valid_elements[i]) continue;
    for (const auto& range : elements[i].ranges) {
      const std::size_t base = static_cast<std::size_t>(range.start) * 3;
      if (base + static_cast<std::size_t>(range.count) * 3 >
          source_indices.size()) {
        continue;
      }
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
    float min_x = 1e9f;
    float min_y = 1e9f;
    float min_z = 1e9f;
    float max_x = -1e9f;
    float max_y = -1e9f;
    float max_z = -1e9f;
    const uint32_t* chunk_indices = &indices[i];
    for (size_t j = 0; j < chunk.index_count; ++j) {
      const uint32_t global_index = chunk_indices[j];
      if (global_index < vertex_offset) continue;
      const size_t local_index =
          static_cast<size_t>(global_index - vertex_offset);
      if (local_index >= mesh.vertices.size()) continue;
      const auto& vertex = mesh.vertices[local_index];
      const float vx = static_cast<float>(vertex[0]);
      const float vy = static_cast<float>(vertex[1]);
      const float vz = static_cast<float>(vertex[2]);
      if (vx < min_x) min_x = vx;
      if (vx > max_x) max_x = vx;
      if (vy < min_y) min_y = vy;
      if (vy > max_y) max_y = vy;
      if (vz < min_z) min_z = vz;
      if (vz > max_z) max_z = vz;
    }
    if (min_x <= max_x) {
      chunk.min_bound = QVector3D(min_x, min_y, min_z);
      chunk.max_bound = QVector3D(max_x, max_y, max_z);
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
