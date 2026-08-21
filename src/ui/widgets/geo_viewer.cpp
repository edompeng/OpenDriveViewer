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
#include "src/core/coordinate_util.h"
#include "src/core/thread_pool.h"
#include "src/logic/scene_index_builder.h"
#include "src/logic/simulation_controller.h"
#include "src/logic/spatial_index.h"

GeoViewerWidget::GeoViewerWidget(QWidget* parent)
    : QOpenGLWidget(parent),
      right_hand_traffic_(true),
      scene_min_bound_(std::numeric_limits<float>::max(),
                       std::numeric_limits<float>::max(),
                       std::numeric_limits<float>::max()),
      scene_max_bound_(std::numeric_limits<float>::lowest(),
                       std::numeric_limits<float>::lowest(),
                       std::numeric_limits<float>::lowest()) {
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);

  for (int i = 0; i < static_cast<int>(LayerType::kCount); ++i) {
    layer_visibility_[i] = true;
  }

  network_mesh_ = std::make_shared<odr::RoadNetworkMesh>();
  junction_mesh_ = std::make_shared<odr::Mesh3D>();
}

GeoViewerWidget::~GeoViewerWidget() {
  spatial_index_generation_++;
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
    user_points_buffer_dirty_ = true;
  }
  update();
  emit UserPointsChanged();
}

void GeoViewerWidget::BeginUserPointsBatch() {
  if (user_points_batch_depth_ == 0) {
    current_point_group_id_ = next_point_group_id_++;
  }
  ++user_points_batch_depth_;
}

void GeoViewerWidget::EndUserPointsBatch() {
  if (user_points_batch_depth_ <= 0) return;
  --user_points_batch_depth_;
  if (user_points_batch_depth_ != 0 || !user_points_batch_dirty_) return;

  if (user_points_batch_buffer_dirty_) {
    user_points_buffer_dirty_ = true;
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
      layer_visibility_[static_cast<size_t>(LayerType::kLaneLinesDashed)] =
          visible;
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
                                   std::optional<double> alt,
                                   const std::optional<QVector3D>& color) {
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
    int gid = (user_points_batch_depth_ > 0) ? current_point_group_id_
                                             : next_point_group_id_++;
    UserPoint up(world_pos, lon, lat, *alt, gid);
    if (color.has_value()) up.color = *color;
    user_points_.push_back(up);
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

    if (spatial_index_ready_) {
      const auto hits = RaycastAllHits(
          spatial_index_data_, ray_origin, ray_dir,
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
        int gid = (user_points_batch_depth_ > 0) ? current_point_group_id_
                                                 : next_point_group_id_++;
        UserPoint up(hit.position, p_lon, p_lat, p_alt, gid);
        if (color.has_value()) up.color = *color;
        user_points_.push_back(up);
      }
    }

    if (user_points_.size() == points_before) {
      // Fallback: place at ground level (Y=0) if no hit or grid not ready
      QVector3D world_pos(static_cast<float>(lx), 0.0f, static_cast<float>(ry));
      int gid = (user_points_batch_depth_ > 0) ? current_point_group_id_
                                               : next_point_group_id_++;
      UserPoint up(world_pos, lon, lat, 0.0, gid);
      if (color.has_value()) up.color = *color;
      user_points_.push_back(up);
    }
  }

  CommitUserPointsChange(true);
}

void GeoViewerWidget::RemoveUserPoint(int index) {
  if (index < 0 || index >= static_cast<int>(user_points_.size())) return;
  user_points_.erase(user_points_.begin() + index);
  CommitUserPointsChange(true);
}

void GeoViewerWidget::RemoveUserPointGroup(int group_id) {
  const auto new_end = std::remove_if(user_points_.begin(), user_points_.end(),
                                      [group_id](const UserPoint& point) {
                                        return point.group_id == group_id;
                                      });
  if (new_end == user_points_.end()) return;

  user_points_.erase(new_end, user_points_.end());
  CommitUserPointsChange(true);
}

void GeoViewerWidget::AddUserPointLocal(double x, double y,
                                        std::optional<double> z,
                                        const std::optional<QVector3D>& color) {
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
    int gid = (user_points_batch_depth_ > 0) ? current_point_group_id_
                                             : next_point_group_id_++;
    UserPoint up(world_pos, lon, lat, alt, gid);
    if (color.has_value()) up.color = *color;
    user_points_.push_back(up);
  } else {
    const size_t points_before = user_points_.size();
    constexpr float kRayStartHeight = 10000.0f;
    QVector3D ray_origin(static_cast<float>(x), kRayStartHeight,
                         static_cast<float>(ry));
    QVector3D ray_dir(0.0f, -1.0f, 0.0f);

    if (spatial_index_ready_) {
      const auto hits = RaycastAllHits(
          spatial_index_data_, ray_origin, ray_dir,
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
        int gid = (user_points_batch_depth_ > 0) ? current_point_group_id_
                                                 : next_point_group_id_++;
        UserPoint up(hit.position, lon, lat, alt, gid);
        if (color.has_value()) up.color = *color;
        user_points_.push_back(up);
      }
    }

    if (user_points_.size() == points_before) {
      QVector3D world_pos(static_cast<float>(x), 0.0f, static_cast<float>(ry));
      double lon = x, lat = y, alt = 0.0;
      resolve_lonlat(x, y, 0.0, lon, lat, alt);
      int gid = (user_points_batch_depth_ > 0) ? current_point_group_id_
                                               : next_point_group_id_++;
      UserPoint up(world_pos, lon, lat, alt, gid);
      if (color.has_value()) up.color = *color;
      user_points_.push_back(up);
    }
  }

  CommitUserPointsChange(true);
}

void GeoViewerWidget::SetUserPointVisible(int index, bool visible) {
  if (index < 0 || index >= static_cast<int>(user_points_.size())) return;
  if (user_points_[index].visible == visible) return;
  user_points_[index].visible = visible;
  CommitUserPointsChange(true);
}

void GeoViewerWidget::SetUserPointColor(int index, const QVector3D& color) {
  if (index < 0 || index >= static_cast<int>(user_points_.size())) return;
  if (user_points_[index].color == color) return;
  user_points_[index].color = color;
  CommitUserPointsChange(true);
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
  return UserPointSnapshot(p.lon, p.lat, p.alt, lx, ly, lz, p.visible, p.color,
                           p.group_id);
}

void GeoViewerWidget::UpdateUserPointsBuffers() {
  if (!gl_renderer_) return;

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
  ClearRefLineCache();
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

  std::array<size_t, kLayerCount> vertex_offsets;
  for (int i = 0; i < kLayerCount; ++i) {
    vertex_offsets[i] =
        gl_renderer_->GetLayerVertexOffset(static_cast<LayerType>(i));
  }

  auto collectLayerData = [this](
                              LayerType /*type*/, size_t vertex_offset,
                              const std::vector<SceneCachedElement>& elements,
                              const std::vector<uint32_t>& original_indices,
                              const odr::Mesh3D* base_mesh) -> LayerTasks {
    if (!base_mesh) return {};
    const SceneLayerIndexResult result = BuildSceneLayerIndex(
        elements, original_indices, vertex_offset, *base_mesh,
        [this](const SceneCachedElement& element) {
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
    return {result.indices, result.chunks};
  };

  auto& pool = geoviewer::utility::ThreadPool::Instance();
  std::vector<std::future<std::pair<LayerType, LayerTasks>>> futures;
  futures.reserve(7);

  size_t lanes_offset = vertex_offsets[static_cast<int>(LayerType::kLanes)];
  futures.push_back(pool.Enqueue([=, &network_mesh]() {
    auto tasks = collectLayerData(
        LayerType::kLanes, lanes_offset, lane_element_items_,
        network_mesh.lanes_mesh.indices, &network_mesh.lanes_mesh);
    return std::make_pair(LayerType::kLanes, std::move(tasks));
  }));

  size_t roadmarks_offset =
      vertex_offsets[static_cast<int>(LayerType::kRoadmarks)];
  futures.push_back(pool.Enqueue([=, &network_mesh]() {
    auto tasks = collectLayerData(
        LayerType::kRoadmarks, roadmarks_offset, roadmark_element_items_,
        network_mesh.roadmarks_mesh.indices, &network_mesh.roadmarks_mesh);
    return std::make_pair(LayerType::kRoadmarks, std::move(tasks));
  }));

  size_t objects_offset = vertex_offsets[static_cast<int>(LayerType::kObjects)];
  futures.push_back(pool.Enqueue([=, &network_mesh]() {
    auto tasks = collectLayerData(LayerType::kObjects, objects_offset,
                                  object_element_items_,
                                  network_mesh.road_objects_mesh.indices,
                                  &network_mesh.road_objects_mesh);
    return std::make_pair(LayerType::kObjects, std::move(tasks));
  }));

  size_t facilities_offset =
      vertex_offsets[static_cast<int>(LayerType::kFacilities)];
  auto facility_mesh_ptr = facility_mesh_.get();
  futures.push_back(pool.Enqueue([=]() {
    auto tasks = collectLayerData(LayerType::kFacilities, facilities_offset,
                                  facility_element_items_,
                                  facility_mesh_ptr ? facility_mesh_ptr->indices
                                                    : std::vector<uint32_t>{},
                                  facility_mesh_ptr);
    return std::make_pair(LayerType::kFacilities, std::move(tasks));
  }));

  size_t junctions_offset =
      vertex_offsets[static_cast<int>(LayerType::kJunctions)];
  futures.push_back(pool.Enqueue([=, &junction_mesh]() {
    const SceneLayerIndexResult result = BuildSceneLayerIndex(
        junction_element_items_, junction_mesh.indices, junctions_offset,
        junction_mesh, [this](const SceneCachedElement& element) {
          if (hidden_elements_.count(element.road_key)) return false;
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
    return std::make_pair(LayerType::kJunctions,
                          LayerTasks{result.indices, result.chunks});
  }));

  size_t signal_lights_offset =
      vertex_offsets[static_cast<int>(LayerType::kSignalLights)];
  futures.push_back(pool.Enqueue([=, &network_mesh]() {
    std::vector<uint32_t> indices;
    std::size_t estimated = 0;
    std::vector<const SceneCachedElement*> visible_elements;
    for (const auto& el : signal_element_items_) {
      if (el.group_key.find(":light") == std::string::npos) continue;
      if (hidden_elements_.count(el.road_key) ||
          hidden_elements_.count(el.group_key) ||
          hidden_elements_.count(el.element_key)) {
        continue;
      }
      visible_elements.push_back(&el);
      for (const auto& range : el.ranges) {
        estimated += static_cast<std::size_t>(range.count) * 3;
      }
    }
    indices.reserve(estimated);
    const auto& src_indices = network_mesh.road_signals_mesh.indices;
    for (const auto* el : visible_elements) {
      for (const auto& range : el->ranges) {
        const std::size_t base = static_cast<std::size_t>(range.start) * 3;
        for (uint32_t k = 0; k < range.count * 3; ++k) {
          indices.push_back(src_indices[base + k] +
                            static_cast<uint32_t>(signal_lights_offset));
        }
      }
    }
    auto chunks = BuildSceneMeshChunks(indices, signal_lights_offset,
                                       network_mesh.road_signals_mesh);
    return std::make_pair(LayerType::kSignalLights,
                          LayerTasks{std::move(indices), std::move(chunks)});
  }));

  size_t signal_signs_offset =
      vertex_offsets[static_cast<int>(LayerType::kSignalSigns)];
  futures.push_back(pool.Enqueue([=, &network_mesh]() {
    std::vector<uint32_t> indices;
    std::size_t estimated = 0;
    std::vector<const SceneCachedElement*> visible_elements;
    for (const auto& el : signal_element_items_) {
      if (el.group_key.find(":sign") == std::string::npos) continue;
      if (hidden_elements_.count(el.road_key) ||
          hidden_elements_.count(el.group_key) ||
          hidden_elements_.count(el.element_key)) {
        continue;
      }
      visible_elements.push_back(&el);
      for (const auto& range : el.ranges) {
        estimated += static_cast<std::size_t>(range.count) * 3;
      }
    }
    indices.reserve(estimated);
    const auto& src_indices = network_mesh.road_signals_mesh.indices;
    for (const auto* el : visible_elements) {
      for (const auto& range : el->ranges) {
        const std::size_t base = static_cast<std::size_t>(range.start) * 3;
        for (uint32_t k = 0; k < range.count * 3; ++k) {
          indices.push_back(src_indices[base + k] +
                            static_cast<uint32_t>(signal_signs_offset));
        }
      }
    }
    auto chunks = BuildSceneMeshChunks(indices, signal_signs_offset,
                                       network_mesh.road_signals_mesh);
    return std::make_pair(LayerType::kSignalSigns,
                          LayerTasks{std::move(indices), std::move(chunks)});
  }));

  for (auto& f : futures) {
    auto res = f.get();
    layerData[static_cast<int>(res.first)] = std::move(res.second);
  }

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
    std::vector<const SceneOutlineElement*> visible_outlines;
    visible_outlines.reserve(outline_element_items_.size());
    std::size_t estimated_solid = 0;
    for (const auto& el : outline_element_items_) {
      if (el.is_dashed) continue;
      if (hidden_elements_.count(el.road_key) ||
          hidden_elements_.count(el.group_key) ||
          hidden_elements_.count(el.element_key)) {
        continue;
      }
      visible_outlines.push_back(&el);
      for (const auto& range : el.ranges) {
        estimated_solid += static_cast<std::size_t>(range.count) * 2;
      }
    }
    solid_indices.reserve(estimated_solid);
    size_t v_offset = gl_renderer_->GetLayerVertexOffset(LayerType::kLanes);

    for (const auto* el : visible_outlines) {
      for (const auto& range : el->ranges) {
        const std::size_t base = static_cast<std::size_t>(range.start) * 2;
        for (uint32_t k = 0; k < range.count * 2; ++k) {
          solid_indices.push_back(
              static_cast<uint32_t>(lane_outline_indices_[base + k]) +
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
