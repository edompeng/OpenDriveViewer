#include "src/ui/widgets/geo_viewer.h"
#include <QContextMenuEvent>
#include <QDebug>
#include <QFileInfo>
#include <QFuture>
#include <QMatrix4x4>
#include <QPainter>
#include <QPolygonF>
#include <QRegularExpression>
#include <QStringList>
#include <algorithm>
#include <future>
#include "src/logic/scene_index_builder.h"
#include "src/logic/spatial_grid_index.h"
#include "src/core/coordinate_util.h"
#include "src/core/thread_pool.h"
#include "src/core/viewer_text_util.h"

GeoViewerWidget::GeoViewerWidget(QWidget* parent)
    : QOpenGLWidget(parent), right_hand_traffic_(true) {
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);

  // Initialize visibility cache with defaults
  for (int i = 0; i < static_cast<int>(LayerType::kCount); ++i) {
    layer_visibility_cache_[static_cast<LayerType>(i)] = true;
  }
  // Junctions and Signal Signs are hidden by default in UI
  layer_visibility_cache_[LayerType::kJunctions] = false;
  layer_visibility_cache_[LayerType::kSignalSigns] = false;
  layer_visibility_cache_[LayerType::kObjects] = false;
}

GeoViewerWidget::~GeoViewerWidget() {
  spatial_grid_generation_++;
  makeCurrent();
  gl_renderer_.reset();
  doneCurrent();
}

void GeoViewerWidget::CommitUserPointsChange(bool buffer_dirty) {
  if (user_points_batch_depth_ > 0) {
    user_points_batch_dirty_ = true;
    if (buffer_dirty) {
      user_points_batch_buffer_dirty_ = true;
    }
    return;
  }

  if (buffer_dirty) {
    UpdateUserPointsBuffers();
  }
  update();
  emit UserPointsChanged();
}

void GeoViewerWidget::BeginUserPointsBatch() { ++user_points_batch_depth_; }

void GeoViewerWidget::EndUserPointsBatch() {
  if (user_points_batch_depth_ <= 0) return;
  --user_points_batch_depth_;
  if (user_points_batch_depth_ != 0 || !user_points_batch_dirty_) return;

  if (user_points_batch_buffer_dirty_) {
    UpdateUserPointsBuffers();
  }
  user_points_batch_dirty_ = false;
  user_points_batch_buffer_dirty_ = false;
  update();
  emit UserPointsChanged();
}

void GeoViewerWidget::SetLayerVisible(LayerType type, bool visible) {
  if (type >= LayerType::kLanes && type < LayerType::kCount) {
    layer_visibility_cache_[type] = visible;
    if (gl_renderer_) {
      auto* highlight_mgr = gl_renderer_->GetHighlightManager();
      if (!visible && highlight_mgr && highlight_mgr->cur_layer == type) {
        ClearHighlight();
      }
      gl_renderer_->SetLayerVisible(type, visible);
      update();
    }
  }
}

bool GeoViewerWidget::IsLayerVisible(LayerType type) const {
  if (!gl_renderer_) {
    auto it = layer_visibility_cache_.find(type);
    return (it != layer_visibility_cache_.end()) ? it->second : false;
  }
  return gl_renderer_->IsLayerVisible(type);
}

void GeoViewerWidget::SetElementVisible(const QString& id, bool visible) {
  std::string sid = id.toStdString();
  if (visible)
    hidden_elements_.erase(sid);
  else {
    hidden_elements_.insert(sid);
    ClearHighlight();
  }
  needs_index_update_ = true;
  if (batch_update_count_ == 0) {
    UpdateMeshIndices();
  }
  emit ElementVisibilityChanged(id, visible);
}

bool GeoViewerWidget::IsElementVisible(const QString& id) const {
  return hidden_elements_.find(id.toStdString()) == hidden_elements_.end();
}

bool GeoViewerWidget::IsElementActuallyVisible(
    const std::string& road_id, const std::string& group,
    const std::string& element_id) const {
  if (hidden_elements_.count("R:" + road_id)) return false;
  if (!group.empty()) {
    if (hidden_elements_.count("G:" + road_id + ":" + group)) return false;
    std::string full_id = "E:" + road_id + ":" + group;
    if (!element_id.empty()) full_id += ":" + element_id;
    if (hidden_elements_.count(full_id)) return false;
  }
  return true;
}

void GeoViewerWidget::AddUserPoint(double lon, double lat,
                                   std::optional<double> alt) {
  if (!map_) return;
  if (!georeference_valid_) return;

  double lx, ly, lz;
  lx = lon;
  ly = lat;
  lz = alt.value_or(0.0);
  try {
    CoordinateUtil::Instance().WGS84ToLocal(&lx, &ly, &lz);
  } catch (const std::exception& e) {
    qDebug() << "AddUserPoint conversion error:" << e.what();
    return;
  }

  if (alt.has_value()) {
    // Direct placement: convert local coords to renderer coords
    // Renderer coords: X -> lx, Y -> lz, Z -> ly (mirrored if RHT)
    double ry = ly;
    if (right_hand_traffic_) ry = -ry;
    QVector3D world_pos(static_cast<float>(lx), static_cast<float>(lz),
                        static_cast<float>(ry));
    user_points_.push_back(UserPoint(world_pos, lon, lat, *alt));
  } else {
    // Raycast mode: cast a vertical ray downward at the (lx, ly) position
    // to find all lane surface intersections.
    double ry = ly;
    if (right_hand_traffic_) ry = -ry;

    const size_t points_before = user_points_.size();

    // Renderer coordinates: X=lx, Z=ry; cast along Y axis (downward)
    // Start from a high Y position (above all geometry)
    constexpr float kRayStartHeight = 10000.0f;
    QVector3D ray_origin(static_cast<float>(lx), kRayStartHeight,
                         static_cast<float>(ry));
    QVector3D ray_dir(0.0f, -1.0f, 0.0f);  // Straight down

    if (spatial_grid_ready_) {
      const auto hits = RaycastAllHits(
          grid_boxes_, ray_origin, ray_dir,
          [this](uint32_t layer_tag) {
            return MeshForLayer(static_cast<LayerType>(layer_tag));
          },
          [](uint32_t layer_tag) {
            // Only test against lane surfaces
            return static_cast<LayerType>(layer_tag) == LayerType::kLanes;
          },
          [this](uint32_t layer_tag, uint32_t triangle_index,
                 size_t vertex_index) {
            return IsTrianglePickVisible(static_cast<LayerType>(layer_tag),
                                         triangle_index, vertex_index);
          });

      for (const auto& hit : hits) {
        // Convert renderer position back to local coords for WGS84
        double hit_lx, hit_ly, hit_lz;
        RendererToLocalCoord(hit.position, hit_lx, hit_ly, hit_lz);
        double p_lon, p_lat, p_alt;
        LocalToWGS84(hit_lx, hit_ly, hit_lz, p_lon, p_lat, p_alt);
        user_points_.push_back(UserPoint(hit.position, p_lon, p_lat, p_alt));
      }
    }

    if (user_points_.size() == points_before) {
      // Fallback: place at ground level (Y=0) if no hit or grid not ready
      QVector3D world_pos(static_cast<float>(lx), 0.0f, static_cast<float>(ry));
      user_points_.push_back(UserPoint(world_pos, lon, lat, 0.0));
    }
  }

  CommitUserPointsChange(true);
}

void GeoViewerWidget::RemoveUserPoint(int index) {
  if (index < 0 || index >= static_cast<int>(user_points_.size())) return;
  user_points_.erase(user_points_.begin() + index);
  CommitUserPointsChange(true);
}

void GeoViewerWidget::AddUserPointLocal(double x, double y,
                                        std::optional<double> z) {
  if (!map_) return;
  const double local_z = z.value_or(0.0);
  double ry = y;
  if (right_hand_traffic_) ry = -ry;

  auto resolve_lonlat = [&](double local_x, double local_y, double local_alt,
                            double& out_lon, double& out_lat, double& out_alt) {
    out_lon = local_x;
    out_lat = local_y;
    out_alt = local_alt;
    if (!georeference_valid_) return;
    LocalToWGS84(local_x, local_y, local_alt, out_lon, out_lat, out_alt);
  };

  if (z.has_value()) {
    QVector3D world_pos(static_cast<float>(x), static_cast<float>(local_z),
                        static_cast<float>(ry));
    double lon = x, lat = y, alt = local_z;
    resolve_lonlat(x, y, local_z, lon, lat, alt);
    user_points_.push_back(UserPoint(world_pos, lon, lat, alt));
  } else {
    const size_t points_before = user_points_.size();
    constexpr float kRayStartHeight = 10000.0f;
    QVector3D ray_origin(static_cast<float>(x), kRayStartHeight,
                         static_cast<float>(ry));
    QVector3D ray_dir(0.0f, -1.0f, 0.0f);

    if (spatial_grid_ready_) {
      const auto hits = RaycastAllHits(
          grid_boxes_, ray_origin, ray_dir,
          [this](uint32_t layer_tag) {
            return MeshForLayer(static_cast<LayerType>(layer_tag));
          },
          [](uint32_t layer_tag) {
            return static_cast<LayerType>(layer_tag) == LayerType::kLanes;
          },
          [this](uint32_t layer_tag, uint32_t triangle_index,
                 size_t vertex_index) {
            return IsTrianglePickVisible(static_cast<LayerType>(layer_tag),
                                         triangle_index, vertex_index);
          });

      for (const auto& hit : hits) {
        double hit_lx, hit_ly, hit_lz;
        RendererToLocalCoord(hit.position, hit_lx, hit_ly, hit_lz);
        double lon = hit_lx, lat = hit_ly, alt = hit_lz;
        resolve_lonlat(hit_lx, hit_ly, hit_lz, lon, lat, alt);
        user_points_.push_back(UserPoint(hit.position, lon, lat, alt));
      }
    }

    if (user_points_.size() == points_before) {
      QVector3D world_pos(static_cast<float>(x), 0.0f, static_cast<float>(ry));
      double lon = x, lat = y, alt = 0.0;
      resolve_lonlat(x, y, 0.0, lon, lat, alt);
      user_points_.push_back(UserPoint(world_pos, lon, lat, alt));
    }
  }

  CommitUserPointsChange(true);
}

void GeoViewerWidget::SetUserPointVisible(int index, bool visible) {
  if (index < 0 || index >= static_cast<int>(user_points_.size())) return;
  if (user_points_[index].visible == visible) return;
  user_points_[index].visible = visible;
  CommitUserPointsChange(false);
}

void GeoViewerWidget::SetUserPointColor(int index, const QVector3D& color) {
  if (index < 0 || index >= static_cast<int>(user_points_.size())) return;
  user_points_[index].color = color;
  update();  // No buffer rebuild needed; color is applied via uniform
}

void GeoViewerWidget::ClearUserPoints() {
  if (user_points_.empty()) return;
  user_points_.clear();
  CommitUserPointsChange(true);
}

int GeoViewerWidget::UserPointCount() const {
  return static_cast<int>(user_points_.size());
}

GeoViewerWidget::UserPointSnapshot GeoViewerWidget::GetUserPointSnapshot(
    int index) const {
  if (index < 0 || index >= (int)user_points_.size())
    return UserPointSnapshot();
  const auto& p = user_points_[index];
  double lx, ly, lz;
  RendererToLocalCoord(p.world_pos, lx, ly, lz);
  return UserPointSnapshot(p.lon, p.lat, p.alt, lx, ly, lz, p.visible, p.color);
}

void GeoViewerWidget::UpdateUserPointsBuffers() {
  if (!gl_renderer_) return;
  makeCurrent();

  // Upload all point positions (visible or not) — visibility is handled
  // during draw calls by skipping invisible points individually.
  std::vector<float> data;
  data.reserve(user_points_.size() * 3);
  for (const auto& p : user_points_) {
    data.push_back(p.world_pos.x());
    data.push_back(p.world_pos.y());
    data.push_back(p.world_pos.z());
  }

  gl_renderer_->UploadUserPointsData(data);
  doneCurrent();
}

void GeoViewerWidget::UpdateMeasureBuffers() {
  if (!measure_ctrl_ || !gl_renderer_) return;
  const auto& points = measure_ctrl_->Points();

  makeCurrent();
  gl_renderer_->UploadMeasurePointsData(points);
  doneCurrent();
}

void GeoViewerWidget::SetRightHandTraffic(bool rht) {
  if (right_hand_traffic_ == rht) return;
  right_hand_traffic_ = rht;
  if (map_) {
    // NOTE: For a full toggle, the mesh would need re-initialization from the
    // original data. Since we are removing the UI toggle, we assume this stays
    // consistent with loading.
    update();
  }
}

void GeoViewerWidget::UpdateMeshIndices() {
  needs_index_update_ = false;
  if (!map_ || !gl_renderer_) return;
  makeCurrent();

  static constexpr int kLayerCount = static_cast<int>(LayerType::kCount);

  struct LayerTasks {
    std::vector<uint32_t> indices;
    std::vector<SceneMeshChunk> chunks;
  };
  std::array<LayerTasks, kLayerCount> layerData;

  auto collectLayerData = [&](LayerType type,
                              const std::vector<SceneCachedElement>& elements,
                              const std::vector<uint32_t>& original_indices,
                              const odr::Mesh3D* base_mesh) {
    if (!base_mesh) return;
    const SceneLayerIndexResult result = BuildSceneLayerIndex(
        elements, original_indices,
        gl_renderer_->GetLayerVertexOffset(type),
        *base_mesh, [this](const SceneCachedElement& element) {
          if (hidden_elements_.count(element.road_key)) return false;
          if (!element.group_key.empty() &&
              hidden_elements_.count(element.group_key)) {
            return false;
          }
          if (!element.element_key.empty() &&
              hidden_elements_.count(element.element_key)) {
            return false;
          }
          return true;
        });
    layerData[static_cast<int>(type)] = {result.indices, result.chunks};
  };

  auto& pool = geoviewer::utility::ThreadPool::Instance();
  std::vector<std::future<void>> futures;
  futures.push_back(pool.Enqueue([&]() {
    collectLayerData(LayerType::kLanes, lane_element_items_,
                     network_mesh_.lanes_mesh.indices,
                     &network_mesh_.lanes_mesh);
  }));
  futures.push_back(pool.Enqueue([&]() {
    collectLayerData(LayerType::kRoadmarks, roadmark_element_items_,
                     network_mesh_.roadmarks_mesh.indices,
                     &network_mesh_.roadmarks_mesh);
  }));
  futures.push_back(pool.Enqueue([&]() {
    collectLayerData(LayerType::kObjects, object_element_items_,
                     network_mesh_.road_objects_mesh.indices,
                     &network_mesh_.road_objects_mesh);
  }));
  futures.push_back(pool.Enqueue([&]() {
    // Junction groups need a specialized predicate: individual junctions use
    // "J:group_id:junction_id" keys which the generic predicate doesn't check.
    // We only hide a group's mesh if the group itself is hidden OR ALL its
    // individual junctions are hidden.
    const SceneLayerIndexResult result = BuildSceneLayerIndex(
        junction_element_items_, junction_mesh_.indices,
        gl_renderer_->GetLayerVertexOffset(LayerType::kJunctions),
        junction_mesh_,
        [this](const SceneCachedElement& element) {
          // Check group-level visibility (JG:group_id)
          if (hidden_elements_.count(element.road_key)) return false;

          // Extract group_id from road_key "JG:xxx"
          if (element.road_key.size() <= 3) return true;
          std::string group_id = element.road_key.substr(3);

          auto group_itr = junction_group_index_by_id_.find(group_id);
          if (group_itr != junction_group_index_by_id_.end()) {
            const auto& group =
                junction_cluster_result_.groups[group_itr->second];
            if (!group.junction_ids.empty()) {
              bool all_children_hidden = true;
              for (const auto& jid : group.junction_ids) {
                std::string junction_key = "J:" + group_id + ":" + jid;
                if (hidden_elements_.count(junction_key) == 0) {
                  all_children_hidden = false;
                  break;
                }
              }
              if (all_children_hidden) {
                return false;
              }
            }
          }
          return true;
        });
    layerData[static_cast<int>(LayerType::kJunctions)] = {result.indices,
                                                          result.chunks};
  }));
  futures.push_back(pool.Enqueue([&]() {
    std::vector<uint32_t> indices;
    size_t v_offset =
        gl_renderer_->GetLayerVertexOffset(LayerType::kSignalLights);
    for (const auto& el : signal_element_items_) {
      if (el.group_key.find(":light") == std::string::npos) continue;
      if (hidden_elements_.count(el.road_key) ||
          hidden_elements_.count(el.group_key) ||
          hidden_elements_.count(el.element_key))
        continue;
      for (const auto& range : el.ranges) {
        for (uint32_t k = 0; k < range.count * 3; ++k) {
          indices.push_back(
              network_mesh_.road_signals_mesh.indices[range.start * 3 + k] +
              static_cast<uint32_t>(v_offset));
        }
      }
    }
    layerData[static_cast<int>(LayerType::kSignalLights)].indices =
        std::move(indices);
  }));
  futures.push_back(pool.Enqueue([&]() {
    std::vector<uint32_t> indices;
    size_t v_offset =
        gl_renderer_->GetLayerVertexOffset(LayerType::kSignalSigns);
    for (const auto& el : signal_element_items_) {
      if (el.group_key.find(":sign") == std::string::npos) continue;
      if (hidden_elements_.count(el.road_key) ||
          hidden_elements_.count(el.group_key) ||
          hidden_elements_.count(el.element_key))
        continue;
      for (const auto& range : el.ranges) {
        for (uint32_t k = 0; k < range.count * 3; ++k) {
          indices.push_back(
              network_mesh_.road_signals_mesh.indices[range.start * 3 + k] +
              static_cast<uint32_t>(v_offset));
        }
      }
    }
    layerData[static_cast<int>(LayerType::kSignalSigns)].indices =
        std::move(indices);
  }));

  for (auto& f : futures) f.get();

  makeCurrent();
  for (int type_index = 0; type_index < kLayerCount; ++type_index) {
    auto& data = layerData[type_index];
    LayerType type = static_cast<LayerType>(type_index);

    // Fix EBO update bug: allow data upload even for empty indices for major
    // layers so they are cleared from the screen when unchecked.
    if (data.indices.empty() && data.chunks.empty()) {
      const bool is_major_layer =
          (type == LayerType::kLanes || type == LayerType::kRoadmarks ||
           type == LayerType::kObjects || type == LayerType::kJunctions ||
           type == LayerType::kSignalLights ||
           type == LayerType::kSignalSigns || type == LayerType::kLaneLines ||
           type == LayerType::kLaneLinesDashed ||
           type == LayerType::kReferenceLines);
      if (!is_major_layer) continue;
    }

    gl_renderer_->UploadLayerIndices(type, data.indices);
    gl_renderer_->SetLayerChunks(type, std::move(data.chunks));
  }

  // kLaneLines
  {
    std::vector<uint32_t> solid_indices;
    std::vector<uint32_t> dashed_indices;
    size_t v_offset =
        gl_renderer_->GetLayerVertexOffset(LayerType::kLanes);

    for (const auto& el : outline_element_items_) {
      if (hidden_elements_.count(el.road_key)) continue;
      if (hidden_elements_.count(el.group_key)) continue;
      if (hidden_elements_.count(el.element_key)) continue;

      auto& target = el.is_dashed ? dashed_indices : solid_indices;
      for (const auto& range : el.ranges) {
        for (uint32_t k = 0; k < range.count * 2; ++k) {
          target.push_back(
              static_cast<uint32_t>(lane_outline_indices_[range.start * 2 + k]) +
              static_cast<uint32_t>(v_offset));
        }
      }
    }

    auto SetupLineLayer = [&](LayerType t,
                              const std::vector<uint32_t>& indices) {
      gl_renderer_->SetLayerChunks(
          t, BuildSceneMeshChunks(
                 indices, gl_renderer_->GetLayerVertexOffset(t),
                 network_mesh_.lanes_mesh));
      gl_renderer_->UploadLayerIndices(t, indices);
    };
    SetupLineLayer(LayerType::kLaneLines, solid_indices);
    SetupLineLayer(LayerType::kLaneLinesDashed, dashed_indices);
  }

  // Reference Lines
  {
    std::vector<uint32_t> ref_line_indices;
    bool layer_visible = gl_renderer_->IsLayerVisible(LayerType::kReferenceLines);
    for (const auto& [road_id, road] : map_->id_to_road) {
      if (layer_visible && IsElementActuallyVisible(road_id, "refline", "")) {
        if (road_ref_line_vert_ranges_.count(road_id)) {
          const auto& range = road_ref_line_vert_ranges_.at(road_id);
          for (size_t i = 0; i < range.count; ++i) {
            ref_line_indices.push_back(static_cast<uint32_t>(range.start + i));
          }
        }
      }
    }

    gl_renderer_->UploadLayerIndices(LayerType::kReferenceLines,
                                     ref_line_indices);
    gl_renderer_->SetLayerChunks(LayerType::kReferenceLines, {});
  }

  doneCurrent();
  update();
}

QMatrix4x4 GeoViewerWidget::GetViewMatrix() const {
  return camera_.GetViewMatrix();
}

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
    QVector3D world_pos;
    std::optional<PickResult> picked_idx;
    if (GetWorldPosAt((int)ev->position().x(), (int)ev->position().y(),
                      world_pos, picked_idx) &&
        picked_idx.has_value() && map_) {
      QString road_id, element_id;
      TreeNodeType node_type = TreeNodeType::kRoad;
      size_t vi = picked_idx->vertex_index;

      if (picked_idx->layer == LayerType::kLanes) {
        road_id =
            QString::fromStdString(network_mesh_.lanes_mesh.get_road_id(vi));
        double s0 = network_mesh_.lanes_mesh.get_lanesec_s0(vi);
        int lane_id = network_mesh_.lanes_mesh.get_lane_id(vi);
        element_id = QString("%1:%2").arg(s0).arg(lane_id);
        node_type = TreeNodeType::kLane;
      } else if (picked_idx->layer == LayerType::kObjects) {
        road_id = QString::fromStdString(
            network_mesh_.road_objects_mesh.get_road_id(vi));
        element_id = QString::fromStdString(
            network_mesh_.road_objects_mesh.get_road_object_id(vi));
        node_type = TreeNodeType::kObject;
      } else if (picked_idx->layer == LayerType::kSignalLights) {
        road_id = QString::fromStdString(
            network_mesh_.road_signals_mesh.get_road_id(vi));
        element_id = QString::fromStdString(
            network_mesh_.road_signals_mesh.get_road_signal_id(vi));
        node_type = TreeNodeType::kLight;
      } else if (picked_idx->layer == LayerType::kSignalSigns) {
        road_id = QString::fromStdString(
            network_mesh_.road_signals_mesh.get_road_id(vi));
        element_id = QString::fromStdString(
            network_mesh_.road_signals_mesh.get_road_signal_id(vi));
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
    }
  }
  ev->accept();
}

void GeoViewerWidget::mouseReleaseEvent(QMouseEvent* ev) {
  camera_.EndDrag();
  ev->accept();
}

void GeoViewerWidget::mouseMoveEvent(QMouseEvent* ev) {
  const QPoint currentPos = ev->position().toPoint();
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
  camera_.BeginDrag(currentPos, camera_.PressedButton());
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
  if (network_mesh_.lanes_mesh.vertices.empty()) return;

  QVector3D minVec(std::numeric_limits<float>::max(),
                   std::numeric_limits<float>::max(),
                   std::numeric_limits<float>::max());
  QVector3D maxVec(std::numeric_limits<float>::lowest(),
                   std::numeric_limits<float>::lowest(),
                   std::numeric_limits<float>::lowest());

  for (size_t i = 0; i < network_mesh_.lanes_mesh.vertices.size(); i++) {
    const float x = network_mesh_.lanes_mesh.vertices[i][0];
    const float y = network_mesh_.lanes_mesh.vertices[i][1];
    const float z = network_mesh_.lanes_mesh.vertices[i][2];
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

// RayIntersectsTriangle and RayIntersectsAABB migrated to
// spatial_grid_index.cpp as free functions, no longer members of
// GeoViewerWidget

// RayIntersectsTriangle and RayIntersectsAABB migrated to
// spatial_grid_index.cpp as free functions, no longer members of
// GeoViewerWidget

void GeoViewerWidget::StartSpatialGridBuild() {
  const std::uint64_t generation = ++spatial_grid_generation_;
  auto map = map_;
  const odr::RoadNetworkMesh* network_mesh = &network_mesh_;
  const odr::Mesh3D* junction_mesh = &junction_mesh_;
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
  grid_boxes_ = BuildSpatialGridData(map_, network_mesh_, junction_mesh_,
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
              [map, &network_mesh](uint32_t vertex_index) {
                std::string road_id =
                    network_mesh.road_signals_mesh.get_road_id(vertex_index);
                std::string signal_id =
                    network_mesh.road_signals_mesh.get_road_signal_id(
                        vertex_index);
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
      },
      grid_resolution);
}

std::optional<GeoViewerWidget::PickResult>
GeoViewerWidget::GetPickedVertexIndex(int x, int y) {
  if (!gl_renderer_ || network_mesh_.lanes_mesh.vertices.empty() ||
      !spatial_grid_ready_) {
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

int GeoViewerWidget::AddRoutingPath(const std::vector<odr::LaneKey>& path,
                                    const QString& name) {
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

// UpdateRoutingBuffers() migrated to RoutingBufferManager::BuildBuffers()

void GeoViewerWidget::RendererToLocalCoord(const QVector3D& renderer_pos,
                                           double& local_x, double& local_y,
                                           double& local_z) const {
  local_x = renderer_pos.x();
  local_y = renderer_pos.z();
  if (right_hand_traffic_) local_y = -local_y;  // Reverse mirroring
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

  QSize viewport = gl_renderer_->GetViewportSize();
  float mouse_x = (2.0f * x) / viewport.width() - 1.0f;
  float mouse_y = 1.0f - (2.0f * y) / viewport.height();

  QMatrix4x4 inv_mvp =
      (gl_renderer_->GetProjectionMatrix() * GetViewMatrix()).inverted();
  QVector4D ray_origin = inv_mvp * QVector4D(mouse_x, mouse_y, -1.0f, 1.0f);
  QVector4D ray_end = inv_mvp * QVector4D(mouse_x, mouse_y, 1.0f, 1.0f);
  ray_origin /= ray_origin.w();
  ray_end /= ray_end.w();

  QVector3D origin(ray_origin.x(), ray_origin.y(), ray_origin.z());
  QVector3D direction = (ray_end - ray_origin).toVector3D().normalized();

  if (picked_idx.has_value()) {
    world_pos = origin + direction * closest_t_;
    return true;
  }

  // Fallback: if no object picked, intersect with ground plane (Renderer Y=0
  // coordinate system)
  if (std::abs(direction.y()) > 1e-6f) {
    float t = -origin.y() / direction.y();
    if (t > 0) {
      world_pos = origin + direction * t;
      return true;
    }
  }

  return false;
}

void GeoViewerWidget::contextMenuEvent(QContextMenuEvent* ev) {
  camera_.EndDrag();
  QVector3D world_pos;
  std::optional<PickResult> picked_idx;
  if (!GetWorldPosAt(ev->pos().x(), ev->pos().y(), world_pos, picked_idx)) {
    return;
  }

  double local_x, local_y, local_z;
  RendererToLocalCoord(world_pos, local_x, local_y, local_z);

  double lon = local_x, lat = local_y, alt = local_z;
  const bool has_wgs84 = georeference_valid_ &&
                         LocalToWGS84(local_x, local_y, local_z, lon, lat, alt);
  QString coord_text;
  if (coord_mode_ == CoordinateMode::kWGS84 && has_wgs84) {
    coord_text = QString("%1,%2,%3")
                     .arg(lon, 0, 'f', 8)
                     .arg(lat, 0, 'f', 8)
                     .arg(alt, 0, 'f', 2);
  } else {
    coord_text = QString("%1,%2,%3")
                     .arg(local_x, 0, 'f', 3)
                     .arg(local_y, 0, 'f', 3)
                     .arg(local_z, 0, 'f', 3);
  }
  QString info_text;

  if (picked_idx.has_value() && map_) {
    size_t vi = picked_idx->vertex_index;
    if (picked_idx->layer == LayerType::kLanes) {
      std::string road_id = network_mesh_.lanes_mesh.get_road_id(vi);
      if (map_->id_to_road.count(road_id)) {
        info_text =
            QString("%1/%2/%3")
                .arg(road_id.c_str())
                .arg(network_mesh_.lanes_mesh.get_lanesec_s0(vi), 0, 'f', 2)
                .arg(network_mesh_.lanes_mesh.get_lane_id(vi));
      }
    } else if (picked_idx->layer == LayerType::kRoadmarks) {
      std::string road_id = network_mesh_.roadmarks_mesh.get_road_id(vi);
      if (map_->id_to_road.count(road_id)) {
        const auto& r = map_->id_to_road.at(road_id);
        std::string road_mark_type =
            network_mesh_.roadmarks_mesh.get_roadmark_type(vi);
        info_text = QString("Roadmark %1 in kRoad %2 (Name: %3)")
                        .arg(road_mark_type.c_str())
                        .arg(road_id.c_str())
                        .arg(r.name.c_str());
      }
    } else if (picked_idx->layer == LayerType::kObjects) {
      std::string road_id = network_mesh_.road_objects_mesh.get_road_id(vi);
      if (map_->id_to_road.count(road_id)) {
        const auto& r = map_->id_to_road.at(road_id);
        std::string object_id =
            network_mesh_.road_objects_mesh.get_road_object_id(vi);
        if (r.id_to_object.count(object_id)) {
          const auto& obj = r.id_to_object.at(object_id);
          info_text = QString("kObject %1 (Name: %2, Type: %3) in kRoad %4")
                          .arg(object_id.c_str())
                          .arg(obj.name.c_str())
                          .arg(obj.type.c_str())
                          .arg(road_id.c_str());
        }
      }
    } else if (picked_idx->layer == LayerType::kSignalLights ||
               picked_idx->layer == LayerType::kSignalSigns) {
      std::string road_id = network_mesh_.road_signals_mesh.get_road_id(vi);
      if (map_->id_to_road.count(road_id)) {
        const auto& r = map_->id_to_road.at(road_id);
        std::string sId =
            network_mesh_.road_signals_mesh.get_road_signal_id(vi);
        if (r.id_to_signal.count(sId)) {
          const auto& sig = r.id_to_signal.at(sId);
          info_text = QString("Signal %1 (Name: %2, Type: %3) in kRoad %4")
                          .arg(sId.c_str())
                          .arg(sig.name.c_str())
                          .arg(sig.type.c_str())
                          .arg(road_id.c_str());
        }
      }
    }
  }

  QMenu menu(this);
  QAction* copy_coord =
      menu.addAction(tr("📋 Copy coordinate: %1").arg(coord_text));
  QAction* copyInfo = nullptr;
  QAction* copyAll = nullptr;
  if (!info_text.isEmpty()) {
    copyInfo = menu.addAction(tr("🏷️ Copy info: %1").arg(info_text));
    copyAll = menu.addAction(tr("📋 Copy all"));
  }

  QAction* hideElement = menu.addAction(tr("👁️ Hide current object"));
  QAction* addFav = menu.addAction(tr("⭐ Add to favorites"));

  QAction* setStartRouting = nullptr;
  QAction* setEndRouting = nullptr;
  if (picked_idx && picked_idx->layer == LayerType::kLanes) {
    menu.addSeparator();
    setStartRouting = menu.addAction(tr("🚩 Set as routing start"));
    setEndRouting = menu.addAction(tr("🏁 Set as routing end"));
  }

  QAction* selected = menu.exec(ev->globalPos());
  if (selected == hideElement) {
    QString road_id, element_id, group;
    size_t vi = picked_idx->vertex_index;
    if (picked_idx->layer == LayerType::kLanes) {
      road_id =
          QString::fromStdString(network_mesh_.lanes_mesh.get_road_id(vi));
      double s0 = network_mesh_.lanes_mesh.get_lanesec_s0(vi);
      int lane_id = network_mesh_.lanes_mesh.get_lane_id(vi);
      group = "lane";
      element_id = QString::fromStdString(FormatSectionValue(s0)) + ":" +
                   QString::number(lane_id);
    } else if (picked_idx->layer == LayerType::kObjects) {
      road_id = QString::fromStdString(
          network_mesh_.road_objects_mesh.get_road_id(vi));
      element_id = QString::fromStdString(
          network_mesh_.road_objects_mesh.get_road_object_id(vi));
      group = "objects";
    } else if (picked_idx->layer == LayerType::kSignalLights) {
      road_id = QString::fromStdString(
          network_mesh_.road_signals_mesh.get_road_id(vi));
      element_id = QString::fromStdString(
          network_mesh_.road_signals_mesh.get_road_signal_id(vi));
      group = "lights";
    } else if (picked_idx->layer == LayerType::kSignalSigns) {
      road_id = QString::fromStdString(
          network_mesh_.road_signals_mesh.get_road_id(vi));
      element_id = QString::fromStdString(
          network_mesh_.road_signals_mesh.get_road_signal_id(vi));
      group = "signs";
    }
    if (!road_id.isEmpty()) {
      SetElementVisible(
          QString("E:%1:%2:%3").arg(road_id).arg(group).arg(element_id), false);
    }
  } else if (selected == copy_coord) {
    QApplication::clipboard()->setText(coord_text);
  } else if (selected == copyInfo && copyInfo) {
    QApplication::clipboard()->setText(info_text);
  } else if (selected == copyAll && copyAll) {
    QApplication::clipboard()->setText(
        QString("%1 | %2").arg(coord_text).arg(info_text));
  } else if (selected == addFav) {
    QString road_id, element_id, name;
    TreeNodeType node_type = TreeNodeType::kRoad;
    size_t vi = picked_idx->vertex_index;
    if (picked_idx->layer == LayerType::kLanes) {
      road_id =
          QString::fromStdString(network_mesh_.lanes_mesh.get_road_id(vi));
      double s0 = network_mesh_.lanes_mesh.get_lanesec_s0(vi);
      int lane_id = network_mesh_.lanes_mesh.get_lane_id(vi);
      element_id = QString::fromStdString(FormatSectionValue(s0)) + ":" +
                   QString::number(lane_id);
      node_type = TreeNodeType::kLane;
      name = QString("kRoad %1 kLane %2").arg(road_id).arg(element_id);
    } else if (picked_idx->layer == LayerType::kObjects) {
      road_id = QString::fromStdString(
          network_mesh_.road_objects_mesh.get_road_id(vi));
      element_id = QString::fromStdString(
          network_mesh_.road_objects_mesh.get_road_object_id(vi));
      node_type = TreeNodeType::kObject;
      name = QString("kObject %1").arg(element_id);
    } else if (picked_idx->layer == LayerType::kSignalLights) {
      road_id = QString::fromStdString(
          network_mesh_.road_signals_mesh.get_road_id(vi));
      element_id = QString::fromStdString(
          network_mesh_.road_signals_mesh.get_road_signal_id(vi));
      node_type = TreeNodeType::kLight;
      name = QString("kLight %1").arg(element_id);
    } else if (picked_idx->layer == LayerType::kSignalSigns) {
      road_id = QString::fromStdString(
          network_mesh_.road_signals_mesh.get_road_id(vi));
      element_id = QString::fromStdString(
          network_mesh_.road_signals_mesh.get_road_signal_id(vi));
      node_type = TreeNodeType::kSign;
      name = QString("kSign %1").arg(element_id);
    }
    if (!road_id.isEmpty()) {
      emit AddFavoriteRequested(road_id, node_type, element_id, name);
    }
  } else if (selected == setStartRouting || selected == setEndRouting) {
    size_t vi = picked_idx->vertex_index;
    QString road_id =
        QString::fromStdString(network_mesh_.lanes_mesh.get_road_id(vi));
    double s0 = network_mesh_.lanes_mesh.get_lanesec_s0(vi);
    int lane_id = network_mesh_.lanes_mesh.get_lane_id(vi);
    const std::string lane_pos_std =
        BuildLanePosition(road_id.toStdString(), FormatSectionValue(s0),
                          QString::number(lane_id).toStdString());
    QString lane_pos = QString::fromStdString(lane_pos_std);
    if (selected == setStartRouting)
      emit RoutingStartRequested(lane_pos.trimmed());
    else
      emit RoutingEndRequested(lane_pos.trimmed());
  }
}

void GeoViewerWidget::UpdateHighlight(size_t vert_idx, LayerType type) {
  size_t start = 0, end = 0;
  const odr::Mesh3D* mesh = nullptr;

  if (type == LayerType::kLanes) {
    auto interval = network_mesh_.lanes_mesh.get_idx_interval_lane(vert_idx);
    start = interval[0];
    end = interval[1];
    mesh = &network_mesh_.lanes_mesh;
  } else if (type == LayerType::kObjects) {
    auto interval =
        network_mesh_.road_objects_mesh.get_idx_interval_road_object(vert_idx);
    start = interval[0];
    end = interval[1];
    mesh = &network_mesh_.road_objects_mesh;
  } else if (type == LayerType::kSignalLights ||
             type == LayerType::kSignalSigns) {
    auto interval =
        network_mesh_.road_signals_mesh.get_idx_interval_signal(vert_idx);
    start = interval[0];
    end = interval[1];
    mesh = &network_mesh_.road_signals_mesh;
  } else {
    ClearHighlight();
    return;
  }

  if (!gl_renderer_) return;
  auto* highlight_mgr = gl_renderer_->GetHighlightManager();
  if (!highlight_mgr) return;

  if (start == highlight_mgr->cur_start && end == highlight_mgr->cur_end &&
      type == highlight_mgr->cur_layer) {
    return;
  }
  highlight_mgr->cur_start = start;
  highlight_mgr->cur_end = end;
  highlight_mgr->cur_layer = type;

  std::vector<uint32_t> indices;
  size_t v_offset = gl_renderer_->GetLayerVertexOffset(type);

  for (size_t i = 0; i < mesh->indices.size(); i += 3) {
    uint32_t i0 = mesh->indices[i];
    uint32_t i1 = mesh->indices[i + 1];
    uint32_t i2 = mesh->indices[i + 2];
    if (i0 >= start && i0 < end && i1 >= start && i1 < end && i2 >= start &&
        i2 < end) {
      indices.push_back(i0 + v_offset);
      indices.push_back(i1 + v_offset);
      indices.push_back(i2 + v_offset);
    }
  }

  SetHighlightIndices(indices, type, type == LayerType::kLanes, vert_idx);
}



void GeoViewerWidget::UpdateHoverInfo(int x, int y) {
  if (!gl_renderer_) return;

  QVector3D world_pos;
  std::optional<PickResult> picked_idx;
  QString type_str, id_str, name_str;

  QSize viewport = gl_renderer_->GetViewportSize();
  float mouse_x = (2.0f * x) / viewport.width() - 1.0f;
  float mouse_y = 1.0f - (2.0f * y) / viewport.height();
  QMatrix4x4 inv_mvp =
      (gl_renderer_->GetProjectionMatrix() * GetViewMatrix()).inverted();
  QVector4D ro4 = inv_mvp * QVector4D(mouse_x, mouse_y, -1.0f, 1.0f);
  QVector4D re4 = inv_mvp * QVector4D(mouse_x, mouse_y, 1.0f, 1.0f);
  ro4 /= ro4.w();
  re4 /= re4.w();
  QVector3D origin(ro4.x(), ro4.y(), ro4.z());
  QVector3D direction = (re4 - ro4).toVector3D().normalized();

  picked_idx = GetPickedVertexIndex(x, y);

  if (picked_idx.has_value() && map_) {
    world_pos = origin + direction * closest_t_;
    size_t vi = picked_idx->vertex_index;

    if (picked_idx->layer == LayerType::kLanes) {
      std::string r_id = network_mesh_.lanes_mesh.get_road_id(vi);
      double s0 = network_mesh_.lanes_mesh.get_lanesec_s0(vi);
      int l_id = network_mesh_.lanes_mesh.get_lane_id(vi);

      type_str = "kLane";
      id_str = QString("%1 / %2 / %3").arg(r_id.c_str()).arg(s0).arg(l_id);

      if (map_->id_to_road.count(r_id)) {
        auto& r = map_->id_to_road.at(r_id);
        name_str =
            QString("kRoad: %1 | Length: %2").arg(r.name.c_str()).arg(r.length);

        if (r.s_to_lanesection.count(s0)) {
          auto& lanesec = r.s_to_lanesection.at(s0);
          if (lanesec.id_to_lane.count(l_id)) {
            type_str = QString("kLane (%1)")
                           .arg(lanesec.id_to_lane.at(l_id).type.c_str());
          }
        }
      }
      UpdateHighlight(vi, picked_idx->layer);
    } else if (picked_idx->layer == LayerType::kRoadmarks) {
      std::string r_id = network_mesh_.roadmarks_mesh.get_road_id(vi);
      type_str = "Roadmark";
      id_str = QString::fromStdString(
          network_mesh_.roadmarks_mesh.get_roadmark_type(vi));
      if (map_->id_to_road.count(r_id)) {
        const auto& r = map_->id_to_road.at(r_id);
        name_str = QString("kRoad: %1").arg(r.name.c_str());
      }
      ClearHighlight();
    } else if (picked_idx->layer == LayerType::kObjects) {
      std::string r_id = network_mesh_.road_objects_mesh.get_road_id(vi);
      std::string o_id = network_mesh_.road_objects_mesh.get_road_object_id(vi);
      type_str = "kObject";
      if (map_->id_to_road.count(r_id)) {
        const auto& r = map_->id_to_road.at(r_id);
        if (r.id_to_object.count(o_id)) {
          id_str = QString("%1 (%2)")
                       .arg(o_id.c_str())
                       .arg(r.id_to_object.at(o_id).type.c_str());
          name_str = QString::fromStdString(r.id_to_object.at(o_id).name);
        } else {
          id_str = QString::fromStdString(o_id);
        }
      } else {
        id_str = QString::fromStdString(o_id);
      }
      UpdateHighlight(vi, picked_idx->layer);
    } else if (picked_idx->layer == LayerType::kSignalLights ||
               picked_idx->layer == LayerType::kSignalSigns) {
      std::string r_id = network_mesh_.road_signals_mesh.get_road_id(vi);
      std::string s_id = network_mesh_.road_signals_mesh.get_road_signal_id(vi);
      type_str = picked_idx->layer == LayerType::kSignalLights ? "TrafficLight"
                                                               : "TrafficSign";
      if (map_->id_to_road.count(r_id)) {
        const auto& r = map_->id_to_road.at(r_id);
        if (r.id_to_signal.count(s_id)) {
          id_str = QString("%1 (%2)")
                       .arg(s_id.c_str())
                       .arg(r.id_to_signal.at(s_id).type.c_str());
          name_str = QString::fromStdString(r.id_to_signal.at(s_id).name);
        } else {
          id_str = QString::fromStdString(s_id);
        }
      } else {
        id_str = QString::fromStdString(s_id);
      }
      UpdateHighlight(vi, picked_idx->layer);
    } else if (picked_idx->layer == LayerType::kJunctions) {
      auto group_id = FindJunctionGroupByVertex(vi);
      if (group_id.has_value()) {
        const std::string& group_id_str = *group_id;
        type_str = "Junction";
        id_str = QString::fromStdString(group_id_str);
        auto group_index_it = junction_group_index_by_id_.find(group_id_str);
        if (group_index_it != junction_group_index_by_id_.end()) {
          const auto& group =
              junction_cluster_result_.groups[group_index_it->second];
          name_str = QString("%1 | %2 roads | %3")
                         .arg(JunctionClusterUtil::SemanticTypeToString(
                             group.semantic_type))
                         .arg((int)group.external_road_ids.size())
                         .arg((int)group.junction_ids.size());
        }
        auto indices = CollectIndicesForCachedElements(
            LayerType::kJunctions, junction_element_items_,
            junction_mesh_.indices, [&](const SceneCachedElement& element) {
              return element.element_key == ("JG:" + group_id_str);
            });
        SetHighlightIndices(indices, LayerType::kJunctions);
      } else {
        ClearHighlight();
      }
    }
  } else {
    world_pos = origin + direction * 1000.0f;
    ClearHighlight();
  }

  double local_x, local_y, local_z;
  RendererToLocalCoord(world_pos, local_x, local_y, local_z);
  double lon = local_x, lat = local_y, alt = local_z;
  if (georeference_valid_) {
    LocalToWGS84(local_x, local_y, local_z, lon, lat, alt);
  }

  emit HoverInfoChanged(local_x, local_y, local_z, lon, lat, alt, type_str,
                        id_str, name_str);
}

void GeoViewerWidget::SearchObject(LayerType type, const QString& id_str) {
  if (!map_) return;
  std::string target_id = id_str.toStdString();
  size_t target_start_vertex = SIZE_MAX;
  const odr::Mesh3D* target_mesh = nullptr;

  auto find_id_str = [&](const std::map<size_t, std::string>& m) {
    for (const auto& kv : m) {
      if (kv.second == target_id) return kv.first;
    }
    return SIZE_MAX;
  };

  if (type == LayerType::kLanes) {
    target_start_vertex =
        find_id_str(network_mesh_.lanes_mesh.road_start_indices);
    target_mesh = &network_mesh_.lanes_mesh;
  } else if (type == LayerType::kObjects) {
    target_start_vertex =
        find_id_str(network_mesh_.road_objects_mesh.road_object_start_indices);
    target_mesh = &network_mesh_.road_objects_mesh;
  } else if (type == LayerType::kSignalLights ||
             type == LayerType::kSignalSigns) {
    target_start_vertex =
        find_id_str(network_mesh_.road_signals_mesh.road_signal_start_indices);
    target_mesh = &network_mesh_.road_signals_mesh;
    if (target_start_vertex != SIZE_MAX) {
      std::string r_id =
          network_mesh_.road_signals_mesh.get_road_id(target_start_vertex);
      if (map_->id_to_road.count(r_id)) {
        bool is_light = false;
        auto& r = map_->id_to_road.at(r_id);
        if (r.id_to_signal.count(target_id))
          is_light = (r.id_to_signal.at(target_id).name == "TrafficLight");
        if ((type == LayerType::kSignalLights && !is_light) ||
            (type == LayerType::kSignalSigns && is_light)) {
          target_start_vertex = SIZE_MAX;
        }
      }
    }
  }

  if (target_start_vertex == SIZE_MAX || !target_mesh) return;

  UpdateHighlight(target_start_vertex, type);

  if (!gl_renderer_) return;
  auto* highlight_mgr = gl_renderer_->GetHighlightManager();
  if (highlight_mgr && highlight_mgr->cur_start < highlight_mgr->cur_end) {
    double cx = 0, cy = 0, cz = 0;
    int count = 0;
    for (size_t i = highlight_mgr->cur_start;
         i < highlight_mgr->cur_end && i < target_mesh->vertices.size(); i++) {
      cx += target_mesh->vertices[i][0];
      cy += target_mesh->vertices[i][1];
      cz += target_mesh->vertices[i][2];
      count++;
    }
    if (count > 0) {
      camera_.SetTarget(QVector3D(cx / count, cy / count, cz / count));
      camera_.SetPitch(-89.0f);
      camera_.SetYaw(0.0f);
      camera_.SetDistance(60.0f);
      update();
    }
  }
}

void GeoViewerWidget::BuildJunctionPlanes() {
  junction_mesh_ = odr::Mesh3D();
  junction_element_items_.clear();
  junction_group_centers_.clear();
  junction_vertex_group_indices_.clear();
  if (junction_cluster_result_.groups.empty()) return;

  junction_vertex_group_indices_.reserve(
      4 * junction_cluster_result_.groups.size());

  for (std::size_t group_index = 0;
       group_index < junction_cluster_result_.groups.size(); ++group_index) {
    const auto& group = junction_cluster_result_.groups[group_index];
    std::vector<odr::Vec3D> points;
    points.reserve(group.boundary_arms.size());
    for (const auto& arm : group.boundary_arms) {
      points.push_back(arm.point);
    }
    if (points.size() < 3) {
      continue;
    }

    std::sort(points.begin(), points.end(),
              [](const odr::Vec3D& lhs, const odr::Vec3D& rhs) {
                if (lhs[0] != rhs[0]) return lhs[0] < rhs[0];
                return lhs[1] < rhs[1];
              });
    points.erase(std::unique(points.begin(), points.end(),
                             [](const odr::Vec3D& lhs, const odr::Vec3D& rhs) {
                               return std::fabs(lhs[0] - rhs[0]) < 1e-3 &&
                                      std::fabs(lhs[1] - rhs[1]) < 1e-3;
                             }),
                 points.end());
    if (points.size() < 3) {
      continue;
    }

    odr::Vec3D centroid{0.0, 0.0, 0.0};
    for (const auto& point : points) {
      centroid[0] += point[0];
      centroid[1] += point[1];
      centroid[2] += point[2];
    }
    centroid[0] /= static_cast<double>(points.size());
    centroid[1] /= static_cast<double>(points.size());
    centroid[2] /= static_cast<double>(points.size());

    std::sort(points.begin(), points.end(),
              [&centroid](const odr::Vec3D& lhs, const odr::Vec3D& rhs) {
                const double lhs_angle =
                    std::atan2(lhs[1] - centroid[1], lhs[0] - centroid[0]);
                const double rhs_angle =
                    std::atan2(rhs[1] - centroid[1], rhs[0] - centroid[0]);
                return lhs_angle < rhs_angle;
              });

    const uint32_t base = static_cast<uint32_t>(junction_mesh_.vertices.size());
    QVector3D center_renderer =
        LocalToRendererPoint({centroid[0], centroid[1], centroid[2] + 0.05});
    junction_mesh_.vertices.push_back(
        {center_renderer.x(), center_renderer.y(), center_renderer.z()});
    junction_vertex_group_indices_.push_back(group_index);
    for (const auto& point : points) {
      QVector3D renderer =
          LocalToRendererPoint({point[0], point[1], point[2] + 0.05});
      junction_mesh_.vertices.push_back(
          {renderer.x(), renderer.y(), renderer.z()});
      junction_vertex_group_indices_.push_back(group_index);
    }
    junction_group_centers_[group.group_id] = LocalToRendererPoint(centroid);

    const uint32_t tri_start =
        static_cast<uint32_t>(junction_mesh_.indices.size() / 3);
    for (uint32_t i = 1; i <= points.size(); ++i) {
      const uint32_t next = (i == points.size()) ? 1 : (i + 1);
      junction_mesh_.indices.insert(junction_mesh_.indices.end(),
                                    {base, base + i, base + next});
    }

    SceneCachedElement element;
    element.road_key = "JG:" + group.group_id;
    element.group_key = "junctions";
    element.element_key = "JG:" + group.group_id;
    element.ranges.push_back({tri_start, static_cast<uint32_t>(points.size())});
    junction_element_items_.push_back(std::move(element));
  }
}

std::optional<std::string> GeoViewerWidget::FindJunctionGroupByTriangle(
    uint32_t tri_index) const {
  for (std::size_t group_index = 0;
       group_index < junction_element_items_.size(); ++group_index) {
    const auto& element = junction_element_items_[group_index];
    for (const auto& range : element.ranges) {
      if (tri_index >= range.start && tri_index < range.start + range.count) {
        return junction_cluster_result_.groups[group_index].group_id;
      }
    }
  }
  return std::nullopt;
}

std::optional<std::string> GeoViewerWidget::FindJunctionGroupByVertex(
    size_t vertex_index) const {
  if (vertex_index >= junction_vertex_group_indices_.size()) {
    return std::nullopt;
  }
  const std::size_t group_index = junction_vertex_group_indices_[vertex_index];
  if (group_index >= junction_cluster_result_.groups.size()) {
    return std::nullopt;
  }
  return junction_cluster_result_.groups[group_index].group_id;
}

std::vector<uint32_t> GeoViewerWidget::CollectIndicesForCachedElements(
    LayerType type, const std::vector<SceneCachedElement>& elements,
    const std::vector<uint32_t>& source_indices,
    const std::function<bool(const SceneCachedElement&)>& predicate) const {
  if (!gl_renderer_) return {};
  return CollectSceneIndices(elements, source_indices,
                             gl_renderer_->GetLayerVertexOffset(type),
                             predicate);
}

const odr::Mesh3D* GeoViewerWidget::MeshForLayer(LayerType type) const {
  if (type == LayerType::kLanes) return &network_mesh_.lanes_mesh;
  if (type == LayerType::kRoadmarks) return &network_mesh_.roadmarks_mesh;
  if (type == LayerType::kObjects) return &network_mesh_.road_objects_mesh;
  if (type == LayerType::kSignalLights || type == LayerType::kSignalSigns) {
    return &network_mesh_.road_signals_mesh;
  }
  if (type == LayerType::kJunctions) return &junction_mesh_;
  return nullptr;
}

bool GeoViewerWidget::IsTrianglePickVisible(LayerType type,
                                            uint32_t triangle_index,
                                            size_t vertex_index) const {
  std::string road_id;
  std::string element_id;
  std::string group;
  if (type == LayerType::kLanes) {
    road_id = network_mesh_.lanes_mesh.get_road_id(vertex_index);
    element_id =
        std::to_string(network_mesh_.lanes_mesh.get_lanesec_s0(vertex_index));
    group = "section";
  } else if (type == LayerType::kRoadmarks) {
    road_id = network_mesh_.roadmarks_mesh.get_road_id(vertex_index);
    group = "section";
  } else if (type == LayerType::kObjects) {
    road_id = network_mesh_.road_objects_mesh.get_road_id(vertex_index);
    element_id =
        network_mesh_.road_objects_mesh.get_road_object_id(vertex_index);
    group = "objects";
  } else if (type == LayerType::kSignalLights ||
             type == LayerType::kSignalSigns) {
    road_id = network_mesh_.road_signals_mesh.get_road_id(vertex_index);
    element_id =
        network_mesh_.road_signals_mesh.get_road_signal_id(vertex_index);
    group = (type == LayerType::kSignalLights) ? "lights" : "signs";
  } else if (type == LayerType::kJunctions) {
    const auto group_id = FindJunctionGroupByTriangle(triangle_index);
    return group_id.has_value() &&
           hidden_elements_.count("JG:" + *group_id) == 0;
  }
  return IsElementActuallyVisible(road_id, group, element_id);
}

void GeoViewerWidget::SetHighlightIndices(const std::vector<uint32_t>& indices,
                                          LayerType type, bool with_neighbors,
                                          size_t reference_vertex) {
  if (!gl_renderer_) return;
  auto* highlight_mgr = gl_renderer_->GetHighlightManager();
  if (!highlight_mgr) return;

  highlight_mgr->cur_layer = type;
  highlight_mgr->bounds_valid = false;

  const odr::Mesh3D* mesh = MeshForLayer(type);

  // Calculate highlighted bounding box
  if (mesh && !indices.empty()) {
    const size_t v_offset = gl_renderer_->GetLayerVertexOffset(type);
    QVector3D min_b(std::numeric_limits<float>::max(),
                    std::numeric_limits<float>::max(),
                    std::numeric_limits<float>::max());
    QVector3D max_b(std::numeric_limits<float>::lowest(),
                    std::numeric_limits<float>::lowest(),
                    std::numeric_limits<float>::lowest());
    for (uint32_t global_idx : indices) {
      if (global_idx < v_offset) continue;
      const size_t local_idx = static_cast<size_t>(global_idx - v_offset);
      if (local_idx >= mesh->vertices.size()) continue;
      const auto& v = mesh->vertices[local_idx];
      min_b.setX(std::min(min_b.x(), static_cast<float>(v[0])));
      min_b.setY(std::min(min_b.y(), static_cast<float>(v[1])));
      min_b.setZ(std::min(min_b.z(), static_cast<float>(v[2])));
      max_b.setX(std::max(max_b.x(), static_cast<float>(v[0])));
      max_b.setY(std::max(max_b.y(), static_cast<float>(v[1])));
      max_b.setZ(std::max(max_b.z(), static_cast<float>(v[2])));
    }
    if (min_b.x() <= max_b.x()) {
      highlight_mgr->bounds_valid = true;
      highlight_mgr->min_bound = min_b;
      highlight_mgr->max_bound = max_b;
    }
  }

  // Upload primary highlight
  makeCurrent();
  highlight_mgr->UploadHighlight(indices);

  // Neighbor highlight
  if (with_neighbors && type == LayerType::kLanes && routing_graph_) {
    std::vector<uint32_t> n_indices;
    const std::string road_id =
        network_mesh_.lanes_mesh.get_road_id(reference_vertex);
    const double s0 = network_mesh_.lanes_mesh.get_lanesec_s0(reference_vertex);
    const int lane_id = network_mesh_.lanes_mesh.get_lane_id(reference_vertex);
    const odr::LaneKey key(road_id, s0, lane_id);

    std::vector<odr::LaneKey> neighbors;
    auto succs = routing_graph_->get_lane_successors(key);
    auto preds = routing_graph_->get_lane_predecessors(key);
    neighbors.insert(neighbors.end(), succs.begin(), succs.end());
    neighbors.insert(neighbors.end(), preds.begin(), preds.end());

    const size_t lane_v_offset =
        gl_renderer_->GetLayerVertexOffset(LayerType::kLanes);
    for (const auto& neighbor_key : neighbors) {
      auto item_itr = lane_element_index_by_key_.find(neighbor_key);
      if (item_itr != lane_element_index_by_key_.end()) {
        const auto& el = lane_element_items_[item_itr->second];
        for (const auto& range : el.ranges) {
          const std::size_t base = static_cast<std::size_t>(range.start) * 3;
          for (uint32_t k = 0; k < range.count * 3; ++k) {
            n_indices.push_back(network_mesh_.lanes_mesh.indices[base + k] +
                                static_cast<uint32_t>(lane_v_offset));
          }
        }
      }
    }
    highlight_mgr->UploadNeighborHighlight(n_indices);
  } else {
    // Clear neighbor highlights
    highlight_mgr->UploadNeighborHighlight({});
  }
  doneCurrent();

  if (!highlight_mgr->HasHighlight()) {
    highlight_mgr->cur_start = SIZE_MAX;
    highlight_mgr->cur_end = 0;
    highlight_mgr->bounds_valid = false;
  }
  update();
}

void GeoViewerWidget::HighlightElement(const QString& road_id,
                                       TreeNodeType type,
                                       const QString& element_id) {
  if (type == TreeNodeType::kJunction || type == TreeNodeType::kJunctionGroup) {
    selected_junction_group_id_ =
        (type == TreeNodeType::kJunctionGroup) ? element_id : road_id;
    selected_junction_id_ =
        (type == TreeNodeType::kJunction) ? element_id : QString();
    ClearHighlight();
    update();
    return;
  }

  selected_junction_group_id_.clear();
  selected_junction_id_.clear();
  if (!map_ || road_id.isEmpty()) {
    ClearHighlight();
    return;
  }

  LayerType layer_type = LayerType::kLanes;

  std::string road_id_str = road_id.toStdString();
  std::string element_id_str = element_id.toStdString();

  if (type == TreeNodeType::kRoad) {
    layer_type = LayerType::kLanes;
    auto indices = CollectIndicesForCachedElements(
        layer_type, lane_element_items_, network_mesh_.lanes_mesh.indices,
        [&](const SceneCachedElement& element) {
          return element.road_key == ("R:" + road_id_str);
        });
    SetHighlightIndices(indices, layer_type);
    return;
  } else if (type == TreeNodeType::kSectionGroup) {
    layer_type = LayerType::kLanes;
    auto indices = CollectIndicesForCachedElements(
        layer_type, lane_element_items_, network_mesh_.lanes_mesh.indices,
        [&](const SceneCachedElement& element) {
          return element.group_key == ("G:" + road_id_str + ":section");
        });
    SetHighlightIndices(indices, layer_type);
    return;
  } else if (type == TreeNodeType::kSection || type == TreeNodeType::kLane) {
    layer_type = LayerType::kLanes;
    const std::string prefix =
        "E:" + road_id_str +
        ":lane:" + element_id_str.substr(0, element_id_str.find(':'));
    auto indices = CollectIndicesForCachedElements(
        layer_type, lane_element_items_, network_mesh_.lanes_mesh.indices,
        [&](const SceneCachedElement& element) {
          if (type == TreeNodeType::kSection) {
            return element.element_key.rfind(prefix + ":", 0) == 0;
          }
          return element.element_key ==
                 ("E:" + road_id_str + ":lane:" + element_id_str);
        });
    SetHighlightIndices(indices, layer_type);
    return;
  } else if (type == TreeNodeType::kObjectGroup) {
    layer_type = LayerType::kObjects;
    auto indices = CollectIndicesForCachedElements(
        layer_type, object_element_items_,
        network_mesh_.road_objects_mesh.indices,
        [&](const SceneCachedElement& element) {
          return element.group_key == ("G:" + road_id_str + ":objects");
        });
    SetHighlightIndices(indices, layer_type);
    return;
  } else if (type == TreeNodeType::kObject) {
    layer_type = LayerType::kObjects;
    auto indices = CollectIndicesForCachedElements(
        layer_type, object_element_items_,
        network_mesh_.road_objects_mesh.indices,
        [&](const SceneCachedElement& element) {
          return element.element_key ==
                 ("E:" + road_id_str + ":objects:" + element_id_str);
        });
    SetHighlightIndices(indices, layer_type);
    return;
  } else if (type == TreeNodeType::kLightGroup ||
             type == TreeNodeType::kSignGroup) {
    layer_type = (type == TreeNodeType::kLightGroup) ? LayerType::kSignalLights
                                                     : LayerType::kSignalSigns;
    const std::string group =
        (type == TreeNodeType::kLightGroup) ? "light" : "sign";
    auto indices = CollectIndicesForCachedElements(
        layer_type, signal_element_items_,
        network_mesh_.road_signals_mesh.indices,
        [&](const SceneCachedElement& element) {
          return element.group_key == ("G:" + road_id_str + ":" + group);
        });
    SetHighlightIndices(indices, layer_type);
    return;
  } else if (type == TreeNodeType::kLight || type == TreeNodeType::kSign) {
    layer_type = (type == TreeNodeType::kLight) ? LayerType::kSignalLights
                                                : LayerType::kSignalSigns;
    const std::string group = (type == TreeNodeType::kLight) ? "light" : "sign";
    auto indices = CollectIndicesForCachedElements(
        layer_type, signal_element_items_,
        network_mesh_.road_signals_mesh.indices,
        [&](const SceneCachedElement& element) {
          return element.element_key ==
                 ("E:" + road_id_str + ":" + group + ":" + element_id_str);
        });
    SetHighlightIndices(indices, layer_type);
    return;
  }

  ClearHighlight();
}

void GeoViewerWidget::CenterOnElement(const QString& road_id, TreeNodeType type,
                                      const QString& element_id) {
  if (!map_) return;

  if (type == TreeNodeType::kJunction || type == TreeNodeType::kJunctionGroup) {
    HighlightElement(road_id, type, element_id);
    QString group_id =
        (type == TreeNodeType::kJunctionGroup) ? element_id : road_id;
    auto group_itr = junction_group_index_by_id_.find(group_id.toStdString());
    if (group_itr != junction_group_index_by_id_.end()) {
      camera_.SetTarget(JunctionGroupCenter(
          junction_cluster_result_.groups[group_itr->second]));
      camera_.SetPitch(-75.0f);
      camera_.SetYaw(25.0f);
      camera_.SetDistance(80.0f);
      update();
    }
    return;
  }

  if (road_id.isEmpty()) return;

  HighlightElement(road_id, type, element_id);

  // Find the interval and mesh again for centering (or we could pass it from
  // HighlightElement)
  std::string road_id_str = road_id.toStdString();
  std::string element_id_str = element_id.toStdString();

  if (!gl_renderer_) return;
  auto* highlight_mgr = gl_renderer_->GetHighlightManager();

  const size_t target_start_vertex =
      highlight_mgr ? highlight_mgr->cur_start : SIZE_MAX;
  const LayerType layer_type =
      highlight_mgr ? highlight_mgr->cur_layer : LayerType::kCount;
  const odr::Mesh3D* target_mesh = nullptr;

  if (layer_type == LayerType::kLanes)
    target_mesh = &network_mesh_.lanes_mesh;
  else if (layer_type == LayerType::kObjects)
    target_mesh = &network_mesh_.road_objects_mesh;
  else if (layer_type == LayerType::kSignalLights ||
           layer_type == LayerType::kSignalSigns)
    target_mesh = &network_mesh_.road_signals_mesh;

  if (highlight_mgr && highlight_mgr->bounds_valid) {
    camera_.SetTarget((highlight_mgr->min_bound + highlight_mgr->max_bound) *
                      0.5f);
    camera_.SetPitch(-60.0f);
    camera_.SetYaw(45.0f);
    camera_.SetDistance(
        qMax(20.0f,
             (highlight_mgr->max_bound - highlight_mgr->min_bound).length() *
                 1.8f));
    update();
    return;
  }

  if (target_start_vertex != SIZE_MAX && target_mesh) {
    // Compute center of the element
    double cx = 0, cy = 0, cz = 0;
    int count = 0;

    size_t start = 0, end = 0;
    if (layer_type == LayerType::kLanes) {
      if (type == TreeNodeType::kRoad) {
        start = target_start_vertex;
        end = network_mesh_.lanes_mesh.vertices.size();
        for (size_t i = start; i < network_mesh_.lanes_mesh.vertices.size();
             i++) {
          if (network_mesh_.lanes_mesh.get_road_id(i) != road_id_str) {
            end = i;
            break;
          }
        }
      } else {
        auto interval =
            network_mesh_.lanes_mesh.get_idx_interval_lane(target_start_vertex);
        start = interval[0];
        end = interval[1];
      }
    } else if (layer_type == LayerType::kObjects) {
      auto interval =
          network_mesh_.road_objects_mesh.get_idx_interval_road_object(
              target_start_vertex);
      start = interval[0];
      end = interval[1];
    } else if (layer_type == LayerType::kSignalLights ||
               layer_type == LayerType::kSignalSigns) {
      auto interval = network_mesh_.road_signals_mesh.get_idx_interval_signal(
          target_start_vertex);
      start = interval[0];
      end = interval[1];
    }

    for (size_t i = start; i < end && i < target_mesh->vertices.size(); i++) {
      cx += target_mesh->vertices[i][0];
      cy += target_mesh->vertices[i][1];
      cz += target_mesh->vertices[i][2];
      count++;
    }

    if (count > 0) {
      camera_.SetTarget(QVector3D(cx / count, cy / count, cz / count));
      camera_.SetPitch(-60.0f);
      camera_.SetYaw(45.0f);
      camera_.SetDistance(60.0f);
      update();
    }
  }
}

void GeoViewerWidget::HighlightRoads(const QStringList& road_ids) {
  if (!map_ || road_ids.isEmpty()) {
    ClearHighlight();
    return;
  }
  std::set<std::string> ids;
  for (const auto& road_id : road_ids) {
    ids.insert(road_id.toStdString());
  }
  auto indices = CollectIndicesForCachedElements(
      LayerType::kLanes, lane_element_items_, network_mesh_.lanes_mesh.indices,
      [&](const SceneCachedElement& element) {
        const std::string prefix = "R:";
        return element.road_key.rfind(prefix, 0) == 0 &&
               ids.count(element.road_key.substr(prefix.size())) > 0;
      });
  SetHighlightIndices(indices, LayerType::kLanes);
}

void GeoViewerWidget::JumpToLocation(double lon, double lat, double alt) {
  if (!map_) return;

  double x = lon, y = lat, z = alt;
  try {
    CoordinateUtil::Instance().WGS84ToLocal(&x, &y, &z);
    JumpToLocalLocation(x, y, z);
  } catch (const std::exception& e) {
    qDebug() << "Jump to location error:" << e.what();
  }
}

void GeoViewerWidget::JumpToLocalLocation(double x, double y, double z) {
  if (!map_) return;

  // In our coordinate system:
  // x = local x (East)
  // y = local y (North)
  // z = local z (Up)
  // In renderer:
  // renderer.x = x
  // renderer.y = z
  // renderer.z = y

  float rz = static_cast<float>(y);
  if (right_hand_traffic_) rz = -rz;

  camera_.SetTarget(
      QVector3D(static_cast<float>(x), static_cast<float>(z), rz));
  camera_.SetPitch(-89.0f);  // Look straight down
  camera_.SetYaw(0.0f);
  camera_.SetDistance(100.0f);  // Zoom level
  update();
}

void GeoViewerWidget::GenerateRefLinePoints(
    std::shared_ptr<odr::OpenDriveMap> map, std::vector<float>& all_vertices,
    std::map<std::string, VertRange>& ranges) {
  if (!map) return;
  for (const auto& [road_id, road] : map->id_to_road) {
    size_t startIdx = all_vertices.size() / 3;
    double road_len = road.length;

    // Samples
    std::vector<odr::Vec3D> pts;
    double step = 2.0;
    for (double s = 0; s <= road_len; s += step) {
      pts.push_back(road.get_xyz(s, 0, 0));
    }
    if (road_len > (pts.size() - 1) * step)
      pts.push_back(road.get_xyz(road_len, 0, 0));

    // Swap Y and Z and mirror Y for all points to match renderer (X, Z, -Y)
    auto transformPt = [&](odr::Vec3D p) {
      float rx = static_cast<float>(p[0]);
      float ry = static_cast<float>(p[2]);  // Elevation ODR Z -> Renderer Y
      float rz = right_hand_traffic_
                     ? -static_cast<float>(p[1])
                     : static_cast<float>(p[1]);  // Lateral mirrored
      return QVector3D(rx, ry, rz);
    };

    // Line segments
    for (size_t i = 0; i < pts.size() - 1; ++i) {
      QVector3D p1 = transformPt(pts[i]);
      QVector3D p2 = transformPt(pts[i + 1]);
      all_vertices.push_back(p1.x());
      all_vertices.push_back(p1.y());
      all_vertices.push_back(p1.z());
      all_vertices.push_back(p2.x());
      all_vertices.push_back(p2.y());
      all_vertices.push_back(p2.z());
    }

    // Arrows every 20m
    for (double s = std::min(10.0, road_len); s < road_len; s += 20.0) {
      odr::Vec3D p = road.get_xyz(s, 0, 0);
      odr::Vec3D d = road.ref_line.get_grad(s);
      // Removed the d[0]=-d[0] flip which caused reversed direction perception
      double norm = std::sqrt(d[0] * d[0] + d[1] * d[1]);
      if (norm > 1e-6) {
        odr::Vec2D dir = {d[0] / norm, d[1] / norm};
        odr::Vec2D side = {-dir[1], dir[0]};

        // Triangle points in XY (local ODR)
        odr::Vec3D p1_l = {p[0] + dir[0] * 2.0, p[1] + dir[1] * 2.0, p[2]};
        odr::Vec3D p2_l = {p[0] - dir[0] * 1.0 + side[0] * 0.8,
                           p[1] - dir[1] * 1.0 + side[1] * 0.8, p[2]};
        odr::Vec3D p3_l = {p[0] - dir[0] * 1.0 - side[0] * 0.8,
                           p[1] - dir[1] * 1.0 - side[1] * 0.8, p[2]};

        std::vector<QVector3D> tri = {transformPt(p1_l), transformPt(p2_l),
                                      transformPt(p3_l)};

        // Add as triangle lines (3 segments)
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
    }

    ranges[road_id] = {startIdx, (all_vertices.size() / 3) - startIdx};
  }
}

QVector3D GeoViewerWidget::JunctionGroupCenter(
    const JunctionClusterGroup& group) const {
  auto it = junction_group_centers_.find(group.group_id);
  if (it != junction_group_centers_.end()) {
    return it->second;
  }
  if (!group.incoming_box.valid) {
    return QVector3D();
  }
  odr::Vec3D local_center{
      (group.incoming_box.min[0] + group.incoming_box.max[0]) * 0.5,
      (group.incoming_box.min[1] + group.incoming_box.max[1]) * 0.5,
      (group.incoming_box.min[2] + group.incoming_box.max[2]) * 0.5};
  return LocalToRendererPoint(local_center);
}

QVector3D GeoViewerWidget::LocalToRendererPoint(const odr::Vec3D& point) const {
  float rx = static_cast<float>(point[0]);
  float ry = static_cast<float>(point[2]);
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
            ("J:" + group_id + ":" + junction_id).toStdString()))
      return false;
  } else {
    // Check group-level visibility (JG:group_id)
    if (hidden_elements_.count(("JG:" + group_id).toStdString())) return false;

    // Hide cluster overlay only when all child junctions are hidden.
    auto group_index_itr =
        junction_group_index_by_id_.find(group_id.toStdString());
    if (group_index_itr != junction_group_index_by_id_.end()) {
      const auto& group =
          junction_cluster_result_.groups[group_index_itr->second];
      if (!group.junction_ids.empty()) {
        bool all_children_hidden = true;
        for (const auto& jid : group.junction_ids) {
          std::string junction_key = "J:" + group.group_id + ":" + jid;
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

  QPen ringPen(QColor(255, 200, 60, 220));
  ringPen.setWidth(2);
  QPen selectedPen(QColor(80, 255, 140, 240));
  selectedPen.setWidth(3);
  painter.setFont(QFont("Menlo", 9));

  for (const auto& group : junction_cluster_result_.groups) {
    const QString group_id = QString::fromStdString(group.group_id);
    if (!IsJunctionVisible(group_id)) continue;

    const QVector3D center = JunctionGroupCenter(group);
    QVector4D clip_pos = view_proj * QVector4D(center, 1.0f);
    if (clip_pos.w() <= 0.0f) continue;
    const float ndc_x = clip_pos.x() / clip_pos.w();
    const float ndc_y = clip_pos.y() / clip_pos.w();
    if (ndc_x < -1.2f || ndc_x > 1.2f || ndc_y < -1.2f || ndc_y > 1.2f)
      continue;

    const int sx = (int)((ndc_x + 1.0f) * width() * 0.5f);
    const int sy = (int)((1.0f - ndc_y) * height() * 0.5f);
    const bool selected = (selected_junction_group_id_ == group_id);

    painter.setPen(selected ? selectedPen : ringPen);
    painter.setBrush(selected ? QColor(80, 255, 140, 80)
                              : QColor(255, 200, 60, 50));
    painter.drawEllipse(QPointF(sx, sy), selected ? 9.0 : 7.0,
                        selected ? 9.0 : 7.0);

    for (const auto& junction_id_itr : group.junction_ids) {
      const QString junction_id = QString::fromStdString(junction_id_itr);
      if (!IsJunctionVisible(group_id, junction_id)) continue;
      auto member_index_itr =
          junction_member_index_by_id_.find(junction_id_itr);
      if (member_index_itr == junction_member_index_by_id_.end()) {
        continue;
      }
      const auto& member =
          junction_cluster_result_.junctions[member_index_itr->second];
      if (!member.incoming_box.valid) {
        continue;
      }
      const odr::Vec3D local_center{
          (member.incoming_box.min[0] + member.incoming_box.max[0]) * 0.5,
          (member.incoming_box.min[1] + member.incoming_box.max[1]) * 0.5,
          (member.incoming_box.min[2] + member.incoming_box.max[2]) * 0.5};
      const QVector3D memberCenter = LocalToRendererPoint(local_center);
      QVector4D member_clip = view_proj * QVector4D(memberCenter, 1.0f);
      if (member_clip.w() <= 0.0f) continue;
      const int msx =
          (int)(((member_clip.x() / member_clip.w()) + 1.0f) * width() * 0.5f);
      const int msy =
          (int)((1.0f - (member_clip.y() / member_clip.w())) * height() * 0.5f);
      const bool memberSelected =
          selected && (selected_junction_id_.isEmpty() ||
                       selected_junction_id_ == junction_id);
      painter.setPen(Qt::NoPen);
      painter.setBrush(memberSelected ? QColor(80, 255, 140, 240)
                                      : QColor(255, 230, 160, 220));
      painter.drawEllipse(QPointF(msx, msy), memberSelected ? 4.0 : 3.0,
                          memberSelected ? 4.0 : 3.0);
    }

    const QString text = QString("%1 [%2]").arg(group_id).arg(
        JunctionClusterUtil::SemanticTypeToString(group.semantic_type));
    painter.setPen(Qt::white);
    painter.drawText(sx + 10, sy - 8, text);
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
