#include "src/ui/widgets/geo_viewer.h"

#include <cmath>

#include "src/core/coordinate_util.h"
#include "src/logic/spatial_grid_index.h"

int GeoViewerWidget::AddRoutingPath(const std::vector<odr::LaneKey>& path,
                                    const QString& /*name*/) {
  if (!map_ || path.empty() || !gl_renderer_) return -1;
  auto* routing_buf_mgr = gl_renderer_->GetRoutingBufferManager();
  if (!routing_buf_mgr) return -1;
  makeCurrent();
  const int id = routing_buf_mgr->Add(path, map_, right_hand_traffic_);
  doneCurrent();
  update();
  return id;
}

void GeoViewerWidget::RemoveRoutingPath(int id) {
  if (!gl_renderer_) return;
  auto* routing_buf_mgr = gl_renderer_->GetRoutingBufferManager();
  if (!routing_buf_mgr) return;
  makeCurrent();
  routing_buf_mgr->Remove(id);
  doneCurrent();
  update();
}

void GeoViewerWidget::SetRoutingPathVisible(int id, bool visible) {
  if (!gl_renderer_) return;
  auto* routing_buf_mgr = gl_renderer_->GetRoutingBufferManager();
  if (routing_buf_mgr) {
    routing_buf_mgr->SetVisible(id, visible);
    update();
  }
}

void GeoViewerWidget::ClearRoutingPaths() {
  if (!gl_renderer_) return;
  auto* routing_buf_mgr = gl_renderer_->GetRoutingBufferManager();
  if (!routing_buf_mgr) return;
  makeCurrent();
  routing_buf_mgr->Clear();
  doneCurrent();
  update();
}

void GeoViewerWidget::RendererToLocalCoord(const QVector3D& renderer_pos,
                                           double& local_x, double& local_y,
                                           double& local_z) const {
  local_x = renderer_pos.x();
  local_y = renderer_pos.z();
  if (right_hand_traffic_) local_y = -local_y;
  local_z = renderer_pos.y();
}

bool GeoViewerWidget::LocalToWGS84(double local_x, double local_y,
                                   double local_z, double& lon, double& lat,
                                   double& alt) const {
  try {
    double x = local_x, y = local_y;
    CoordinateUtil::Instance().LocalToWGS84(&x, &y, nullptr);
    lon = x;
    lat = y;
    alt = local_z;
    return true;
  } catch (...) {
    return false;
  }
}

bool GeoViewerWidget::GetWorldPosAt(int x, int y, QVector3D& world_pos,
                                    std::optional<PickResult>& picked_idx) {
  if (!gl_renderer_) return false;
  picked_idx = GetPickedVertexIndex(x, y);

  const QSize viewport = gl_renderer_->GetViewportSize();
  const float mouse_x = (2.0f * x) / viewport.width() - 1.0f;
  const float mouse_y = 1.0f - (2.0f * y) / viewport.height();

  const QMatrix4x4 inv_mvp =
      (gl_renderer_->GetProjectionMatrix() * GetViewMatrix()).inverted();
  QVector4D ray_origin = inv_mvp * QVector4D(mouse_x, mouse_y, -1.0f, 1.0f);
  QVector4D ray_end = inv_mvp * QVector4D(mouse_x, mouse_y, 1.0f, 1.0f);
  ray_origin /= ray_origin.w();
  ray_end /= ray_end.w();

  const QVector3D origin(ray_origin.x(), ray_origin.y(), ray_origin.z());
  const QVector3D direction = (ray_end - ray_origin).toVector3D().normalized();

  if (picked_idx.has_value()) {
    world_pos = origin + direction * closest_t_;
    return true;
  }

  if (std::abs(direction.y()) > 1e-6f) {
    const float t = -origin.y() / direction.y();
    if (t > 0) {
      world_pos = origin + direction * t;
      return true;
    }
  }

  return false;
}
