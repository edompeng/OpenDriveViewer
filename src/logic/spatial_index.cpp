#include "src/logic/spatial_index.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stack>
#include <unordered_set>

struct TriTemp {
  uint32_t encoded;
  QVector3D min_b;
  QVector3D max_b;
  QVector3D centroid;
};

// Moeller-Trumbore Ray-Triangle intersection
bool RayIntersectsTriangle(const QVector3D& origin, const QVector3D& dir,
                           const QVector3D& v0, const QVector3D& v1,
                           const QVector3D& v2, float& t) {
  const QVector3D e1 = v1 - v0;
  const QVector3D e2 = v2 - v0;
  const QVector3D h = QVector3D::crossProduct(dir, e2);
  const float a = QVector3D::dotProduct(e1, h);
  if (std::abs(a) < 1e-7f) return false;
  const float f = 1.0f / a;
  const QVector3D s = origin - v0;
  const float u = f * QVector3D::dotProduct(s, h);
  if (u < 0.0f || u > 1.0f) return false;
  const QVector3D q = QVector3D::crossProduct(s, e1);
  const float v = f * QVector3D::dotProduct(dir, q);
  if (v < 0.0f || u + v > 1.0f) return false;
  t = f * QVector3D::dotProduct(e2, q);
  return t > 1e-5f;
}

// Ray-AABB intersection
bool RayIntersectsAABB(const QVector3D& origin, const QVector3D& dir,
                       const QVector3D& min_b, const QVector3D& max_b,
                       float& t_near) {
  float tmin = -std::numeric_limits<float>::infinity();
  float tmax = std::numeric_limits<float>::infinity();

  // X axis
  float invDx =
      1.0f /
      (std::abs(dir.x()) < 1e-9f ? (dir.x() < 0 ? -1e-9f : 1e-9f) : dir.x());
  float t0x = (min_b.x() - origin.x()) * invDx;
  float t1x = (max_b.x() - origin.x()) * invDx;
  if (invDx < 0.0f) std::swap(t0x, t1x);
  tmin = std::max(tmin, t0x);
  tmax = std::min(tmax, t1x);

  // Y axis
  float invDy =
      1.0f /
      (std::abs(dir.y()) < 1e-9f ? (dir.y() < 0 ? -1e-9f : 1e-9f) : dir.y());
  float t0y = (min_b.y() - origin.y()) * invDy;
  float t1y = (max_b.y() - origin.y()) * invDy;
  if (invDy < 0.0f) std::swap(t0y, t1y);
  tmin = std::max(tmin, t0y);
  tmax = std::min(tmax, t1y);

  // Z axis
  float invDz =
      1.0f /
      (std::abs(dir.z()) < 1e-9f ? (dir.z() < 0 ? -1e-9f : 1e-9f) : dir.z());
  float t0z = (min_b.z() - origin.z()) * invDz;
  float t1z = (max_b.z() - origin.z()) * invDz;
  if (invDz < 0.0f) std::swap(t0z, t1z);
  tmin = std::max(tmin, t0z);
  tmax = std::min(tmax, t1z);

  if (tmax < tmin || tmax < 0) return false;
  t_near = tmin;
  return true;
}

SpatialIndexData BuildSpatialIndex(
    const std::vector<SceneMeshLayerView>& layer_views) {
  SpatialIndexData index_data;
  std::vector<TriTemp> all_tris;

  // Collect all triangles from all layers
  for (const auto& view : layer_views) {
    if (!view.mesh) continue;
    const auto& indices = view.mesh->indices;
    const auto& verts = view.mesh->vertices;
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
      uint32_t i0 = indices[i];
      uint32_t i1 = indices[i + 1];
      uint32_t i2 = indices[i + 2];

      TriTemp t;
      uint32_t layer_tag = view.resolve_layer_tag ? view.resolve_layer_tag(i0)
                                                  : view.default_layer_tag;
      t.encoded =
          (layer_tag << 28) | (static_cast<uint32_t>(i / 3) & 0x0FFFFFFF);

      QVector3D v0(verts[i0][0], verts[i0][1], verts[i0][2]);
      QVector3D v1(verts[i1][0], verts[i1][1], verts[i1][2]);
      QVector3D v2(verts[i2][0], verts[i2][1], verts[i2][2]);

      t.min_b = QVector3D(std::min({v0.x(), v1.x(), v2.x()}),
                          std::min({v0.y(), v1.y(), v2.y()}),
                          std::min({v0.z(), v1.z(), v2.z()}));
      t.max_b = QVector3D(std::max({v0.x(), v1.x(), v2.x()}),
                          std::max({v0.y(), v1.y(), v2.y()}),
                          std::max({v0.z(), v1.z(), v2.z()}));
      t.centroid = (v0 + v1 + v2) / 3.0f;
      all_tris.push_back(t);
    }
  }

  if (all_tris.empty()) return index_data;

  // Top-down BVH build using a stack to avoid deep recursion
  struct BuildTask {
    int start, end;
    uint32_t node_idx;
  };
  std::stack<BuildTask> stack;

  index_data.nodes.emplace_back();  // Root node
  stack.push({0, (int)all_tris.size(), 0});

  while (!stack.empty()) {
    BuildTask task = stack.top();
    stack.pop();

    auto& node = index_data.nodes[task.node_idx];

    // Compute Bounding Box for this node
    QVector3D min_b(1e12f, 1e12f, 1e12f);
    QVector3D max_b(-1e12f, -1e12f, -1e12f);
    for (int i = task.start; i < task.end; ++i) {
      min_b.setX(std::min(min_b.x(), all_tris[i].min_b.x()));
      min_b.setY(std::min(min_b.y(), all_tris[i].min_b.y()));
      min_b.setZ(std::min(min_b.z(), all_tris[i].min_b.z()));
      max_b.setX(std::max(max_b.x(), all_tris[i].max_b.x()));
      max_b.setY(std::max(max_b.y(), all_tris[i].max_b.y()));
      max_b.setZ(std::max(max_b.z(), all_tris[i].max_b.z()));
    }
    node.min_bound = min_b;
    node.max_bound = max_b;

    int count = task.end - task.start;
    if (count <= 16) {  // Leaf threshold
      node.tri_start = static_cast<uint32_t>(index_data.flat_indices.size());
      node.tri_count = static_cast<uint32_t>(count);
      for (int i = task.start; i < task.end; ++i) {
        index_data.flat_indices.push_back(all_tris[i].encoded);
      }
    } else {
      // Split along longest axis
      QVector3D extent = max_b - min_b;
      int axis = 0;
      if (extent.y() > extent.x()) axis = 1;
      if (extent.z() > (axis == 0 ? extent.x() : extent.y())) axis = 2;

      int mid = task.start + count / 2;
      std::nth_element(all_tris.begin() + task.start, all_tris.begin() + mid,
                       all_tris.begin() + task.end,
                       [axis](const TriTemp& a, const TriTemp& b) {
                         if (axis == 0) return a.centroid.x() < b.centroid.x();
                         if (axis == 1) return a.centroid.y() < b.centroid.y();
                         return a.centroid.z() < b.centroid.z();
                       });

      node.tri_count = 0;
      uint32_t left_idx = static_cast<uint32_t>(index_data.nodes.size());
      index_data.nodes.emplace_back();  // Placeholder for left child
      uint32_t right_idx = static_cast<uint32_t>(index_data.nodes.size());
      index_data.nodes.emplace_back();  // Placeholder for right child

      // Re-fetch node reference because emplace_back might have invalidated it
      index_data.nodes[task.node_idx].left_child = left_idx;
      index_data.nodes[task.node_idx].right_child = right_idx;

      stack.push({mid, task.end, right_idx});
      stack.push({task.start, mid, left_idx});
    }
  }

  index_data.is_ready = true;
  return index_data;
}

std::optional<SpatialPickResult> PickFromSpatialIndex(
    const SpatialIndexData& index_data, const QVector3D& ray_origin,
    const QVector3D& ray_dir,
    const std::function<const odr::Mesh3D*(uint32_t)>& mesh_for_layer,
    const std::function<bool(uint32_t)>& is_layer_visible,
    const std::function<bool(uint32_t, uint32_t, size_t)>&
        is_triangle_visible) {
  if (!index_data.is_ready || index_data.nodes.empty()) return std::nullopt;

  float closest_t = std::numeric_limits<float>::max();
  std::optional<SpatialPickResult> result;

  struct StackNode {
    uint32_t node_idx;
    float t_near;
  };
  std::stack<StackNode> stack;

  float t_root = 0;
  if (RayIntersectsAABB(ray_origin, ray_dir, index_data.nodes[0].min_bound,
                        index_data.nodes[0].max_bound, t_root)) {
    stack.push({0, t_root});
  }

  while (!stack.empty()) {
    StackNode curr = stack.top();
    stack.pop();

    if (curr.t_near >= closest_t) continue;

    const auto& node = index_data.nodes[curr.node_idx];
    if (node.tri_count > 0) {
      // Leaf: test triangles
      for (uint32_t i = 0; i < node.tri_count; ++i) {
        uint32_t encoded = index_data.flat_indices[node.tri_start + i];
        uint32_t layer_tag = encoded >> 28;
        if (!is_layer_visible(layer_tag)) continue;

        uint32_t tri_idx = encoded & 0x0FFFFFFF;
        const odr::Mesh3D* mesh = mesh_for_layer(layer_tag);
        if (!mesh) continue;

        size_t base = static_cast<size_t>(tri_idx) * 3;
        size_t v0_idx = mesh->indices[base];
        if (!is_triangle_visible(layer_tag, tri_idx, v0_idx)) continue;

        QVector3D v0(mesh->vertices[v0_idx][0], mesh->vertices[v0_idx][1],
                     mesh->vertices[v0_idx][2]);
        QVector3D v1(mesh->vertices[mesh->indices[base + 1]][0],
                     mesh->vertices[mesh->indices[base + 1]][1],
                     mesh->vertices[mesh->indices[base + 1]][2]);
        QVector3D v2(mesh->vertices[mesh->indices[base + 2]][0],
                     mesh->vertices[mesh->indices[base + 2]][1],
                     mesh->vertices[mesh->indices[base + 2]][2]);

        float t = 0;
        if (RayIntersectsTriangle(ray_origin, ray_dir, v0, v1, v2, t)) {
          if (t < closest_t) {
            closest_t = t;
            result = {layer_tag, v0_idx, t};
          }
        }
      }
    } else {
      // Internal: visit children
      float t_left = 0, t_right = 0;
      bool hit_left = RayIntersectsAABB(
          ray_origin, ray_dir, index_data.nodes[node.left_child].min_bound,
          index_data.nodes[node.left_child].max_bound, t_left);
      bool hit_right = RayIntersectsAABB(
          ray_origin, ray_dir, index_data.nodes[node.right_child].min_bound,
          index_data.nodes[node.right_child].max_bound, t_right);

      if (hit_left && hit_right) {
        // Visit closer child first
        if (t_left < t_right) {
          stack.push({node.right_child, t_right});
          stack.push({node.left_child, t_left});
        } else {
          stack.push({node.left_child, t_left});
          stack.push({node.right_child, t_right});
        }
      } else if (hit_left) {
        stack.push({node.left_child, t_left});
      } else if (hit_right) {
        stack.push({node.right_child, t_right});
      }
    }
  }

  return result;
}

std::vector<RaycastHitPoint> RaycastAllHits(
    const SpatialIndexData& index_data, const QVector3D& ray_origin,
    const QVector3D& ray_dir,
    const std::function<const odr::Mesh3D*(uint32_t)>& mesh_for_layer,
    const std::function<bool(uint32_t)>& is_layer_visible,
    const std::function<bool(uint32_t, uint32_t, size_t)>&
        is_triangle_visible) {
  std::vector<RaycastHitPoint> hits;
  if (!index_data.is_ready || index_data.nodes.empty()) return hits;

  std::stack<uint32_t> stack;
  stack.push(0);

  std::unordered_set<uint32_t> visited_tris;

  while (!stack.empty()) {
    uint32_t node_idx = stack.top();
    stack.pop();

    float t_near = 0;
    if (!RayIntersectsAABB(ray_origin, ray_dir,
                           index_data.nodes[node_idx].min_bound,
                           index_data.nodes[node_idx].max_bound, t_near))
      continue;

    const auto& node = index_data.nodes[node_idx];
    if (node.tri_count > 0) {
      for (uint32_t i = 0; i < node.tri_count; ++i) {
        uint32_t encoded = index_data.flat_indices[node.tri_start + i];
        if (!visited_tris.insert(encoded).second) continue;

        uint32_t layer_tag = encoded >> 28;
        if (!is_layer_visible(layer_tag)) continue;

        uint32_t tri_idx = encoded & 0x0FFFFFFF;
        const odr::Mesh3D* mesh = mesh_for_layer(layer_tag);
        if (!mesh) continue;

        size_t base = static_cast<size_t>(tri_idx) * 3;
        size_t v0_idx = mesh->indices[base];
        if (!is_triangle_visible(layer_tag, tri_idx, v0_idx)) continue;

        QVector3D v0(mesh->vertices[v0_idx][0], mesh->vertices[v0_idx][1],
                     mesh->vertices[v0_idx][2]);
        QVector3D v1(mesh->vertices[mesh->indices[base + 1]][0],
                     mesh->vertices[mesh->indices[base + 1]][1],
                     mesh->vertices[mesh->indices[base + 1]][2]);
        QVector3D v2(mesh->vertices[mesh->indices[base + 2]][0],
                     mesh->vertices[mesh->indices[base + 2]][1],
                     mesh->vertices[mesh->indices[base + 2]][2]);

        float t = 0;
        if (RayIntersectsTriangle(ray_origin, ray_dir, v0, v1, v2, t)) {
          hits.push_back({ray_origin + ray_dir * t, layer_tag, v0_idx, t});
        }
      }
    } else {
      stack.push(node.right_child);
      stack.push(node.left_child);
    }
  }

  std::sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) {
    return a.distance < b.distance;
  });
  return hits;
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
