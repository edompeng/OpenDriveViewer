#include "src/app/spatial_grid_index.h"

#include <algorithm>
#include <limits>
#include <unordered_set>

std::vector<SceneGridBox> BuildSpatialGridBoxes(
    const odr::Mesh3D& reference_mesh,
    const std::vector<SceneMeshLayerView>& layer_views, int grid_resolution) {
  std::vector<SceneGridBox> grid_boxes;
  if (reference_mesh.indices.empty() || reference_mesh.vertices.empty() ||
      grid_resolution <= 0) {
    return grid_boxes;
  }

  QVector3D mesh_min(std::numeric_limits<float>::max(),
                     std::numeric_limits<float>::max(),
                     std::numeric_limits<float>::max());
  QVector3D mesh_max(std::numeric_limits<float>::lowest(),
                     std::numeric_limits<float>::lowest(),
                     std::numeric_limits<float>::lowest());

  for (const auto& vertex : reference_mesh.vertices) {
    mesh_min.setX(std::min(mesh_min.x(), static_cast<float>(vertex[0])));
    mesh_min.setY(std::min(mesh_min.y(), static_cast<float>(vertex[1])));
    mesh_min.setZ(std::min(mesh_min.z(), static_cast<float>(vertex[2])));
    mesh_max.setX(std::max(mesh_max.x(), static_cast<float>(vertex[0])));
    mesh_max.setY(std::max(mesh_max.y(), static_cast<float>(vertex[1])));
    mesh_max.setZ(std::max(mesh_max.z(), static_cast<float>(vertex[2])));
  }

  grid_boxes.resize(grid_resolution * grid_resolution);
  float step_x = (mesh_max.x() - mesh_min.x()) / grid_resolution;
  float step_z = (mesh_max.z() - mesh_min.z()) / grid_resolution;
  if (step_x < 0.0001f) step_x = 0.0001f;
  if (step_z < 0.0001f) step_z = 0.0001f;

  for (int i = 0; i < grid_resolution; ++i) {
    for (int j = 0; j < grid_resolution; ++j) {
      auto& box = grid_boxes[i * grid_resolution + j];
      box.min_bound = QVector3D(mesh_min.x() + i * step_x, mesh_min.y(),
                                mesh_min.z() + j * step_z);
      box.max_bound = QVector3D(mesh_min.x() + (i + 1) * step_x, mesh_max.y(),
                                mesh_min.z() + (j + 1) * step_z);
    }
  }

  for (const auto& layer_view : layer_views) {
    if (!layer_view.mesh) continue;
    const int triangle_count =
        static_cast<int>(layer_view.mesh->indices.size() / 3);
    if (triangle_count == 0) continue;
    for (int triangle = 0; triangle < triangle_count; ++triangle) {
      const std::size_t base = static_cast<std::size_t>(triangle) * 3;
      const uint32_t i0 = layer_view.mesh->indices[base];
      const uint32_t i1 = layer_view.mesh->indices[base + 1];
      const uint32_t i2 = layer_view.mesh->indices[base + 2];
      QVector3D v0(layer_view.mesh->vertices[i0][0],
                   layer_view.mesh->vertices[i0][1],
                   layer_view.mesh->vertices[i0][2]);
      QVector3D v1(layer_view.mesh->vertices[i1][0],
                   layer_view.mesh->vertices[i1][1],
                   layer_view.mesh->vertices[i1][2]);
      QVector3D v2(layer_view.mesh->vertices[i2][0],
                   layer_view.mesh->vertices[i2][1],
                   layer_view.mesh->vertices[i2][2]);

      const float min_x = std::min({v0.x(), v1.x(), v2.x()});
      const float max_x = std::max({v0.x(), v1.x(), v2.x()});
      const float min_z = std::min({v0.z(), v1.z(), v2.z()});
      const float max_z = std::max({v0.z(), v1.z(), v2.z()});

      const int min_col = std::max(
          0, std::min(grid_resolution - 1,
                      static_cast<int>((min_x - mesh_min.x()) / step_x)));
      const int max_col = std::max(
          0, std::min(grid_resolution - 1,
                      static_cast<int>((max_x - mesh_min.x()) / step_x)));
      const int min_row = std::max(
          0, std::min(grid_resolution - 1,
                      static_cast<int>((min_z - mesh_min.z()) / step_z)));
      const int max_row = std::max(
          0, std::min(grid_resolution - 1,
                      static_cast<int>((max_z - mesh_min.z()) / step_z)));

      const uint32_t layer_tag = layer_view.resolve_layer_tag
                                     ? layer_view.resolve_layer_tag(i0)
                                     : layer_view.default_layer_tag;
      const uint32_t encoded =
          (layer_tag << 28) | static_cast<uint32_t>(triangle);
      for (int c = min_col; c <= max_col; ++c) {
        for (int r = min_row; r <= max_row; ++r) {
          const int box_index = c * grid_resolution + r;
          grid_boxes[box_index].triangle_indices.push_back(encoded);
        }
      }
    }
  }

  return grid_boxes;
}

bool RayIntersectsSceneAabb(const QVector3D& ray_origin,
                            const QVector3D& ray_dir, const QVector3D& box_min,
                            const QVector3D& box_max, float& hit_distance) {
  QVector3D inv_dir(1.0f / (ray_dir.x() == 0 ? 1e-6f : ray_dir.x()),
                    1.0f / (ray_dir.y() == 0 ? 1e-6f : ray_dir.y()),
                    1.0f / (ray_dir.z() == 0 ? 1e-6f : ray_dir.z()));
  float t1 = (box_min.x() - ray_origin.x()) * inv_dir.x();
  float t2 = (box_max.x() - ray_origin.x()) * inv_dir.x();
  float tmin = std::min(t1, t2);
  float tmax = std::max(t1, t2);
  t1 = (box_min.y() - ray_origin.y()) * inv_dir.y();
  t2 = (box_max.y() - ray_origin.y()) * inv_dir.y();
  tmin = std::max(tmin, std::min(t1, t2));
  tmax = std::min(tmax, std::max(t1, t2));
  t1 = (box_min.z() - ray_origin.z()) * inv_dir.z();
  t2 = (box_max.z() - ray_origin.z()) * inv_dir.z();
  tmin = std::max(tmin, std::min(t1, t2));
  tmax = std::min(tmax, std::max(t1, t2));
  if (tmax >= std::max(0.0f, tmin)) {
    hit_distance = tmin;
    return true;
  }
  return false;
}

bool RayIntersectsSceneTriangle(const QVector3D& ray_origin,
                                const QVector3D& ray_dir, const QVector3D& v0,
                                const QVector3D& v1, const QVector3D& v2,
                                float& t, float& u, float& v) {
  const QVector3D edge1 = v1 - v0;
  const QVector3D edge2 = v2 - v0;
  const QVector3D h = QVector3D::crossProduct(ray_dir, edge2);
  const float a = QVector3D::dotProduct(edge1, h);
  if (a > -0.00001f && a < 0.00001f) return false;
  const float f = 1.0f / a;
  const QVector3D s = ray_origin - v0;
  u = f * QVector3D::dotProduct(s, h);
  if (u < 0.0f || u > 1.0f) return false;
  const QVector3D q = QVector3D::crossProduct(s, edge1);
  v = f * QVector3D::dotProduct(ray_dir, q);
  if (v < 0.0f || u + v > 1.0f) return false;
  t = f * QVector3D::dotProduct(edge2, q);
  return t > 0.00001f;
}

std::optional<SpatialPickResult> PickFromSpatialGrid(
    const std::vector<SceneGridBox>& grid_boxes, const QVector3D& ray_origin,
    const QVector3D& ray_dir,
    const std::function<const odr::Mesh3D*(uint32_t)>& mesh_for_layer,
    const std::function<bool(uint32_t)>& is_layer_visible,
    const std::function<bool(uint32_t, uint32_t, size_t)>&
        is_triangle_visible) {
  float closest_t = std::numeric_limits<float>::max();
  std::optional<SpatialPickResult> result;

  for (const auto& box : grid_boxes) {
    if (box.triangle_indices.empty()) continue;
    float box_hit = 0.0f;
    if (!RayIntersectsSceneAabb(ray_origin, ray_dir, box.min_bound,
                                box.max_bound, box_hit) ||
        box_hit > closest_t) {
      continue;
    }

    for (uint32_t encoded : box.triangle_indices) {
      const uint32_t layer_tag = encoded >> 28;
      if (!is_layer_visible(layer_tag)) continue;
      const uint32_t triangle_index = encoded & 0x0FFFFFFF;
      const odr::Mesh3D* mesh = mesh_for_layer(layer_tag);
      if (!mesh) continue;
      const std::size_t base = static_cast<std::size_t>(triangle_index) * 3;
      if (base + 2 >= mesh->indices.size()) continue;
      const size_t idx0 = mesh->indices[base];
      if (!is_triangle_visible(layer_tag, triangle_index, idx0)) continue;
      const size_t idx1 = mesh->indices[base + 1];
      const size_t idx2 = mesh->indices[base + 2];
      QVector3D v0(mesh->vertices[idx0][0], mesh->vertices[idx0][1],
                   mesh->vertices[idx0][2]);
      QVector3D v1(mesh->vertices[idx1][0], mesh->vertices[idx1][1],
                   mesh->vertices[idx1][2]);
      QVector3D v2(mesh->vertices[idx2][0], mesh->vertices[idx2][1],
                   mesh->vertices[idx2][2]);
      float t = 0.0f, u = 0.0f, v = 0.0f;
      if (RayIntersectsSceneTriangle(ray_origin, ray_dir, v0, v1, v2, t, u,
                                     v) &&
          t < closest_t) {
        closest_t = t;
        result = SpatialPickResult{layer_tag, idx0, t};
      }
    }
  }

  return result;
}

void BuildRayFromScreenPoint(int x, int y, const QSize& viewport_size,
                             const QMatrix4x4& projection_times_view,
                             QVector3D& ray_origin, QVector3D& ray_dir) {
  const float mouse_x = (2.0f * x) / viewport_size.width() - 1.0f;
  const float mouse_y = 1.0f - (2.0f * y) / viewport_size.height();
  QMatrix4x4 inverse = projection_times_view.inverted();
  QVector4D origin4 = inverse * QVector4D(mouse_x, mouse_y, -1.0f, 1.0f);
  QVector4D end4 = inverse * QVector4D(mouse_x, mouse_y, 1.0f, 1.0f);
  origin4 /= origin4.w();
  end4 /= end4.w();
  ray_origin = QVector3D(origin4.x(), origin4.y(), origin4.z());
  ray_dir = (end4 - origin4).toVector3D().normalized();
}

std::vector<RaycastHitPoint> RaycastAllHits(
    const std::vector<SceneGridBox>& grid_boxes, const QVector3D& ray_origin,
    const QVector3D& ray_dir,
    const std::function<const odr::Mesh3D*(uint32_t)>& mesh_for_layer,
    const std::function<bool(uint32_t)>& is_layer_visible,
    const std::function<bool(uint32_t, uint32_t, size_t)>&
        is_triangle_visible) {
  std::vector<RaycastHitPoint> hits;
  // Track already-hit triangles by (layer_tag, triangle_index) to avoid
  // duplicates from overlapping grid boxes.
  std::unordered_set<uint64_t> visited;

  for (const auto& box : grid_boxes) {
    if (box.triangle_indices.empty()) continue;
    float box_hit = 0.0f;
    if (!RayIntersectsSceneAabb(ray_origin, ray_dir, box.min_bound,
                                box.max_bound, box_hit)) {
      continue;
    }

    for (uint32_t encoded : box.triangle_indices) {
      const uint32_t layer_tag = encoded >> 28;
      if (!is_layer_visible(layer_tag)) continue;
      const uint32_t triangle_index = encoded & 0x0FFFFFFF;

      // Deduplicate: same triangle may appear in multiple grid boxes
      uint64_t key = (static_cast<uint64_t>(layer_tag) << 32) | triangle_index;
      if (visited.count(key)) continue;
      visited.insert(key);

      const odr::Mesh3D* mesh = mesh_for_layer(layer_tag);
      if (!mesh) continue;
      const std::size_t base = static_cast<std::size_t>(triangle_index) * 3;
      if (base + 2 >= mesh->indices.size()) continue;
      const size_t idx0 = mesh->indices[base];
      if (!is_triangle_visible(layer_tag, triangle_index, idx0)) continue;
      const size_t idx1 = mesh->indices[base + 1];
      const size_t idx2 = mesh->indices[base + 2];
      QVector3D v0(mesh->vertices[idx0][0], mesh->vertices[idx0][1],
                   mesh->vertices[idx0][2]);
      QVector3D v1(mesh->vertices[idx1][0], mesh->vertices[idx1][1],
                   mesh->vertices[idx1][2]);
      QVector3D v2(mesh->vertices[idx2][0], mesh->vertices[idx2][1],
                   mesh->vertices[idx2][2]);
      float t = 0.0f, u = 0.0f, v = 0.0f;
      if (RayIntersectsSceneTriangle(ray_origin, ray_dir, v0, v1, v2, t, u,
                                     v)) {
        QVector3D hit_pos = ray_origin + ray_dir * t;
        hits.push_back({hit_pos, layer_tag, idx0, t});
      }
    }
  }

  // Sort hits by distance so the caller gets them in order
  std::sort(hits.begin(), hits.end(),
            [](const RaycastHitPoint& a, const RaycastHitPoint& b) {
              return a.distance < b.distance;
            });
  return hits;
}
