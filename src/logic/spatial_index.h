#pragma once

#include <QMatrix4x4>
#include <QVector3D>
#include <functional>
#include <optional>
#include <vector>
#include "Mesh.h"
#include "src/core/scene_geometry_types.h"

struct SceneMeshLayerView {
  const odr::Mesh3D* mesh = nullptr;
  uint32_t default_layer_tag = 0;
  std::function<uint32_t(uint32_t)> resolve_layer_tag;
};

struct SpatialPickResult {
  uint32_t layer_tag = 0;
  size_t vertex_index = 0;
  float distance = 0.0f;
};

/// @brief A single raycast hit point with its 3D position
struct RaycastHitPoint {
  QVector3D position;
  uint32_t layer_tag = 0;
  size_t vertex_index = 0;
  float distance = 0.0f;
};

struct BVHNode {
  QVector3D min_bound;
  QVector3D max_bound;
  uint32_t left_child = 0;
  uint32_t right_child = 0;
  uint32_t tri_start = 0;
  uint32_t tri_count = 0;  // 0 if not a leaf
};

struct SpatialIndexData {
  std::vector<BVHNode> nodes;
  std::vector<uint32_t> flat_indices;
  bool is_ready = false;
};

SpatialIndexData BuildSpatialIndex(
    const std::vector<SceneMeshLayerView>& layer_views);

std::optional<SpatialPickResult> PickFromSpatialIndex(
    const SpatialIndexData& index_data, const QVector3D& ray_origin,
    const QVector3D& ray_dir,
    const std::function<const odr::Mesh3D*(uint32_t)>& mesh_for_layer,
    const std::function<bool(uint32_t)>& is_layer_visible,
    const std::function<bool(uint32_t, uint32_t, size_t)>& is_triangle_visible);

std::vector<RaycastHitPoint> RaycastAllHits(
    const SpatialIndexData& index_data, const QVector3D& ray_origin,
    const QVector3D& ray_dir,
    const std::function<const odr::Mesh3D*(uint32_t)>& mesh_for_layer,
    const std::function<bool(uint32_t)>& is_layer_visible,
    const std::function<bool(uint32_t, uint32_t, size_t)>& is_triangle_visible);

void BuildRayFromScreenPoint(int x, int y, const QSize& viewport_size,
                             const QMatrix4x4& projection_times_view,
                             QVector3D& ray_origin, QVector3D& ray_dir);
