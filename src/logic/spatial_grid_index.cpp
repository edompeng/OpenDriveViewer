#include "src/logic/spatial_grid_index.h"

#include <algorithm>
#include <cmath>
#include <future>
#include <limits>
#include <unordered_set>
#include "src/core/thread_pool.h"

SpatialGridData BuildSpatialGridBoxes(
    const odr::Mesh3D& reference_mesh,
    const std::vector<SceneMeshLayerView>& layer_views, int grid_resolution) {
  SpatialGridData grid_data;
  const int kMaxGridResolution = 512;
  int resolved_resolution = std::min(grid_resolution, kMaxGridResolution);
  if (reference_mesh.indices.empty() || reference_mesh.vertices.empty() ||
      resolved_resolution <= 0) {
    return grid_data;
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

  grid_data.boxes.resize(resolved_resolution * resolved_resolution);
  float step_x = (mesh_max.x() - mesh_min.x()) / resolved_resolution;
  float step_z = (mesh_max.z() - mesh_min.z()) / resolved_resolution;
  if (step_x < 0.0001f) step_x = 0.0001f;
  if (step_z < 0.0001f) step_z = 0.0001f;

  for (int i = 0; i < resolved_resolution; ++i) {
    for (int j = 0; j < resolved_resolution; ++j) {
      auto& box = grid_data.boxes[i * resolved_resolution + j];
      box.min_bound = QVector3D(mesh_min.x() + i * step_x, mesh_min.y(),
                                mesh_min.z() + j * step_z);
      box.max_bound = QVector3D(mesh_min.x() + (i + 1) * step_x, mesh_max.y(),
                                mesh_min.z() + (j + 1) * step_z);
    }
  }

  // Use a temporary vector of vectors to collect indices, then flatten
  std::vector<std::vector<uint32_t>> temp_boxes(grid_data.boxes.size());

  // For large maps, we can parallelize this by layer or even by triangle chunks
  auto& pool = geoviewer::utility::ThreadPool::Instance();
  std::vector<std::future<void>> futures;

  std::vector<std::unique_ptr<std::mutex>> row_mutexes;
  for (int i = 0; i < resolved_resolution; ++i) {
    row_mutexes.push_back(std::make_unique<std::mutex>());
  }

  for (const auto& layer_view : layer_views) {
    if (!layer_view.mesh) continue;

    futures.push_back(pool.Enqueue([&, layer_view]() {
      const int triangle_count =
          static_cast<int>(layer_view.mesh->indices.size() / 3);
      if (triangle_count == 0) return;

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
            0, std::min(resolved_resolution - 1,
                        static_cast<int>((min_x - mesh_min.x()) / step_x)));
        const int max_col = std::max(
            0, std::min(resolved_resolution - 1,
                        static_cast<int>((max_x - mesh_min.x()) / step_x)));
        const int min_row = std::max(
            0, std::min(resolved_resolution - 1,
                        static_cast<int>((min_z - mesh_min.z()) / step_z)));
        const int max_row = std::max(
            0, std::min(resolved_resolution - 1,
                        static_cast<int>((max_z - mesh_min.z()) / step_z)));

        const uint32_t layer_tag = layer_view.resolve_layer_tag
                                       ? layer_view.resolve_layer_tag(i0)
                                       : layer_view.default_layer_tag;
        const uint32_t encoded =
            (layer_tag << 28) | static_cast<uint32_t>(triangle);

        for (int c = min_col; c <= max_col; ++c) {
          for (int r = min_row; r <= max_row; ++r) {
            const int box_index = c * resolved_resolution + r;
            std::lock_guard<std::mutex> lock(*row_mutexes[r]);
            temp_boxes[box_index].push_back(encoded);
          }
        }
      }
    }));
  }

  for (auto& f : futures) f.get();

  // Flattening
  size_t total_indices = 0;
  for (const auto& tb : temp_boxes) total_indices += tb.size();
  grid_data.flat_indices.reserve(total_indices);

  for (size_t i = 0; i < grid_data.boxes.size(); ++i) {
    grid_data.boxes[i].index_offset =
        static_cast<uint32_t>(grid_data.flat_indices.size());
    grid_data.boxes[i].index_count =
        static_cast<uint32_t>(temp_boxes[i].size());
    grid_data.flat_indices.insert(grid_data.flat_indices.end(),
                                  temp_boxes[i].begin(), temp_boxes[i].end());
  }

  return grid_data;
}

bool RayIntersectsSceneAabb(const QVector3D& ray_origin,
                            const QVector3D& ray_dir, const QVector3D& box_min,
                            const QVector3D& box_max, float& hit_distance) {
  const float kEpsilon = 1e-8f;
  QVector3D inv_dir(1.0f / (std::abs(ray_dir.x()) < kEpsilon
                                ? (ray_dir.x() < 0 ? -kEpsilon : kEpsilon)
                                : ray_dir.x()),
                    1.0f / (std::abs(ray_dir.y()) < kEpsilon
                                ? (ray_dir.y() < 0 ? -kEpsilon : kEpsilon)
                                : ray_dir.y()),
                    1.0f / (std::abs(ray_dir.z()) < kEpsilon
                                ? (ray_dir.z() < 0 ? -kEpsilon : kEpsilon)
                                : ray_dir.z()));
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
    const SpatialGridData& grid_data, const QVector3D& ray_origin,
    const QVector3D& ray_dir,
    const std::function<const odr::Mesh3D*(uint32_t)>& mesh_for_layer,
    const std::function<bool(uint32_t)>& is_layer_visible,
    const std::function<bool(uint32_t, uint32_t, size_t)>&
        is_triangle_visible) {
  if (grid_data.boxes.empty()) return std::nullopt;

  const int resolution = static_cast<int>(std::sqrt(grid_data.boxes.size()));
  const QVector3D g_min = grid_data.boxes[0].min_bound;
  const QVector3D g_max = grid_data.boxes.back().max_bound;
  const float step_x = (g_max.x() - g_min.x()) / resolution;
  const float step_z = (g_max.z() - g_min.z()) / resolution;

  float t_min = -1e9f, t_max = 1e9f;
  auto intersect_axis = [&](float origin, float dir, float min_b, float max_b) {
    if (std::abs(dir) < 1e-8f) {
      if (origin < min_b || origin > max_b) return false;
    } else {
      float t1 = (min_b - origin) / dir;
      float t2 = (max_b - origin) / dir;
      t_min = std::max(t_min, std::min(t1, t2));
      t_max = std::min(t_max, std::max(t1, t2));
    }
    return true;
  };

  if (!intersect_axis(ray_origin.x(), ray_dir.x(), g_min.x(), g_max.x()) ||
      !intersect_axis(ray_origin.z(), ray_dir.z(), g_min.z(), g_max.z()) ||
      t_min > t_max || t_max < 0) {
    return std::nullopt;
  }

  float start_t = std::max(0.0f, t_min);
  QVector3D p = ray_origin + ray_dir * start_t;

  int curr_x = std::clamp(static_cast<int>((p.x() - g_min.x()) / step_x), 0,
                          resolution - 1);
  int curr_z = std::clamp(static_cast<int>((p.z() - g_min.z()) / step_z), 0,
                          resolution - 1);

  const int step_xi = (ray_dir.x() > 0) ? 1 : -1;
  const int step_zi = (ray_dir.z() > 0) ? 1 : -1;

  const float t_delta_x =
      std::abs(step_x / (std::abs(ray_dir.x()) > 1e-8f ? ray_dir.x() : 1e-8f));
  const float t_delta_z =
      std::abs(step_z / (std::abs(ray_dir.z()) > 1e-8f ? ray_dir.z() : 1e-8f));

  float t_next_x =
      (ray_dir.x() > 0)
          ? (g_min.x() + (curr_x + 1) * step_x - ray_origin.x()) / ray_dir.x()
          : (g_min.x() + curr_x * step_x - ray_origin.x()) / ray_dir.x();
  float t_next_z =
      (ray_dir.z() > 0)
          ? (g_min.z() + (curr_z + 1) * step_z - ray_origin.z()) / ray_dir.z()
          : (g_min.z() + curr_z * step_z - ray_origin.z()) / ray_dir.z();

  if (std::abs(ray_dir.x()) < 1e-8f) t_next_x = 1e9f;
  if (std::abs(ray_dir.z()) < 1e-8f) t_next_z = 1e9f;

  float closest_t = std::numeric_limits<float>::max();
  std::optional<SpatialPickResult> result;

  while (curr_x >= 0 && curr_x < resolution && curr_z >= 0 &&
         curr_z < resolution) {
    const auto& box = grid_data.boxes[curr_x * resolution + curr_z];
    if (box.index_count > 0) {
      for (uint32_t k = 0; k < box.index_count; ++k) {
        uint32_t encoded = grid_data.flat_indices[box.index_offset + k];
        const uint32_t layer_tag = encoded >> 28;
        if (!is_layer_visible(layer_tag)) continue;
        const uint32_t triangle_index = encoded & 0x0FFFFFFF;
        const odr::Mesh3D* mesh = mesh_for_layer(layer_tag);
        if (!mesh) continue;
        const std::size_t base = static_cast<std::size_t>(triangle_index) * 3;
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
        float t = 0, u = 0, v = 0;
        if (RayIntersectsSceneTriangle(ray_origin, ray_dir, v0, v1, v2, t, u,
                                       v) &&
            t < closest_t) {
          closest_t = t;
          result = SpatialPickResult{layer_tag, idx0, t};
        }
      }
      if (result.has_value()) {
        float t_exit = std::min(t_next_x, t_next_z);
        if (closest_t <= t_exit) return result;
      }
    }

    if (t_next_x < t_next_z) {
      t_next_x += t_delta_x;
      curr_x += step_xi;
    } else {
      t_next_z += t_delta_z;
      curr_z += step_zi;
    }
    if (result.has_value() && std::min(t_next_x, t_next_z) > closest_t) break;
    if (std::min(t_next_x, t_next_z) > t_max) break;
  }

  return result;
}

void BuildRayFromScreenPoint(int x, int y, const QSize& viewport_size,
                             const QMatrix4x4& projection_times_view,
                             QVector3D& ray_origin, QVector3D& ray_dir) {
  float nx = (2.0f * x) / viewport_size.width() - 1.0f;
  float ny = 1.0f - (2.0f * y) / viewport_size.height();
  QMatrix4x4 inv_pv = projection_times_view.inverted();
  QVector4D near_pt = inv_pv * QVector4D(nx, ny, -1.0f, 1.0f);
  QVector4D far_pt = inv_pv * QVector4D(nx, ny, 1.0f, 1.0f);
  ray_origin = QVector3D(near_pt / near_pt.w());
  ray_dir = (QVector3D(far_pt / far_pt.w()) - ray_origin).normalized();
}

std::vector<RaycastHitPoint> RaycastAllHits(
    const SpatialGridData& grid_data, const QVector3D& ray_origin,
    const QVector3D& ray_dir,
    const std::function<const odr::Mesh3D*(uint32_t)>& mesh_for_layer,
    const std::function<bool(uint32_t)>& is_layer_visible,
    const std::function<bool(uint32_t, uint32_t, size_t)>&
        is_triangle_visible) {
  std::vector<RaycastHitPoint> hits;
  if (grid_data.boxes.empty()) return hits;

  const int resolution = static_cast<int>(std::sqrt(grid_data.boxes.size()));
  const QVector3D g_min = grid_data.boxes[0].min_bound;
  const QVector3D g_max = grid_data.boxes.back().max_bound;
  const float step_x = (g_max.x() - g_min.x()) / resolution;
  const float step_z = (g_max.z() - g_min.z()) / resolution;

  float t_min = -1e9f, t_max = 1e9f;
  auto intersect_axis = [&](float origin, float dir, float min_b, float max_b) {
    if (std::abs(dir) < 1e-8f) {
      if (origin < min_b || origin > max_b) return false;
    } else {
      float t1 = (min_b - origin) / dir;
      float t2 = (max_b - origin) / dir;
      t_min = std::max(t_min, std::min(t1, t2));
      t_max = std::min(t_max, std::max(t1, t2));
    }
    return true;
  };

  if (!intersect_axis(ray_origin.x(), ray_dir.x(), g_min.x(), g_max.x()) ||
      !intersect_axis(ray_origin.z(), ray_dir.z(), g_min.z(), g_max.z()) ||
      t_min > t_max || t_max < 0) {
    return hits;
  }

  float start_t = std::max(0.0f, t_min);
  QVector3D p = ray_origin + ray_dir * start_t;
  int curr_x = std::clamp(static_cast<int>((p.x() - g_min.x()) / step_x), 0,
                          resolution - 1);
  int curr_z = std::clamp(static_cast<int>((p.z() - g_min.z()) / step_z), 0,
                          resolution - 1);

  const int step_xi = (ray_dir.x() > 0) ? 1 : -1;
  const int step_zi = (ray_dir.z() > 0) ? 1 : -1;
  const float t_delta_x =
      std::abs(step_x / (std::abs(ray_dir.x()) > 1e-8f ? ray_dir.x() : 1e-8f));
  const float t_delta_z =
      std::abs(step_z / (std::abs(ray_dir.z()) > 1e-8f ? ray_dir.z() : 1e-8f));

  float t_next_x =
      (ray_dir.x() > 0)
          ? (g_min.x() + (curr_x + 1) * step_x - ray_origin.x()) / ray_dir.x()
          : (g_min.x() + curr_x * step_x - ray_origin.x()) / ray_dir.x();
  float t_next_z =
      (ray_dir.z() > 0)
          ? (g_min.z() + (curr_z + 1) * step_z - ray_origin.z()) / ray_dir.z()
          : (g_min.z() + curr_z * step_z - ray_origin.z()) / ray_dir.z();

  if (std::abs(ray_dir.x()) < 1e-8f) t_next_x = 1e9f;
  if (std::abs(ray_dir.z()) < 1e-8f) t_next_z = 1e9f;

  std::unordered_set<uint64_t> visited_triangles;

  while (curr_x >= 0 && curr_x < resolution && curr_z >= 0 &&
         curr_z < resolution) {
    const auto& box = grid_data.boxes[curr_x * resolution + curr_z];
    for (uint32_t k = 0; k < box.index_count; ++k) {
      uint32_t encoded = grid_data.flat_indices[box.index_offset + k];
      if (visited_triangles.count(encoded)) continue;
      visited_triangles.insert(encoded);

      const uint32_t layer_tag = encoded >> 28;
      if (!is_layer_visible(layer_tag)) continue;
      const uint32_t triangle_index = encoded & 0x0FFFFFFF;
      const odr::Mesh3D* mesh = mesh_for_layer(layer_tag);
      if (!mesh) continue;
      const std::size_t base = static_cast<std::size_t>(triangle_index) * 3;
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
      float t = 0, u = 0, v = 0;
      if (RayIntersectsSceneTriangle(ray_origin, ray_dir, v0, v1, v2, t, u,
                                     v)) {
        hits.push_back({ray_origin + ray_dir * t, layer_tag, idx0, t});
      }
    }

    if (t_next_x < t_next_z) {
      t_next_x += t_delta_x;
      curr_x += step_xi;
    } else {
      t_next_z += t_delta_z;
      curr_z += step_zi;
    }
    if (std::min(t_next_x, t_next_z) > t_max) break;
  }

  std::sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) {
    return a.distance < b.distance;
  });
  return hits;
}
