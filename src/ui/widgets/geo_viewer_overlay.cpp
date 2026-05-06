#include "src/ui/widgets/geo_viewer.h"

#include <QPainter>

#include <algorithm>
#include <cmath>
#include <vector>

void GeoViewerWidget::GenerateRefLinePoints(
    std::shared_ptr<odr::OpenDriveMap> map, std::vector<float>& all_vertices,
    std::map<std::string, VertRange>& ranges) {
  if (!map) return;
  std::size_t estimated_floats = 0;
  for (const auto& [road_id, road] : map->id_to_road) {
    (void)road_id;
    const double road_len = road.length;
    const std::size_t segment_count =
        static_cast<std::size_t>(std::max(1.0, std::ceil(road_len / 2.0)));
    const std::size_t arrow_count =
        road_len > 10.0
            ? static_cast<std::size_t>(std::ceil((road_len - 10.0) / 20.0))
            : 0;
    estimated_floats += segment_count * 6 + arrow_count * 18;
  }
  all_vertices.reserve(all_vertices.size() + estimated_floats);

  for (const auto& [road_id, road] : map->id_to_road) {
    const size_t start_idx = all_vertices.size() / 3;
    const double road_len = road.length;

    std::vector<odr::Vec3D> points;
    constexpr double kStep = 2.0;
    for (double s = 0; s <= road_len; s += kStep) {
      points.push_back(road.get_xyz(s, 0, 0));
    }
    if (road_len > (points.size() - 1) * kStep) {
      points.push_back(road.get_xyz(road_len, 0, 0));
    }

    auto transform_point = [&](odr::Vec3D p) {
      const float rx = static_cast<float>(p[0]);
      const float ry = static_cast<float>(p[2]);
      const float rz = right_hand_traffic_ ? -static_cast<float>(p[1])
                                           : static_cast<float>(p[1]);
      return QVector3D(rx, ry, rz);
    };

    for (size_t i = 0; i + 1 < points.size(); ++i) {
      const QVector3D p1 = transform_point(points[i]);
      const QVector3D p2 = transform_point(points[i + 1]);
      all_vertices.push_back(p1.x());
      all_vertices.push_back(p1.y());
      all_vertices.push_back(p1.z());
      all_vertices.push_back(p2.x());
      all_vertices.push_back(p2.y());
      all_vertices.push_back(p2.z());
    }

    for (double s = std::min(10.0, road_len); s < road_len; s += 20.0) {
      const odr::Vec3D p = road.get_xyz(s, 0, 0);
      const odr::Vec3D d = road.ref_line.get_grad(s);
      const double norm = std::sqrt(d[0] * d[0] + d[1] * d[1]);
      if (norm <= 1e-6) continue;

      const odr::Vec2D dir = {d[0] / norm, d[1] / norm};
      const odr::Vec2D side = {-dir[1], dir[0]};

      const odr::Vec3D p1_l = {p[0] + dir[0] * 2.0, p[1] + dir[1] * 2.0, p[2]};
      const odr::Vec3D p2_l = {p[0] - dir[0] * 1.0 + side[0] * 0.8,
                               p[1] - dir[1] * 1.0 + side[1] * 0.8, p[2]};
      const odr::Vec3D p3_l = {p[0] - dir[0] * 1.0 - side[0] * 0.8,
                               p[1] - dir[1] * 1.0 - side[1] * 0.8, p[2]};

      const std::vector<QVector3D> tri = {
          transform_point(p1_l), transform_point(p2_l), transform_point(p3_l)};

      all_vertices.push_back(tri[0][0]);
      all_vertices.push_back(tri[0][1]);
      all_vertices.push_back(tri[0][2]);
      all_vertices.push_back(tri[1][0]);
      all_vertices.push_back(tri[1][1]);
      all_vertices.push_back(tri[1][2]);

      all_vertices.push_back(tri[1][0]);
      all_vertices.push_back(tri[1][1]);
      all_vertices.push_back(tri[1][2]);
      all_vertices.push_back(tri[2][0]);
      all_vertices.push_back(tri[2][1]);
      all_vertices.push_back(tri[2][2]);

      all_vertices.push_back(tri[2][0]);
      all_vertices.push_back(tri[2][1]);
      all_vertices.push_back(tri[2][2]);
      all_vertices.push_back(tri[0][0]);
      all_vertices.push_back(tri[0][1]);
      all_vertices.push_back(tri[0][2]);
    }

    ranges[road_id] = {start_idx, (all_vertices.size() / 3) - start_idx};
  }
}

QVector3D GeoViewerWidget::JunctionGroupCenter(
    const JunctionClusterGroup& group) const {
  const auto it = junction_group_centers_.find(group.group_id);
  if (it != junction_group_centers_.end()) {
    return it->second;
  }
  if (!group.incoming_box.valid) {
    return QVector3D();
  }
  const odr::Vec3D local_center{
      (group.incoming_box.min[0] + group.incoming_box.max[0]) * 0.5,
      (group.incoming_box.min[1] + group.incoming_box.max[1]) * 0.5,
      (group.incoming_box.min[2] + group.incoming_box.max[2]) * 0.5};
  return LocalToRendererPoint(local_center);
}

QVector3D GeoViewerWidget::LocalToRendererPoint(const odr::Vec3D& point) const {
  const float rx = static_cast<float>(point[0]);
  const float ry = static_cast<float>(point[2]);
  float rz = static_cast<float>(point[1]);
  if (right_hand_traffic_) {
    rz = -rz;
  }
  return QVector3D(rx, ry, rz);
}

bool GeoViewerWidget::IsJunctionVisible(const QString& group_id,
                                        const QString& junction_id) const {
  if (hidden_elements_.count(("JG:" + group_id).toStdString())) return false;
  if (!junction_id.isEmpty()) {
    if (hidden_elements_.count(
            ("J:" + group_id + ":" + junction_id).toStdString())) {
      return false;
    }
  } else {
    auto group_index_itr =
        junction_group_index_by_id_.find(group_id.toStdString());
    if (group_index_itr != junction_group_index_by_id_.end()) {
      const auto& group =
          junction_cluster_result_.groups[group_index_itr->second];
      if (!group.junction_ids.empty()) {
        bool all_children_hidden = true;
        for (const auto& jid : group.junction_ids) {
          const std::string junction_key = "J:" + group.group_id + ":" + jid;
          if (hidden_elements_.count(junction_key) == 0) {
            all_children_hidden = false;
            break;
          }
        }
        if (all_children_hidden) return false;
      }
    }
  }
  return true;
}

void GeoViewerWidget::RenderJunctionOverlay(QPainter& painter,
                                            const QMatrix4x4& view_proj) {
  if (!gl_renderer_ || !gl_renderer_->IsLayerVisible(LayerType::kJunctions))
    return;
  if (junction_cluster_result_.groups.empty()) return;

  QPen ring_pen(QColor(255, 200, 60, 220));
  ring_pen.setWidth(2);
  QPen selected_pen(QColor(80, 255, 140, 240));
  selected_pen.setWidth(3);
  painter.setFont(QFont("Menlo", 9));

  const float camera_dist = camera_.GetDistance();
  const bool show_labels = camera_dist < 1000.0f;
  const bool show_members = camera_dist < 3000.0f;

  for (const auto& group : junction_cluster_result_.groups) {
    const QString group_id = QString::fromStdString(group.group_id);
    if (!IsJunctionVisible(group_id)) continue;

    const QVector3D center = JunctionGroupCenter(group);
    const QVector4D clip_pos = view_proj * QVector4D(center, 1.0f);
    if (clip_pos.w() <= 0.0f) continue;
    const float ndc_x = clip_pos.x() / clip_pos.w();
    const float ndc_y = clip_pos.y() / clip_pos.w();
    if (ndc_x < -1.1f || ndc_x > 1.1f || ndc_y < -1.1f || ndc_y > 1.1f)
      continue;

    const int sx = static_cast<int>((ndc_x + 1.0f) * width() * 0.5f);
    const int sy = static_cast<int>((1.0f - ndc_y) * height() * 0.5f);
    const bool selected = (selected_junction_group_id_ == group_id);

    painter.setPen(selected ? selected_pen : ring_pen);
    painter.setBrush(selected ? QColor(80, 255, 140, 80)
                              : QColor(255, 200, 60, 50));
    painter.drawEllipse(QPointF(sx, sy), selected ? 9.0 : 7.0,
                        selected ? 9.0 : 7.0);

    if (show_members || selected) {
      for (const auto& junction_id_itr : group.junction_ids) {
        const QString junction_id = QString::fromStdString(junction_id_itr);
        if (!IsJunctionVisible(group_id, junction_id)) continue;
        const auto member_index_itr =
            junction_member_index_by_id_.find(junction_id_itr);
        if (member_index_itr == junction_member_index_by_id_.end()) continue;

        const auto& member =
            junction_cluster_result_.junctions[member_index_itr->second];
        if (!member.incoming_box.valid) continue;
        const odr::Vec3D local_center{
            (member.incoming_box.min[0] + member.incoming_box.max[0]) * 0.5,
            (member.incoming_box.min[1] + member.incoming_box.max[1]) * 0.5,
            (member.incoming_box.min[2] + member.incoming_box.max[2]) * 0.5};
        const QVector3D member_center = LocalToRendererPoint(local_center);
        const QVector4D member_clip = view_proj * QVector4D(member_center, 1.0f);
        if (member_clip.w() <= 0.0f) continue;
        const int msx = static_cast<int>(
            ((member_clip.x() / member_clip.w()) + 1.0f) * width() * 0.5f);
        const int msy = static_cast<int>(
            (1.0f - (member_clip.y() / member_clip.w())) * height() * 0.5f);
        const bool member_selected =
            selected && (selected_junction_id_.isEmpty() ||
                         selected_junction_id_ == junction_id);
        painter.setPen(Qt::NoPen);
        painter.setBrush(member_selected ? QColor(80, 255, 140, 240)
                                         : QColor(255, 230, 160, 220));
        painter.drawEllipse(QPointF(msx, msy), member_selected ? 4.0 : 3.0,
                            member_selected ? 4.0 : 3.0);
      }
    }

    if (show_labels || selected) {
      const QString text = QString("%1 [%2]").arg(group_id).arg(
          JunctionClusterUtil::SemanticTypeToString(group.semantic_type));
      painter.setPen(Qt::white);
      painter.drawText(sx + 10, sy - 8, text);
    }
  }
}

void GeoViewerWidget::SetMeasureMode(bool active) {
  if (!measure_ctrl_) return;
  if (measure_ctrl_->IsActive() == active) return;
  measure_ctrl_->SetActive(active);
  if (active) {
    measure_ctrl_->ClearPoints();
    setCursor(Qt::CrossCursor);
  } else {
    setCursor(Qt::ArrowCursor);
  }
  emit MeasureModeChanged(active);
  update();
}

bool GeoViewerWidget::IsMeasureMode() const {
  return measure_ctrl_ && measure_ctrl_->IsActive();
}

void GeoViewerWidget::ClearMeasure() {
  if (!measure_ctrl_) return;
  measure_ctrl_->ClearPoints();
  update();
}
