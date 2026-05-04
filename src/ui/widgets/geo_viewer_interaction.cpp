#include "src/ui/widgets/geo_viewer.h"

#include <QDebug>
#include <QPainter>

#include "src/core/thread_pool.h"
#include "src/logic/spatial_grid_index.h"

void GeoViewerWidget::initializeGL() {
  gl_renderer_ = std::make_unique<geoviewer::render::GlRenderer>();
  if (!gl_renderer_->Initialize()) {
    qCritical() << "Failed to initialize GlRenderer";
    return;
  }

  // Apply cached visibility
  for (const auto& [type, visible] : layer_visibility_cache_) {
    gl_renderer_->SetLayerVisible(type, visible);
  }

  measure_ctrl_ = std::make_unique<MeasureToolController>(this);
  connect(measure_ctrl_.get(), &MeasureToolController::pointsChanged, this,
          &GeoViewerWidget::UpdateMeasureBuffers);
  connect(measure_ctrl_.get(), &MeasureToolController::TotalDistanceChanged,
          this, &GeoViewerWidget::TotalDistanceChanged);
  connect(measure_ctrl_.get(), &MeasureToolController::activeChanged, this,
          &GeoViewerWidget::MeasureModeChanged);
}

void GeoViewerWidget::resizeGL(int w, int h) {
  if (gl_renderer_) {
    gl_renderer_->Resize(w, h);
  }
}

void GeoViewerWidget::paintGL() {
  if (!gl_renderer_) return;

  if (needs_index_update_ && batch_update_count_ == 0) {
    UpdateMeshIndices();
  }

  if (!mesh_updated_) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    return;
  }

  QMatrix4x4 view = GetViewMatrix();
  const float distance = camera_.GetDistance();
  const float mesh_radius = camera_.MeshRadius();

  // Prepare user point data for drawing (positions + color override)
  std::vector<std::pair<QVector3D, bool>> user_points_draw_data;
  user_points_draw_data.reserve(user_points_.size());
  for (const auto& p : user_points_) {
    user_points_draw_data.push_back({p.color, p.visible});
  }

  size_t measure_ptr_count = measure_ctrl_ ? measure_ctrl_->Points().size() : 0;

  // The renderer handles all OpenGL draw calls
  gl_renderer_->RenderScene(view, distance, mesh_radius, user_points_draw_data,
                            measure_ptr_count, QVector3D(0.0f, 1.0f, 0.5f),
                            0.8f);

  // QPainter overlays (Labels, UI elements)
  const QMatrix4x4 view_proj = gl_renderer_->GetProjectionMatrix() * view;
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);

  if (measure_ptr_count > 0) {
    painter.setPen(Qt::yellow);
    QFont font = painter.font();
    font.setBold(true);
    font.setPointSize(10);
    painter.setFont(font);

    const auto& pts = measure_ctrl_->Points();
    for (size_t i = 1; i < pts.size(); ++i) {
      const QVector3D p1 = pts[i - 1];
      const QVector3D p2 = pts[i];
      const float dist = p1.distanceToPoint(p2);
      const QVector3D mid = (p1 + p2) * 0.5f;

      const QVector4D clip_pos = view_proj * QVector4D(mid, 1.0f);
      if (clip_pos.w() > 0) {
        const float ndc_x = clip_pos.x() / clip_pos.w();
        const float ndc_y = clip_pos.y() / clip_pos.w();
        const int sx = (int)((ndc_x + 1.0f) * width() * 0.5f);
        const int sy = (int)((1.0f - ndc_y) * height() * 0.5f);
        painter.drawText(sx + 5, sy - 5, QString("%1m").arg(dist, 0, 'f', 2));
      }
    }
  }

  RenderJunctionOverlay(painter, view_proj);
  painter.end();
}

void GeoViewerWidget::mousePressEvent(QMouseEvent* ev) {
  if (measure_ctrl_ && measure_ctrl_->IsActive()) {
    if (ev->button() == Qt::LeftButton) {
      QVector3D world_pos;
      std::optional<PickResult> picked_idx;
      if (GetWorldPosAt((int)ev->position().x(), (int)ev->position().y(),
                        world_pos, picked_idx)) {
        if (picked_idx.has_value() && picked_idx->layer == LayerType::kLanes) {
          measure_ctrl_->AddPoint(world_pos);
          update();
        }
      }
      return;
    } else if (ev->button() == Qt::RightButton) {
      SetMeasureMode(false);
      return;
    }
  }

  camera_.BeginDrag(ev->position().toPoint(), ev->button());

  if (ev->button() == Qt::LeftButton) {
    HandlePickSelection((int)ev->position().x(), (int)ev->position().y(), false);
  }
  ev->accept();
}

void GeoViewerWidget::mouseReleaseEvent(QMouseEvent* ev) {
  camera_.EndDrag();
  ev->accept();
}

void GeoViewerWidget::mouseDoubleClickEvent(QMouseEvent* ev) {
  if (ev->button() == Qt::LeftButton) {
    HandlePickSelection((int)ev->position().x(), (int)ev->position().y(), true);
  }
  ev->accept();
}

void GeoViewerWidget::HandlePickSelection(int x, int y, bool is_double_click) {
  QVector3D world_pos;
  std::optional<PickResult> picked_idx;
  if (GetWorldPosAt(x, y, world_pos, picked_idx) && picked_idx.has_value() &&
      map_) {
    QString road_id, element_id;
    TreeNodeType node_type = TreeNodeType::kRoad;
    size_t vi = picked_idx->vertex_index;

    if (picked_idx->layer == LayerType::kLanes) {
      road_id =
          QString::fromStdString(network_mesh_->lanes_mesh.get_road_id(vi));
      double s0 = network_mesh_->lanes_mesh.get_lanesec_s0(vi);
      int lane_id = network_mesh_->lanes_mesh.get_lane_id(vi);
      element_id = QString("%1:%2").arg(s0).arg(lane_id);
      node_type = TreeNodeType::kLane;
    } else if (picked_idx->layer == LayerType::kObjects ||
               picked_idx->layer == LayerType::kFacilities) {
      if (picked_idx->layer == LayerType::kObjects) {
        road_id = QString::fromStdString(
            network_mesh_->road_objects_mesh.get_road_id(vi));
        element_id = QString::fromStdString(
            network_mesh_->road_objects_mesh.get_road_object_id(vi));
      } else if (facility_mesh_) {
        for (const auto& el : facility_element_items_) {
          bool found = false;
          for (const auto& range : el.ranges) {
            for (uint32_t k = 0; k < range.count * 3; ++k) {
              if (facility_mesh_->indices[range.start * 3 + k] ==
                  static_cast<uint32_t>(vi)) {
                found = true;
                break;
              }
            }
            if (found) break;
          }
          if (found) {
            QStringList parts =
                QString::fromStdString(el.element_key).split(":");
            if (parts.size() >= 4) {
              road_id = parts[1];
              element_id = parts[3];
            }
            break;
          }
        }
      }
      node_type = TreeNodeType::kObject;
    } else if (picked_idx->layer == LayerType::kSignalLights) {
      element_id = QString::fromStdString(
          network_mesh_->road_signals_mesh.get_road_signal_id(vi));
      const std::string signal_id = element_id.toStdString();
      road_id = QString::fromStdString(GetRoadIdBySignalId(signal_id));
      node_type = TreeNodeType::kLight;
    } else if (picked_idx->layer == LayerType::kSignalSigns) {
      element_id = QString::fromStdString(
          network_mesh_->road_signals_mesh.get_road_signal_id(vi));
      const std::string signal_id = element_id.toStdString();
      road_id = QString::fromStdString(GetRoadIdBySignalId(signal_id));
      node_type = TreeNodeType::kSign;
    } else if (picked_idx->layer == LayerType::kJunctions) {
      auto group_id = FindJunctionGroupByVertex(vi);
      if (group_id.has_value()) {
        road_id = QString::fromStdString(*group_id);
        element_id = QString::fromStdString(*group_id);
        node_type = TreeNodeType::kJunctionGroup;
      }
    }

    if (!road_id.isEmpty()) {
      emit ElementSelected(road_id, node_type, element_id);
    }
    if (is_double_click) {
      JumpToLocalLocation(world_pos.x(), world_pos.y(), world_pos.z());
    }
  }
}

void GeoViewerWidget::mouseMoveEvent(QMouseEvent* ev) {
  const QPoint currentPos = ev->position().toPoint();
  Qt::MouseButton active_button = Qt::NoButton;
  if (ev->buttons() & Qt::RightButton) {
    active_button = Qt::RightButton;
  } else if (ev->buttons() & Qt::LeftButton) {
    active_button = Qt::LeftButton;
  }

  if (active_button != camera_.PressedButton()) {
    if (active_button == Qt::NoButton) {
      camera_.EndDrag();
      UpdateHoverInfo((int)ev->position().x(), (int)ev->position().y());
    } else {
      camera_.BeginDrag(currentPos, active_button);
    }
    ev->accept();
    return;
  }

  const QPoint delta = currentPos - camera_.LastPos();

  if (camera_.PressedButton() == Qt::LeftButton) {
    camera_.PanByDelta(delta);
    update();
  } else if (camera_.PressedButton() == Qt::RightButton) {
    camera_.OrbitByDelta(delta);
    update();
  } else {
    UpdateHoverInfo((int)ev->position().x(), (int)ev->position().y());
  }
  // Update last position for next delta
  camera_.BeginDrag(currentPos, active_button);
  ev->accept();
}

void GeoViewerWidget::wheelEvent(QWheelEvent* ev) {
  const QPoint numDegrees = ev->angleDelta();
  if (!numDegrees.isNull()) {
    const float wheelDelta = static_cast<float>(numDegrees.y()) / 120.0f;
    const float maxDist = qMax(camera_.MeshRadius() * 100.0f, 50000000.0f);

    QVector3D world_pos;
    std::optional<PickResult> picked_idx;
    const bool hasPick =
        GetWorldPosAt((int)ev->position().x(), (int)ev->position().y(),
                      world_pos, picked_idx);

    camera_.ZoomToward(wheelDelta, maxDist, world_pos, hasPick);
    update();
  }
  ev->accept();
}

bool GeoViewerWidget::event(QEvent* ev) {
  if (ev->type() == QEvent::NativeGesture) {
    auto* gesture_ev = static_cast<QNativeGestureEvent*>(ev);
    switch (gesture_ev->gestureType()) {
      case Qt::PanNativeGesture: {
        const QPointF delta = gesture_ev->delta();
        const QPoint delta_pixels(static_cast<int>(delta.x()),
                                  static_cast<int>(delta.y()));
        if (gesture_ev->modifiers().testFlag(Qt::ShiftModifier)) {
          camera_.PanByDelta(delta_pixels);
        } else {
          camera_.OrbitByDelta(delta_pixels);
        }
        update();
        ev->accept();
        return true;
      }
      case Qt::RotateNativeGesture: {
        // Native rotate value is an incremental degree delta.
        const int dx = static_cast<int>(gesture_ev->value() / 0.3f);
        camera_.OrbitByDelta(QPoint(dx, 0));
        update();
        ev->accept();
        return true;
      }
      case Qt::ZoomNativeGesture: {
        const float wheel_delta = static_cast<float>(gesture_ev->value() * 8.0);
        const float max_dist = qMax(camera_.MeshRadius() * 100.0f, 50000000.0f);

        QVector3D world_pos;
        std::optional<PickResult> picked_idx;
        const QPointF pos = gesture_ev->position();
        const bool has_pick =
            GetWorldPosAt(static_cast<int>(pos.x()), static_cast<int>(pos.y()),
                          world_pos, picked_idx);
        camera_.ZoomToward(wheel_delta, max_dist, world_pos, has_pick);
        update();
        ev->accept();
        return true;
      }
      default:
        break;
    }
  }
  return QOpenGLWidget::event(ev);
}

void GeoViewerWidget::focusOutEvent(QFocusEvent* event) {
  camera_.EndDrag();
  QWidget::focusOutEvent(event);
}

// These methods are now handled by geoviewer::render::GlRenderer
void GeoViewerWidget::ClearHighlight() {
  if (!gl_renderer_) return;
  auto* highlight_mgr = gl_renderer_->GetHighlightManager();
  if (highlight_mgr && (highlight_mgr->HasHighlight() ||
                        highlight_mgr->HasNeighborHighlight())) {
    highlight_mgr->Clear();
    update();
  }
}

void GeoViewerWidget::CalculateMeshCenter() {
  if (!network_mesh_ || network_mesh_->lanes_mesh.vertices.empty()) return;
  auto& network_mesh = *network_mesh_;

  QVector3D minVec(std::numeric_limits<float>::max(),
                   std::numeric_limits<float>::max(),
                   std::numeric_limits<float>::max());
  QVector3D maxVec(std::numeric_limits<float>::lowest(),
                   std::numeric_limits<float>::lowest(),
                   std::numeric_limits<float>::lowest());

  for (size_t i = 0; i < network_mesh.lanes_mesh.vertices.size(); i++) {
    const float x = network_mesh.lanes_mesh.vertices[i][0];
    const float y = network_mesh.lanes_mesh.vertices[i][1];
    const float z = network_mesh.lanes_mesh.vertices[i][2];
    minVec.setX(qMin(minVec.x(), x));
    minVec.setY(qMin(minVec.y(), y));
    minVec.setZ(qMin(minVec.z(), z));
    maxVec.setX(qMax(maxVec.x(), x));
    maxVec.setY(qMax(maxVec.y(), y));
    maxVec.setZ(qMax(maxVec.z(), z));
  }
  // Delegate to CameraController for scene fitting
  camera_.FitToScene(minVec, maxVec);
}

void GeoViewerWidget::StartSpatialGridBuild() {
  if (!map_ || !network_mesh_ || !junction_mesh_) return;
  const std::uint64_t generation = ++spatial_grid_generation_;
  auto map = map_;
  auto network_mesh = network_mesh_;
  auto junction_mesh = junction_mesh_;
  const int grid_resolution = grid_resolution_;

  geoviewer::utility::ThreadPool::Instance().Enqueue(
      [this, map, network_mesh, junction_mesh, grid_resolution, generation]() {
        auto result = BuildSpatialGridData(map, *network_mesh, *junction_mesh,
                                           grid_resolution);
        QMetaObject::invokeMethod(
            this, [this, res = std::move(result), generation]() mutable {
              if (generation == spatial_grid_generation_.load()) {
                grid_boxes_ = std::move(res);
                spatial_grid_ready_ = true;
                update();
              }
            });
      });
}

void GeoViewerWidget::BuildSpatialGrid() {
  grid_boxes_ = BuildSpatialGridData(map_, *network_mesh_, *junction_mesh_,
                                     grid_resolution_);
  spatial_grid_ready_ = true;
}

std::vector<SceneGridBox> GeoViewerWidget::BuildSpatialGridData(
    std::shared_ptr<odr::OpenDriveMap> map,
    const odr::RoadNetworkMesh& network_mesh, const odr::Mesh3D& junction_mesh,
    int grid_resolution) const {
  return BuildSpatialGridBoxes(
      network_mesh.lanes_mesh,
      {
          SceneMeshLayerView{&network_mesh.lanes_mesh,
                             static_cast<uint32_t>(LayerType::kLanes),
                             {}},
          SceneMeshLayerView{&network_mesh.roadmarks_mesh,
                             static_cast<uint32_t>(LayerType::kRoadmarks),
                             {}},
          SceneMeshLayerView{&network_mesh.road_objects_mesh,
                             static_cast<uint32_t>(LayerType::kObjects),
                             {}},
          SceneMeshLayerView{
              &junction_mesh, static_cast<uint32_t>(LayerType::kJunctions), {}},
          SceneMeshLayerView{
              &network_mesh.road_signals_mesh,
              static_cast<uint32_t>(LayerType::kSignalLights),
              [this, map, &network_mesh](uint32_t vertex_index) {
                std::string signal_id =
                    network_mesh.road_signals_mesh.get_road_signal_id(
                        vertex_index);
                std::string road_id = GetRoadIdBySignalId(signal_id);
                bool is_light = false;
                if (map && map->id_to_road.count(road_id)) {
                  const auto& road = map->id_to_road.at(road_id);
                  if (road.id_to_signal.count(signal_id)) {
                    is_light = (road.id_to_signal.at(signal_id).name ==
                                "TrafficLight");
                  }
                }
                return static_cast<uint32_t>(is_light
                                                 ? LayerType::kSignalLights
                                                 : LayerType::kSignalSigns);
              }},
          SceneMeshLayerView{facility_mesh_.get(),
                             static_cast<uint32_t>(LayerType::kFacilities),
                             {}},
      },
      grid_resolution);
}

std::optional<GeoViewerWidget::PickResult>
GeoViewerWidget::GetPickedVertexIndex(int x, int y) {
  if (!gl_renderer_ || !network_mesh_ ||
      network_mesh_->lanes_mesh.vertices.empty() || !spatial_grid_ready_) {
    return std::nullopt;
  }
  QVector3D origin;
  QVector3D direction;
  BuildRayFromScreenPoint(x, y, gl_renderer_->GetViewportSize(),
                          gl_renderer_->GetProjectionMatrix() * GetViewMatrix(),
                          origin, direction);

  const auto result = PickFromSpatialGrid(
      grid_boxes_, origin, direction,
      [this](uint32_t layer_tag) {
        return MeshForLayer(static_cast<LayerType>(layer_tag));
      },
      [this](uint32_t layer_tag) {
        return IsLayerVisible(static_cast<LayerType>(layer_tag));
      },
      [this](uint32_t layer_tag, uint32_t triangle_index, size_t vertex_index) {
        return IsTrianglePickVisible(static_cast<LayerType>(layer_tag),
                                     triangle_index, vertex_index);
      });

  if (!result.has_value()) {
    return std::nullopt;
  }

  closest_t_ = result->distance;
  return PickResult{static_cast<LayerType>(result->layer_tag),
                    result->vertex_index};
}
