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
#include <future>
#include "src/core/coordinate_util.h"
#include "src/core/thread_pool.h"
#include "src/logic/scene_index_builder.h"
#include "src/logic/spatial_grid_index.h"

GeoViewerWidget::GeoViewerWidget(QWidget* parent)
    : QOpenGLWidget(parent), right_hand_traffic_(true) {
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);

  // Initialize visibility array with defaults
  for (int i = 0; i < static_cast<int>(LayerType::kCount); ++i) {
    layer_visibility_[i] = true;
  }
  // Junctions and Signal Signs are hidden by default in UI
  layer_visibility_[static_cast<size_t>(LayerType::kJunctions)] = false;
  layer_visibility_[static_cast<size_t>(LayerType::kSignalSigns)] = false;
  layer_visibility_[static_cast<size_t>(LayerType::kObjects)] = false;
  layer_visibility_[static_cast<size_t>(LayerType::kFacilities)] = true;

  network_mesh_ = std::make_shared<odr::RoadNetworkMesh>();
  junction_mesh_ = std::make_shared<odr::Mesh3D>();
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
  layer_visibility_[static_cast<size_t>(type)] = visible;
  
  if (gl_renderer_) {
    gl_renderer_->SetLayerVisible(type, visible);
    
    // Sync dashed lane lines with solid lane lines
    if (type == LayerType::kLaneLines) {
      gl_renderer_->SetLayerVisible(LayerType::kLaneLinesDashed, visible);
      layer_visibility_[static_cast<size_t>(LayerType::kLaneLinesDashed)] = visible;
    }
  }

  update();
}

bool GeoViewerWidget::IsLayerVisible(LayerType type) const {
  return layer_visibility_[static_cast<size_t>(type)];
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
    update();
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
          spatial_grid_data_, ray_origin, ray_dir,
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
          spatial_grid_data_, ray_origin, ray_dir,
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

  // Upload position (3 floats) + color (4 floats) for each point.
  // Visibility is handled by alpha=0 in the color or discarding in shader.
  // Here we use alpha=0 for invisible points.
  std::vector<float> data;
  data.reserve(user_points_.size() * 7);
  for (const auto& p : user_points_) {
    data.push_back(p.world_pos.x());
    data.push_back(p.world_pos.y());
    data.push_back(p.world_pos.z());
    data.push_back(p.color.x());
    data.push_back(p.color.y());
    data.push_back(p.color.z());
    data.push_back(p.visible ? 1.0f : 0.0f);
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
  if (!map_ || !gl_renderer_ || !network_mesh_ || !junction_mesh_) return;

  auto& network_mesh = *network_mesh_;
  auto& junction_mesh = *junction_mesh_;

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
        elements, original_indices, gl_renderer_->GetLayerVertexOffset(type),
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
  futures.reserve(6);
  futures.push_back(pool.Enqueue([&]() {
    collectLayerData(LayerType::kLanes, lane_element_items_,
                     network_mesh.lanes_mesh.indices, &network_mesh.lanes_mesh);
  }));
  futures.push_back(pool.Enqueue([&]() {
    collectLayerData(LayerType::kRoadmarks, roadmark_element_items_,
                     network_mesh.roadmarks_mesh.indices,
                     &network_mesh.roadmarks_mesh);
  }));
  futures.push_back(pool.Enqueue([&]() {
    collectLayerData(LayerType::kObjects, object_element_items_,
                     network_mesh.road_objects_mesh.indices,
                     &network_mesh.road_objects_mesh);
  }));
  futures.push_back(pool.Enqueue([&]() {
    collectLayerData(
        LayerType::kFacilities, facility_element_items_,
        facility_mesh_ ? facility_mesh_->indices : std::vector<uint32_t>{},
        facility_mesh_.get());
  }));
  futures.push_back(pool.Enqueue([&]() {
    // Junction groups need a specialized predicate: individual junctions use
    // "J:group_id:junction_id" keys which the generic predicate doesn't check.
    // We only hide a group's mesh if the group itself is hidden OR ALL its
    // individual junctions are hidden.
    const SceneLayerIndexResult result = BuildSceneLayerIndex(
        junction_element_items_, junction_mesh.indices,
        gl_renderer_->GetLayerVertexOffset(LayerType::kJunctions),
        junction_mesh, [this](const SceneCachedElement& element) {
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
    std::size_t estimated = 0;
    for (const auto& el : signal_element_items_) {
      if (el.group_key.find(":light") == std::string::npos) continue;
      if (hidden_elements_.count(el.road_key) ||
          hidden_elements_.count(el.group_key) ||
          hidden_elements_.count(el.element_key)) {
        continue;
      }
      for (const auto& range : el.ranges) {
        estimated += static_cast<std::size_t>(range.count) * 3;
      }
    }
    indices.reserve(estimated);
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
              network_mesh.road_signals_mesh.indices[range.start * 3 + k] +
              static_cast<uint32_t>(v_offset));
        }
      }
    }
    layerData[static_cast<int>(LayerType::kSignalLights)].indices =
        std::move(indices);
  }));
  futures.push_back(pool.Enqueue([&]() {
    std::vector<uint32_t> indices;
    std::size_t estimated = 0;
    for (const auto& el : signal_element_items_) {
      if (el.group_key.find(":sign") == std::string::npos) continue;
      if (hidden_elements_.count(el.road_key) ||
          hidden_elements_.count(el.group_key) ||
          hidden_elements_.count(el.element_key)) {
        continue;
      }
      for (const auto& range : el.ranges) {
        estimated += static_cast<std::size_t>(range.count) * 3;
      }
    }
    indices.reserve(estimated);
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
              network_mesh.road_signals_mesh.indices[range.start * 3 + k] +
              static_cast<uint32_t>(v_offset));
        }
      }
    }
    layerData[static_cast<int>(LayerType::kSignalSigns)].indices =
        std::move(indices);
  }));

  for (auto& f : futures) f.get();

  bool was_current = (QOpenGLContext::currentContext() == context());
  if (!was_current) makeCurrent();

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
    std::size_t estimated_solid = 0;
    for (const auto& el : outline_element_items_) {
      if (hidden_elements_.count(el.road_key) ||
          hidden_elements_.count(el.group_key) ||
          hidden_elements_.count(el.element_key)) {
        continue;
      }
      if (el.is_dashed) continue;
      std::size_t count = 0;
      for (const auto& range : el.ranges) {
        count += static_cast<std::size_t>(range.count) * 2;
      }
      estimated_solid += count;
    }
    solid_indices.reserve(estimated_solid);
    size_t v_offset = gl_renderer_->GetLayerVertexOffset(LayerType::kLanes);

    for (const auto& el : outline_element_items_) {
      if (hidden_elements_.count(el.road_key)) continue;
      if (hidden_elements_.count(el.group_key)) continue;
      if (hidden_elements_.count(el.element_key)) continue;
      if (el.is_dashed) continue;

      for (const auto& range : el.ranges) {
        for (uint32_t k = 0; k < range.count * 2; ++k) {
          solid_indices.push_back(static_cast<uint32_t>(
                               lane_outline_indices_[range.start * 2 + k]) +
                           static_cast<uint32_t>(v_offset));
        }
      }
    }

    auto SetupLineLayer = [&](LayerType t,
                              const std::vector<uint32_t>& indices) {
      gl_renderer_->SetLayerChunks(
          t,
          BuildSceneMeshChunks(indices, gl_renderer_->GetLayerVertexOffset(t),
                               network_mesh_->lanes_mesh));
      gl_renderer_->UploadLayerIndices(t, indices);
    };
    SetupLineLayer(LayerType::kLaneLines, solid_indices);
    SetupLineLayer(LayerType::kLaneLinesDashed, {});
  }

  // Reference Lines
  {
    std::vector<uint32_t> ref_line_indices;
    ref_line_indices.reserve(network_mesh_->lanes_mesh.vertices.size());
    bool layer_visible =
        gl_renderer_->IsLayerVisible(LayerType::kReferenceLines);
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

  if (!was_current) doneCurrent();
  update();
}

QMatrix4x4 GeoViewerWidget::GetViewMatrix() const {
  return camera_.GetViewMatrix();
}
