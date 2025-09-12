#include "src/app/routing_buffer_manager.h"

#include <algorithm>
#include <cmath>

RoutingBufferManager::RoutingBufferManager(QOpenGLExtraFunctions* gl)
    : gl_(gl) {}

RoutingBufferManager::~RoutingBufferManager() { Clear(); }

int RoutingBufferManager::Add(const std::vector<odr::LaneKey>& path,
                              const std::shared_ptr<odr::OpenDriveMap>& map,
                              bool right_hand_traffic) {
  if (!map || path.empty()) return -1;

  const int id = next_id_++;
  RouteRenderData& route = routes_[id];
  route.path = path;
  route.visible = true;

  BuildBuffers(id, map, right_hand_traffic);
  return id;
}

void RoutingBufferManager::Remove(int id) {
  auto it = routes_.find(id);
  if (it == routes_.end()) return;
  FreeRoute(it->second);
  routes_.erase(it);
}

void RoutingBufferManager::SetVisible(int id, bool visible) {
  auto it = routes_.find(id);
  if (it != routes_.end()) {
    it->second.visible = visible;
  }
}

void RoutingBufferManager::Clear() {
  for (auto& [id, route] : routes_) {
    FreeRoute(route);
  }
  routes_.clear();
}

void RoutingBufferManager::FreeRoute(RouteRenderData& route) {
  if (route.vao) {
    gl_->glDeleteVertexArrays(1, &route.vao);
    gl_->glDeleteBuffers(1, &route.vbo);
    gl_->glDeleteBuffers(1, &route.ebo);
    route.vao = 0;
    route.vbo = 0;
    route.ebo = 0;
  }
}

void RoutingBufferManager::BuildBuffers(
    int id, const std::shared_ptr<odr::OpenDriveMap>& map,
    bool right_hand_traffic) {
  auto it = routes_.find(id);
  if (it == routes_.end()) return;
  auto& route = it->second;

  std::vector<float> vertices;
  std::vector<uint32_t> indices;

  constexpr float kRouteWidth = 1.0f;
  constexpr float kHalfWidth = kRouteWidth * 0.5f;

  for (const auto& key : route.path) {
    if (!map->id_to_road.count(key.road_id)) continue;
    const auto& road = map->id_to_road.at(key.road_id);
    const auto& section = road.get_lanesection(key.lanesection_s0);
    if (!section.id_to_lane.count(key.lane_id)) continue;

    const double s_start = section.s0;
    const double s_end = road.get_lanesection_end(key.lanesection_s0);
    const int num_samples =
        std::max(2, static_cast<int>((s_end - s_start) / 1.0));

    const size_t baseIdx = vertices.size() / 3;

    for (int i = 0; i <= num_samples; ++i) {
      const double s = s_start + (s_end - s_start) * i / num_samples;
      const auto& lane = section.id_to_lane.at(key.lane_id);
      const double lane_w = lane.lane_width.get(s);
      const double t_outer = lane.outer_border.get(s);
      const double t_center = t_outer - (key.lane_id > 0 ? 0.5 : -0.5) * lane_w;

      const odr::Vec3D pt = road.get_xyz(s, t_center, 0);
      const odr::Vec3D grad = road.ref_line.get_grad(s);
      double norm = std::sqrt(grad[0] * grad[0] + grad[1] * grad[1]);
      if (norm < 1e-6) norm = 1.0;
      const odr::Vec3D side = {-grad[1] / norm, grad[0] / norm, 0};

      double ry = pt[1];
      double sy0 = side[0] * kHalfWidth;
      double sy1 = side[1] * kHalfWidth;
      if (right_hand_traffic) {
        ry = -ry;
        sy1 = -sy1;
      }

      vertices.push_back(static_cast<float>(pt[0] + sy0));
      vertices.push_back(static_cast<float>(pt[2]));
      vertices.push_back(static_cast<float>(ry + sy1));

      vertices.push_back(static_cast<float>(pt[0] - sy0));
      vertices.push_back(static_cast<float>(pt[2]));
      vertices.push_back(static_cast<float>(ry - sy1));

      if (i > 0) {
        const uint32_t curr = static_cast<uint32_t>(baseIdx + i * 2);
        const uint32_t prev = static_cast<uint32_t>(baseIdx + (i - 1) * 2);
        indices.push_back(prev);
        indices.push_back(prev + 1);
        indices.push_back(curr);

        indices.push_back(curr);
        indices.push_back(prev + 1);
        indices.push_back(curr + 1);
      }
    }
  }

  if (!route.vao) {
    gl_->glGenVertexArrays(1, &route.vao);
    gl_->glGenBuffers(1, &route.vbo);
    gl_->glGenBuffers(1, &route.ebo);
  }

  gl_->glBindVertexArray(route.vao);

  gl_->glBindBuffer(GL_ARRAY_BUFFER, route.vbo);
  gl_->glBufferData(GL_ARRAY_BUFFER,
                    static_cast<GLsizeiptr>(vertices.size() * sizeof(float)),
                    vertices.data(), GL_STATIC_DRAW);

  gl_->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, route.ebo);
  gl_->glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                    static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)),
                    indices.data(), GL_STATIC_DRAW);

  gl_->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float),
                             nullptr);
  gl_->glEnableVertexAttribArray(0);

  route.index_count = indices.size();

  gl_->glBindVertexArray(0);
}
