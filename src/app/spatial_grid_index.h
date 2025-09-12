#pragma once

#include <QMatrix4x4>
#include <QVector3D>
#include <functional>
#include <optional>
#include <vector>
#include "src/app/scene_geometry_types.h"
#include "third_party/libOpenDRIVE/include/Mesh.h"

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

std::vector<SceneGridBox> BuildSpatialGridBoxes(
    const odr::Mesh3D& reference_mesh,
    const std::vector<SceneMeshLayerView>& layer_views, int grid_resolution);

bool RayIntersectsSceneAabb(const QVector3D& ray_origin,
                            const QVector3D& ray_dir, const QVector3D& box_min,
                            const QVector3D& box_max, float& hit_distance);

bool RayIntersectsSceneTriangle(const QVector3D& ray_origin,
                                const QVector3D& ray_dir, const QVector3D& v0,
                                const QVector3D& v1, const QVector3D& v2,
                                float& t, float& u, float& v);

std::optional<SpatialPickResult> PickFromSpatialGrid(
    const std::vector<SceneGridBox>& grid_boxes, const QVector3D& ray_origin,
    const QVector3D& ray_dir,
    const std::function<const odr::Mesh3D*(uint32_t)>& mesh_for_layer,
    const std::function<bool(uint32_t)>& is_layer_visible,
    const std::function<bool(uint32_t, uint32_t, size_t)>& is_triangle_visible);

void BuildRayFromScreenPoint(int x, int y, const QSize& viewport_size,
                             const QMatrix4x4& projection_times_view,
                             QVector3D& ray_origin, QVector3D& ray_dir);

/// @brief Cast a ray and collect ALL triangle intersection points (not just the
///        closest). Used for finding all road surface hits at a world position.
std::vector<RaycastHitPoint> RaycastAllHits(
    const std::vector<SceneGridBox>& grid_boxes, const QVector3D& ray_origin,
    const QVector3D& ray_dir,
    const std::function<const odr::Mesh3D*(uint32_t)>& mesh_for_layer,
    const std::function<bool(uint32_t)>& is_layer_visible,
    const std::function<bool(uint32_t, uint32_t, size_t)>& is_triangle_visible);
