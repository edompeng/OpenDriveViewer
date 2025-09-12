#include "src/app/geo_viewer.h"
#include <QContextMenuEvent>
#include <QDebug>
#include <QFileInfo>
#include <QFuture>
#include <QMatrix4x4>
#include <QPainter>
#include <QPolygonF>
#include <QRegularExpression>
#include <QStringList>
#include <QThread>
#include <QtConcurrent>
#include <QtMath>
#include <algorithm>
#include "src/app/scene_index_builder.h"
#include "src/app/spatial_grid_index.h"
#include "src/utility/coordinate_util.h"
#include "src/utility/viewer_text_util.h"

GeoViewerWidget::GeoViewerWidget(QWidget* parent)
    : QOpenGLWidget(parent),
      vao_(0),
      vbo_(0),
      shader_program_(0),
      right_hand_traffic_(true) {
  spatial_grid_watcher_ = new QFutureWatcher<std::vector<SceneGridBox>>(this);
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  for (int i = 0; i < kLayerCount; ++i) {
    layers_[i].ebo = 0;
    layers_[i].index_count = 0;
    layers_[i].visible = true;
    layers_[i].polygon_offset_factor = 0.0f;
    layers_[i].polygon_offset_units = 0.0f;
    layers_[i].alpha = 1.0f;
    layers_[i].draw_mode = GL_TRIANGLES;
  }
  layers_[(int)LayerType::kRouting].color = QVector3D(0.0f, 1.0f, 0.5f);
  layers_[(int)LayerType::kRouting].alpha = 0.8f;
}

GeoViewerWidget::~GeoViewerWidget() {
  if (spatial_grid_watcher_) {
    spatial_grid_watcher_->cancel();
    spatial_grid_watcher_->waitForFinished();
  }
  makeCurrent();
  glDeleteBuffers(1, &vbo_);
  // highlight_mgr_ automatically releases its EBO via unique_ptr
  for (int i = 0; i < kLayerCount; ++i) {
    if (layers_[i].ebo) glDeleteBuffers(1, &layers_[i].ebo);
  }
  if (measure_vbo_) glDeleteBuffers(1, &measure_vbo_);
  if (measure_vao_) glDeleteVertexArrays(1, &measure_vao_);
  glDeleteVertexArrays(1, &vao_);
  glDeleteProgram(shader_program_);
  doneCurrent();
}

void GeoViewerWidget::SetLayerVisible(LayerType type, bool visible) {
  if (type >= LayerType::kLanes && type < LayerType::kCount) {
    layers_[(int)type].visible = visible;
    if (!visible && highlight_mgr_ && highlight_mgr_->cur_layer == type) {
      ClearHighlight();
    }
    update();
  }
}

bool GeoViewerWidget::IsLayerVisible(LayerType type) const {
  if (type >= LayerType::kLanes && type < LayerType::kCount) {
    return layers_[(int)type].visible;
  }
  return false;
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
  emit elementVisibilityChanged(id, visible);
}

bool GeoViewerWidget::IsElementVisible(const QString& id) const {
  return hidden_elements_.find(id.toStdString()) == hidden_elements_.end();
}

bool GeoViewerWidget::IsElementActuallyVisible(
    const std::string& roadId, const std::string& group,
    const std::string& elementId) const {
  if (hidden_elements_.count("R:" + roadId)) return false;
  if (!group.empty()) {
    if (hidden_elements_.count("G:" + roadId + ":" + group)) return false;
    std::string fullId = "E:" + roadId + ":" + group;
    if (!elementId.empty()) fullId += ":" + elementId;
    if (hidden_elements_.count(fullId)) return false;
  }
  return true;
}

void GeoViewerWidget::AddUserPoint(double lon, double lat,
                                   std::optional<double> alt) {
  if (!map_) return;

  double lx, ly, lz;
  lx = lon;
  ly = lat;
  lz = alt.value_or(0.0);
  CoordinateUtil::Instance().WGS84ToLocal(&lx, &ly, &lz);

  if (alt.has_value()) {
    // Direct placement: convert local coords to renderer coords
    // Renderer coords: X -> lx, Y -> lz, Z -> ly (mirrored if RHT)
    double ry = ly;
    if (right_hand_traffic_) ry = -ry;
    QVector3D worldPos(static_cast<float>(lx), static_cast<float>(lz),
                       static_cast<float>(ry));
    user_points_.push_back({worldPos, lon, lat, *alt});
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
        user_points_.push_back({hit.position, p_lon, p_lat, p_alt});
      }
    }

    if (user_points_.size() == points_before) {
      // Fallback: place at ground level (Y=0) if no hit or grid not ready
      QVector3D worldPos(static_cast<float>(lx), 0.0f, static_cast<float>(ry));
      user_points_.push_back({worldPos, lon, lat, 0.0});
    }
  }

  UpdateUserPointsBuffers();
  update();
  emit userPointsChanged();
}

void GeoViewerWidget::RemoveUserPoint(int index) {
  if (index < 0 || index >= static_cast<int>(user_points_.size())) return;
  user_points_.erase(user_points_.begin() + index);
  UpdateUserPointsBuffers();
  update();
  emit userPointsChanged();
}

void GeoViewerWidget::SetUserPointVisible(int index, bool visible) {
  if (index < 0 || index >= static_cast<int>(user_points_.size())) return;
  user_points_[index].visible = visible;
  UpdateUserPointsBuffers();
  update();
}

void GeoViewerWidget::SetUserPointColor(int index, const QVector3D& color) {
  if (index < 0 || index >= static_cast<int>(user_points_.size())) return;
  user_points_[index].color = color;
  update();  // No buffer rebuild needed; color is applied via uniform
}

void GeoViewerWidget::ClearUserPoints() {
  user_points_.clear();
  UpdateUserPointsBuffers();
  update();
  emit userPointsChanged();
}

int GeoViewerWidget::UserPointCount() const {
  return static_cast<int>(user_points_.size());
}

GeoViewerWidget::UserPointSnapshot GeoViewerWidget::GetUserPointSnapshot(
    int index) const {
  if (index < 0 || index >= static_cast<int>(user_points_.size())) {
    return {0.0, 0.0, 0.0, false, {1.0f, 0.3f, 0.3f}};
  }
  const auto& p = user_points_[index];
  return {p.lon, p.lat, p.alt, p.visible, p.color};
}

void GeoViewerWidget::UpdateUserPointsBuffers() {
  makeCurrent();
  if (!user_points_vao_) {
    glGenVertexArrays(1, &user_points_vao_);
    glGenBuffers(1, &user_points_vbo_);
  }

  // Upload all point positions (visible or not) — visibility is handled
  // during draw calls by skipping invisible points individually.
  std::vector<float> data;
  for (const auto& p : user_points_) {
    data.push_back(p.worldPos.x());
    data.push_back(p.worldPos.y());
    data.push_back(p.worldPos.z());
  }

  glBindVertexArray(user_points_vao_);
  glBindBuffer(GL_ARRAY_BUFFER, user_points_vbo_);
  glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), data.data(),
               GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
  glEnableVertexAttribArray(0);
  glBindVertexArray(0);
  doneCurrent();
}

void GeoViewerWidget::UpdateMeasureBuffers() {
  if (!measure_ctrl_) return;
  const auto& points = measure_ctrl_->Points();

  makeCurrent();
  if (!measure_vao_) {
    glGenVertexArrays(1, &measure_vao_);
    glGenBuffers(1, &measure_vbo_);
  }

  glBindVertexArray(measure_vao_);
  glBindBuffer(GL_ARRAY_BUFFER, measure_vbo_);
  glBufferData(GL_ARRAY_BUFFER, points.size() * sizeof(QVector3D),
               points.data(), GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(QVector3D), nullptr);
  glBindVertexArray(0);
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
  if (!map_) return;
  makeCurrent();

  struct LayerTasks {
    std::vector<uint32_t> indices;
    std::vector<SceneMeshChunk> chunks;
  };
  std::array<LayerTasks, kLayerCount> layerData;

  auto collectLayerData = [&](LayerType type,
                              const std::vector<SceneCachedElement>& elements,
                              const std::vector<uint32_t>& originalIndices,
                              const odr::Mesh3D* baseMesh) {
    if (!baseMesh) return;
    const SceneLayerIndexResult result = BuildSceneLayerIndex(
        elements, originalIndices, layers_[(int)type].vertex_offset, *baseMesh,
        [this](const SceneCachedElement& element) {
          if (hidden_elements_.count(element.roadKey)) return false;
          if (!element.groupKey.empty() &&
              hidden_elements_.count(element.groupKey)) {
            return false;
          }
          if (!element.elementKey.empty() &&
              hidden_elements_.count(element.elementKey)) {
            return false;
          }
          return true;
        });
    layerData[(int)type] = {result.indices, result.chunks};
  };

  std::vector<QFuture<void>> futures;
  futures.push_back(QtConcurrent::run([&]() {
    collectLayerData(LayerType::kLanes, lane_element_items_,
                     network_mesh_.lanes_mesh.indices,
                     &network_mesh_.lanes_mesh);
  }));
  futures.push_back(QtConcurrent::run([&]() {
    collectLayerData(LayerType::kRoadmarks, roadmark_element_items_,
                     network_mesh_.roadmarks_mesh.indices,
                     &network_mesh_.roadmarks_mesh);
  }));
  futures.push_back(QtConcurrent::run([&]() {
    collectLayerData(LayerType::kObjects, object_element_items_,
                     network_mesh_.road_objects_mesh.indices,
                     &network_mesh_.road_objects_mesh);
  }));
  futures.push_back(QtConcurrent::run([&]() {
    // Junction groups need a specialized predicate: individual junctions use
    // "J:groupId:junctionId" keys which the generic predicate doesn't check.
    // We only hide a group's mesh if the group itself is hidden OR ALL its
    // individual junctions are hidden.
    const SceneLayerIndexResult result = BuildSceneLayerIndex(
        junction_element_items_, junction_mesh_.indices,
        layers_[(int)LayerType::kJunctions].vertex_offset, junction_mesh_,
        [this](const SceneCachedElement& element) {
          // Check group-level visibility (JG:groupId)
          if (hidden_elements_.count(element.roadKey)) return false;

          // Extract groupId from roadKey "JG:xxx"
          if (element.roadKey.size() <= 3) return true;
          std::string groupId = element.roadKey.substr(3);

          auto groupIt = junction_group_index_by_id_.find(groupId);
          if (groupIt != junction_group_index_by_id_.end()) {
            const auto& group =
                junction_cluster_result_.groups[groupIt->second];
            if (!group.junction_ids.empty()) {
              bool all_children_hidden = true;
              for (const auto& jid : group.junction_ids) {
                std::string jKey = "J:" + groupId + ":" + jid;
                if (hidden_elements_.count(jKey) == 0) {
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
    layerData[(int)LayerType::kJunctions] = {result.indices, result.chunks};
  }));
  futures.push_back(QtConcurrent::run([&]() {
    std::vector<uint32_t> indices;
    size_t vOffset = layers_[(int)LayerType::kSignalLights].vertex_offset;
    for (const auto& el : signal_element_items_) {
      if (el.groupKey.find(":light") == std::string::npos) continue;
      if (hidden_elements_.count(el.roadKey) ||
          hidden_elements_.count(el.groupKey) ||
          hidden_elements_.count(el.elementKey))
        continue;
      for (const auto& range : el.ranges) {
        for (uint32_t k = 0; k < range.count * 3; ++k) {
          indices.push_back(
              network_mesh_.road_signals_mesh.indices[range.start * 3 + k] +
              (uint32_t)vOffset);
        }
      }
    }
    layerData[(int)LayerType::kSignalLights].indices = std::move(indices);
  }));
  futures.push_back(QtConcurrent::run([&]() {
    std::vector<uint32_t> indices;
    size_t vOffset = layers_[(int)LayerType::kSignalSigns].vertex_offset;
    for (const auto& el : signal_element_items_) {
      if (el.groupKey.find(":sign") == std::string::npos) continue;
      if (hidden_elements_.count(el.roadKey) ||
          hidden_elements_.count(el.groupKey) ||
          hidden_elements_.count(el.elementKey))
        continue;
      for (const auto& range : el.ranges) {
        for (uint32_t k = 0; k < range.count * 3; ++k) {
          indices.push_back(
              network_mesh_.road_signals_mesh.indices[range.start * 3 + k] +
              (uint32_t)vOffset);
        }
      }
    }
    layerData[(int)LayerType::kSignalSigns].indices = std::move(indices);
  }));

  for (auto& f : futures) f.waitForFinished();

  makeCurrent();
  for (int typeIndex = 0; typeIndex < kLayerCount; ++typeIndex) {
    auto& data = layerData[typeIndex];
    LayerType type = static_cast<LayerType>(typeIndex);

    // Fix EBO update bug: allow update for layers that can be cleared
    // Fix EBO update bug: allow data upload even for empty indices for major
    // layers so they are cleared from the screen when unchecked.
    if (data.indices.empty() && data.chunks.empty()) {
      const bool isMajorLayer =
          (type == LayerType::kLanes || type == LayerType::kRoadmarks ||
           type == LayerType::kObjects || type == LayerType::kJunctions ||
           type == LayerType::kSignalLights ||
           type == LayerType::kSignalSigns || type == LayerType::kLaneLines ||
           type == LayerType::kLaneLinesDashed ||
           type == LayerType::kReferenceLines);
      if (!isMajorLayer) continue;
    }

    if (!layers_[(int)type].ebo) glGenBuffers(1, &layers_[(int)type].ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, layers_[(int)type].ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 data.indices.size() * sizeof(uint32_t), data.indices.data(),
                 GL_STATIC_DRAW);
    layers_[(int)type].index_count = data.indices.size();
    layers_[(int)type].chunks = std::move(data.chunks);
  }

  // kLaneLines
  {
    std::vector<uint32_t> solidIndices;
    std::vector<uint32_t> dashedIndices;
    size_t vOffset = layers_[(int)LayerType::kLanes].vertex_offset;

    for (const auto& el : outline_element_items_) {
      if (hidden_elements_.count(el.roadKey)) continue;
      if (hidden_elements_.count(el.groupKey)) continue;
      if (hidden_elements_.count(el.elementKey)) continue;

      auto& target = el.isDashed ? dashedIndices : solidIndices;
      for (const auto& range : el.ranges) {
        for (uint32_t k = 0; k < range.count * 2; ++k) {
          target.push_back(
              (uint32_t)lane_outline_indices_[range.start * 2 + k] +
              (uint32_t)vOffset);
        }
      }
    }

    auto setupLineLayer = [&](LayerType t,
                              const std::vector<uint32_t>& indices) {
      layers_[(int)t].chunks = BuildSceneMeshChunks(
          indices, layers_[(int)t].vertex_offset, network_mesh_.lanes_mesh);
      int ltype = (int)t;
      if (!layers_[ltype].ebo) glGenBuffers(1, &layers_[ltype].ebo);
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, layers_[ltype].ebo);
      glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t),
                   indices.data(), GL_STATIC_DRAW);
      layers_[ltype].index_count = indices.size();
    };
    setupLineLayer(LayerType::kLaneLines, solidIndices);
    setupLineLayer(LayerType::kLaneLinesDashed, dashedIndices);
  }

  // Reference Lines
  {
    std::vector<uint32_t> refLineIndices;
    bool layerVisible = layers_[(int)LayerType::kReferenceLines].visible;
    for (const auto& [roadId, road] : map_->id_to_road) {
      if (layerVisible && IsElementActuallyVisible(roadId, "refline", "")) {
        if (road_ref_line_vert_ranges_.count(roadId)) {
          const auto& range = road_ref_line_vert_ranges_.at(roadId);
          for (size_t i = 0; i < range.count; ++i) {
            refLineIndices.push_back((uint32_t)(range.start + i));
          }
        }
      }
    }

    auto setupRefLineLayer = [&](LayerType t,
                                 const std::vector<uint32_t>& indices) {
      int ltype = (int)t;
      if (!layers_[ltype].ebo) glGenBuffers(1, &layers_[ltype].ebo);
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, layers_[ltype].ebo);
      glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t),
                   indices.data(), GL_STATIC_DRAW);
      layers_[ltype].index_count = indices.size();
      layers_[ltype].chunks.clear();
    };
    setupRefLineLayer(LayerType::kReferenceLines, refLineIndices);
  }

  doneCurrent();
  update();
}

QMatrix4x4 GeoViewerWidget::GetViewMatrix() const {
  return camera_.GetViewMatrix();
}

void GeoViewerWidget::initializeGL() {
  initializeOpenGLFunctions();
  if (!InitializeRendering()) {
    qCritical() << "Failed to initialize rendering";
  }
}

void GeoViewerWidget::resizeGL(int w, int h) {
  viewport_size_ = QSize(w, h);
  glViewport(0, 0, w, h);
}

void GeoViewerWidget::paintGL() {
  if (needs_index_update_ && batch_update_count_ == 0) {
    UpdateMeshIndices();
  }
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  if (!mesh_updated_) return;

  glUseProgram(shader_program_);

  QMatrix4x4 model;
  model.setToIdentity();
  glUniformMatrix4fv(glGetUniformLocation(shader_program_, "model"), 1,
                     GL_FALSE, model.data());

  QMatrix4x4 view = GetViewMatrix();
  glUniformMatrix4fv(glGetUniformLocation(shader_program_, "view"), 1, GL_FALSE,
                     view.data());

  float aspect = (float)viewport_size_.width() / (float)viewport_size_.height();

  const float distance = camera_.GetDistance();
  const float meshRadius = camera_.MeshRadius();

  // 1. Ensure the near plane is always smaller than the viewing distance to
  // prevent clipping the current view. Also set a minimum near plane to prevent
  // large ratios with the far plane which leads to float precision issues.
  float nearPlane = qMax(0.1f, distance * 0.01f);

  // 2. Calculate the ideal far plane (including the whole model)
  float farPlane = distance + meshRadius * 2.0f + 1000.0f;

  // 3. Depth buffer precision limit: if the ratio exceeds one million times,
  // increase the near plane instead of shrinking the far plane. This ensures
  // distant objects won't be clipped (producing black shadows) due to ratio
  // limits.
  const float maxRatio = 1000000.0f;
  if (farPlane / nearPlane > maxRatio) {
    nearPlane = farPlane / maxRatio;
  }

  // Ensure the near plane is not pushed too far, causing clipping of the
  // target.
  if (nearPlane > distance * 0.5f) {
    nearPlane = distance * 0.5f;
    farPlane = nearPlane * maxRatio;
  }

  proj_.setToIdentity();
  proj_.perspective(45.0f, aspect, nearPlane, farPlane);
  glUniformMatrix4fv(glGetUniformLocation(shader_program_, "projection"), 1,
                     GL_FALSE, proj_.data());

  GLint colorLoc = glGetUniformLocation(shader_program_, "objectColor");
  GLint alphaLoc = glGetUniformLocation(shader_program_, "alpha");
  GLint dashedLoc = glGetUniformLocation(shader_program_, "isDashed");

  glBindVertexArray(vao_);

  // Draw each layer
  for (int i = 0; i < (int)LayerType::kCount; ++i) {
    if (!layers_[i].visible || layers_[i].index_count == 0 || !layers_[i].ebo)
      continue;

    if (layers_[i].draw_mode == GL_LINES) {
      glLineWidth(2.0f);
    }

    if (layers_[i].polygon_offset_factor != 0.0f ||
        layers_[i].polygon_offset_units != 0.0f) {
      glEnable(GL_POLYGON_OFFSET_FILL);
      glEnable(GL_POLYGON_OFFSET_LINE);
      glPolygonOffset(layers_[i].polygon_offset_factor,
                      layers_[i].polygon_offset_units);
    }

    glUniform3f(colorLoc, layers_[i].color.x(), layers_[i].color.y(),
                layers_[i].color.z());
    glUniform1f(alphaLoc, layers_[i].alpha);
    glUniform1i(dashedLoc, (i == (int)LayerType::kLaneLinesDashed) ? 1 : 0);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, layers_[i].ebo);

    if (layers_[i].chunks.empty()) {
      glDrawElements(layers_[i].draw_mode, layers_[i].index_count,
                     GL_UNSIGNED_INT, 0);
    } else {
      QMatrix4x4 vp = proj_ * view;
      auto isAABBVisible = [&](const QVector3D& minB, const QVector3D& maxB) {
        QVector3D corners[8] = {
            {minB.x(), minB.y(), minB.z()}, {maxB.x(), minB.y(), minB.z()},
            {minB.x(), maxB.y(), minB.z()}, {maxB.x(), maxB.y(), minB.z()},
            {minB.x(), minB.y(), maxB.z()}, {maxB.x(), minB.y(), maxB.z()},
            {minB.x(), maxB.y(), maxB.z()}, {maxB.x(), maxB.y(), maxB.z()}};

        bool allOut[6] = {true, true, true, true, true, true};
        for (int c = 0; c < 8; ++c) {
          QVector4D pt(corners[c], 1.0f);
          pt = vp * pt;

          if (pt.w() >
              0) {  // Only judge if the point is in front of the camera
            if (pt.x() >= -pt.w()) allOut[0] = false;
            if (pt.x() <= pt.w()) allOut[1] = false;
            if (pt.y() >= -pt.w()) allOut[2] = false;
            if (pt.y() <= pt.w()) allOut[3] = false;
            if (pt.z() >= -pt.w()) allOut[4] = false;
            if (pt.z() <= pt.w()) allOut[5] = false;
          } else {
            // If the point is behind the camera, we cannot simply assume it's
            // outside. For safety, if the AABB spans the w=0 plane, we skip
            // clipping for now.
            return true;
          }
        }
        for (int i = 0; i < 6; ++i) {
          if (allOut[i]) return false;
        }
        return true;
      };

      for (const auto& chunk : layers_[i].chunks) {
        if (isAABBVisible(chunk.min_bound, chunk.max_bound)) {
          glDrawElements(
              layers_[i].draw_mode, chunk.index_count, GL_UNSIGNED_INT,
              (void*)(intptr_t)(chunk.index_offset * sizeof(uint32_t)));
        }
      }
    }

    if (layers_[i].polygon_offset_factor != 0.0f ||
        layers_[i].polygon_offset_units != 0.0f) {
      glDisable(GL_POLYGON_OFFSET_FILL);
      glDisable(GL_POLYGON_OFFSET_LINE);
    }
  }

  // Draw highlighting
  if (highlight_mgr_ && highlight_mgr_->HasHighlight()) {
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-2.0f, -2.0f);  // Higher than roadmarks
    glUniform3f(colorLoc, 0.2f, 0.85f, 0.4f);
    glUniform1f(alphaLoc, 1.0f);
    glUniform1i(dashedLoc, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, highlight_mgr_->Primary().ebo);
    glDrawElements(GL_TRIANGLES,
                   static_cast<GLsizei>(highlight_mgr_->Primary().count),
                   GL_UNSIGNED_INT, nullptr);
    glDisable(GL_POLYGON_OFFSET_FILL);
  }

  // Draw neighbor highlighting (orange)
  if (highlight_mgr_ && highlight_mgr_->HasNeighborHighlight()) {
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.8f, -1.8f);  // Slightly lower than primary highlight
    glUniform3f(colorLoc, 1.0f, 0.5f, 0.0f);  // Orange
    glUniform1f(alphaLoc, 0.8f);
    glUniform1i(dashedLoc, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, highlight_mgr_->Neighbor().ebo);
    glDrawElements(GL_TRIANGLES,
                   static_cast<GLsizei>(highlight_mgr_->Neighbor().count),
                   GL_UNSIGNED_INT, nullptr);
    glDisable(GL_POLYGON_OFFSET_FILL);
  }

  // Draw user-added points - highest priority, point-by-point drawing supports
  // independent colors
  if (!user_points_.empty() && user_points_vao_) {
    glDisable(GL_DEPTH_TEST);
    glPointSize(10.0f);
    glUniform1f(alphaLoc, 1.0f);
    glUniform1i(dashedLoc, 0);
    glBindVertexArray(user_points_vao_);
    for (int i = 0; i < static_cast<int>(user_points_.size()); ++i) {
      if (!user_points_[i].visible) continue;
      const auto& c = user_points_[i].color;
      glUniform3f(colorLoc, c.x(), c.y(), c.z());
      glDrawArrays(GL_POINTS, i, 1);
    }
    glEnable(GL_DEPTH_TEST);
  }

  // Draw routing results
  if (routing_buf_mgr_) {
    for (const auto& [id, route] : routing_buf_mgr_->Routes()) {
      if (route.visible && route.index_count > 0 && route.vao) {
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-3.0f, -3.0f);  // Higher than highlights
        glUniform3f(colorLoc, layers_[(int)LayerType::kRouting].color.x(),
                    layers_[(int)LayerType::kRouting].color.y(),
                    layers_[(int)LayerType::kRouting].color.z());
        glUniform1f(alphaLoc, layers_[(int)LayerType::kRouting].alpha);
        glUniform1i(dashedLoc, 0);
        glBindVertexArray(route.vao);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(route.index_count),
                       GL_UNSIGNED_INT, nullptr);
        glDisable(GL_POLYGON_OFFSET_FILL);
      }
    }
  }

  // Draw measurement lines and points
  if (measure_ctrl_ && !measure_ctrl_->Points().empty() && measure_vao_) {
    glDisable(GL_DEPTH_TEST);
    glLineWidth(3.0f);
    glPointSize(8.0f);
    glUniform3f(colorLoc, 1.0f, 1.0f, 0.2f);  // Yellow
    glUniform1f(alphaLoc, 1.0f);
    glUniform1i(dashedLoc, 0);
    glBindVertexArray(measure_vao_);
    if (measure_ctrl_->Points().size() >= 2) {
      glDrawArrays(GL_LINE_STRIP, 0,
                   static_cast<GLsizei>(measure_ctrl_->Points().size()));
    }
    glDrawArrays(GL_POINTS, 0,
                 static_cast<GLsizei>(measure_ctrl_->Points().size()));
    glEnable(GL_DEPTH_TEST);
  }

  glBindVertexArray(0);

  // Render distance labels
  const bool hasMeasurePoints =
      measure_ctrl_ && !measure_ctrl_->Points().empty();
  if (hasMeasurePoints) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::yellow);
    QFont font = painter.font();
    font.setBold(true);
    font.setPointSize(10);
    painter.setFont(font);

    const QMatrix4x4 viewProj = proj_ * view;
    const auto& pts = measure_ctrl_->Points();

    for (size_t i = 1; i < pts.size(); ++i) {
      const QVector3D p1 = pts[i - 1];
      const QVector3D p2 = pts[i];
      const float dist = p1.distanceToPoint(p2);
      const QVector3D mid = (p1 + p2) * 0.5f;

      const QVector4D clipPos = viewProj * QVector4D(mid, 1.0f);
      if (clipPos.w() > 0) {
        const float ndcX = clipPos.x() / clipPos.w();
        const float ndcY = clipPos.y() / clipPos.w();
        const int sx = (int)((ndcX + 1.0f) * width() * 0.5f);
        const int sy = (int)((1.0f - ndcY) * height() * 0.5f);
        painter.drawText(sx + 5, sy - 5, QString("%1m").arg(dist, 0, 'f', 2));
      }
    }
    RenderJunctionOverlay(painter, proj_ * view);
    RenderOpenScenarioOverlay(painter, proj_ * view);
    painter.end();
  } else {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    RenderJunctionOverlay(painter, proj_ * view);
    RenderOpenScenarioOverlay(painter, proj_ * view);
    painter.end();
  }
}

void GeoViewerWidget::mousePressEvent(QMouseEvent* ev) {
  if (measure_ctrl_ && measure_ctrl_->IsActive()) {
    if (ev->button() == Qt::LeftButton) {
      QVector3D worldPos;
      std::optional<PickResult> pickedIdx;
      if (GetWorldPosAt((int)ev->position().x(), (int)ev->position().y(),
                        worldPos, pickedIdx)) {
        if (pickedIdx.has_value() && pickedIdx->layer == LayerType::kLanes) {
          measure_ctrl_->AddPoint(worldPos);
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
    QVector3D worldPos;
    std::optional<PickResult> pickedIdx;
    if (GetWorldPosAt((int)ev->position().x(), (int)ev->position().y(),
                      worldPos, pickedIdx) &&
        pickedIdx.has_value() && map_) {
      QString roadId, elementId;
      TreeNodeType nodeType = TreeNodeType::kRoad;
      size_t vi = pickedIdx->vertexIndex;

      if (pickedIdx->layer == LayerType::kLanes) {
        roadId =
            QString::fromStdString(network_mesh_.lanes_mesh.get_road_id(vi));
        double s0 = network_mesh_.lanes_mesh.get_lanesec_s0(vi);
        int laneId = network_mesh_.lanes_mesh.get_lane_id(vi);
        elementId = QString("%1:%2").arg(s0).arg(laneId);
        nodeType = TreeNodeType::kLane;
      } else if (pickedIdx->layer == LayerType::kObjects) {
        roadId = QString::fromStdString(
            network_mesh_.road_objects_mesh.get_road_id(vi));
        elementId = QString::fromStdString(
            network_mesh_.road_objects_mesh.get_road_object_id(vi));
        nodeType = TreeNodeType::kObject;
      } else if (pickedIdx->layer == LayerType::kSignalLights) {
        roadId = QString::fromStdString(
            network_mesh_.road_signals_mesh.get_road_id(vi));
        elementId = QString::fromStdString(
            network_mesh_.road_signals_mesh.get_road_signal_id(vi));
        nodeType = TreeNodeType::kLight;
      } else if (pickedIdx->layer == LayerType::kSignalSigns) {
        roadId = QString::fromStdString(
            network_mesh_.road_signals_mesh.get_road_id(vi));
        elementId = QString::fromStdString(
            network_mesh_.road_signals_mesh.get_road_signal_id(vi));
        nodeType = TreeNodeType::kSign;
      } else if (pickedIdx->layer == LayerType::kJunctions) {
        auto group_id = FindJunctionGroupByVertex(vi);
        if (group_id.has_value()) {
          roadId = QString::fromStdString(*group_id);
          elementId = QString::fromStdString(*group_id);
          nodeType = TreeNodeType::kJunctionGroup;
        }
      }

      if (!roadId.isEmpty()) {
        emit elementSelected(roadId, nodeType, elementId);
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

    QVector3D worldPos;
    std::optional<PickResult> pickedIdx;
    const bool hasPick = GetWorldPosAt(
        (int)ev->position().x(), (int)ev->position().y(), worldPos, pickedIdx);

    camera_.ZoomToward(wheelDelta, maxDist, worldPos, hasPick);
    update();
  }
  ev->accept();
}

void GeoViewerWidget::focusOutEvent(QFocusEvent* event) {
  camera_.EndDrag();
  QWidget::focusOutEvent(event);
}

bool GeoViewerWidget::InitializeRendering() {
  glClearColor(0.15f, 0.15f, 0.18f, 1.0f);
  glEnable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  // Initialize delegated components (Dependency Injection: pass OpenGL function
  // interface to each sub-component)
  auto* glFuncs = static_cast<QOpenGLExtraFunctions*>(this);
  highlight_mgr_ = std::make_unique<HighlightManager>(glFuncs);
  measure_ctrl_ = std::make_unique<MeasureToolController>(this);
  routing_buf_mgr_ = std::make_unique<RoutingBufferManager>(glFuncs);

  // Connect measurement signals
  connect(measure_ctrl_.get(), &MeasureToolController::pointsChanged, this,
          &GeoViewerWidget::UpdateMeasureBuffers);
  connect(measure_ctrl_.get(), &MeasureToolController::totalDistanceChanged,
          this, &GeoViewerWidget::totalDistanceChanged);
  connect(measure_ctrl_.get(), &MeasureToolController::activeChanged, this,
          &GeoViewerWidget::measureModeChanged);

  if (!InitShaders()) {
    return false;
  }
  InitBuffers();
  return true;
}

bool GeoViewerWidget::InitShaders() {
  const char* vertexShaderSource = R"(
        #version 330 core
        layout (location = 0) in vec3 aPos;
        uniform mat4 model;
        uniform mat4 view;
        uniform mat4 projection;
        out vec3 vWorldPos;
        void main() {
            vec4 worldPos = model * vec4(aPos, 1.0);
            vWorldPos = worldPos.xyz;
            gl_Position = projection * view * worldPos;
        }
    )";
  const char* fragmentShaderSource = R"(
    #version 330 core
    out vec4 FragColor;
    in vec3 vWorldPos;
    uniform vec3 objectColor;
    uniform float alpha;
    uniform bool isDashed;
    void main() {
        if (isDashed) {
            float d = length(vWorldPos.xy) * 2.0; 
            if (fract(d) > 0.5) discard;
        }
        FragColor = vec4(objectColor, alpha);
    }
    )";

  GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
  glCompileShader(vertexShader);

  GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
  glCompileShader(fragmentShader);

  if (!CheckShaderErrors(vertexShader, "VERTEX") ||
      !CheckShaderErrors(fragmentShader, "FRAGMENT")) {
    return false;
  }

  shader_program_ = glCreateProgram();
  glAttachShader(shader_program_, vertexShader);
  glAttachShader(shader_program_, fragmentShader);
  glLinkProgram(shader_program_);

  if (!CheckProgramErrors(shader_program_)) {
    return false;
  }
  glDeleteShader(vertexShader);
  glDeleteShader(fragmentShader);
  return true;
}

void GeoViewerWidget::InitBuffers() {
  glGenVertexArrays(1, &vao_);
  glBindVertexArray(vao_);
  glGenBuffers(1, &vbo_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);

  // Highlight buffers are managed by HighlightManager
  if (highlight_mgr_) {
    highlight_mgr_->Initialize();
  }

  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float),
                        (void*)nullptr);
  glEnableVertexAttribArray(0);
  glBindVertexArray(0);
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

bool GeoViewerWidget::CheckShaderErrors(GLuint shader, std::string type) {
  GLint success;
  GLchar infoLog[1024];
  glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
  if (!success) {
    glGetShaderInfoLog(shader, 1024, NULL, infoLog);
    qCritical() << "Shader error" << QString::fromStdString(type) << ":"
                << infoLog;
    return false;
  }
  return true;
}

bool GeoViewerWidget::CheckProgramErrors(GLuint program) {
  GLint success;
  GLchar infoLog[1024];
  glGetProgramiv(program, GL_LINK_STATUS, &success);
  if (!success) {
    glGetProgramInfoLog(program, 1024, NULL, infoLog);
    qCritical() << "Program link error:" << infoLog;
    return false;
  }
  return true;
}

// RayIntersectsTriangle and RayIntersectsAABB migrated to
// spatial_grid_index.cpp as free functions, no longer members of
// GeoViewerWidget

void GeoViewerWidget::StartSpatialGridBuild() {
  if (spatial_grid_watcher_) {
    spatial_grid_watcher_->cancel();
  }
  const std::uint64_t generation = ++spatial_grid_generation_;
  auto map = map_;
  const odr::RoadNetworkMesh* networkMesh = &network_mesh_;
  const odr::Mesh3D* junctionMesh = &junction_mesh_;
  const int gridResolution = grid_resolution_;

  connect(
      spatial_grid_watcher_,
      &QFutureWatcher<std::vector<SceneGridBox>>::finished, this,
      [this, generation]() {
        if (generation != spatial_grid_generation_) {
          return;
        }
        grid_boxes_ = spatial_grid_watcher_->result();
        spatial_grid_ready_ = true;
        update();
      },
      Qt::SingleShotConnection);

  spatial_grid_watcher_->setFuture(QtConcurrent::run(
      [this, map, networkMesh, junctionMesh, gridResolution]() {
        return BuildSpatialGridData(map, *networkMesh, *junctionMesh,
                                    gridResolution);
      }));
}

void GeoViewerWidget::BuildSpatialGrid() {
  grid_boxes_ = BuildSpatialGridData(map_, network_mesh_, junction_mesh_,
                                     grid_resolution_);
  spatial_grid_ready_ = true;
}

std::vector<SceneGridBox> GeoViewerWidget::BuildSpatialGridData(
    std::shared_ptr<odr::OpenDriveMap> map,
    const odr::RoadNetworkMesh& networkMesh, const odr::Mesh3D& junctionMesh,
    int gridResolution) const {
  return BuildSpatialGridBoxes(
      networkMesh.lanes_mesh,
      {
          SceneMeshLayerView{&networkMesh.lanes_mesh,
                             static_cast<uint32_t>(LayerType::kLanes),
                             {}},
          SceneMeshLayerView{&networkMesh.roadmarks_mesh,
                             static_cast<uint32_t>(LayerType::kRoadmarks),
                             {}},
          SceneMeshLayerView{&networkMesh.road_objects_mesh,
                             static_cast<uint32_t>(LayerType::kObjects),
                             {}},
          SceneMeshLayerView{
              &junctionMesh, static_cast<uint32_t>(LayerType::kJunctions), {}},
          SceneMeshLayerView{
              &networkMesh.road_signals_mesh,
              static_cast<uint32_t>(LayerType::kSignalLights),
              [map, &networkMesh](uint32_t vertex_index) {
                std::string road_id =
                    networkMesh.road_signals_mesh.get_road_id(vertex_index);
                std::string signal_id =
                    networkMesh.road_signals_mesh.get_road_signal_id(
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
      gridResolution);
}

std::optional<GeoViewerWidget::PickResult>
GeoViewerWidget::GetPickedVertexIndex(int x, int y) {
  if (network_mesh_.lanes_mesh.vertices.empty() || !spatial_grid_ready_) {
    return std::nullopt;
  }
  QVector3D origin;
  QVector3D direction;
  BuildRayFromScreenPoint(x, y, viewport_size_, proj_ * GetViewMatrix(), origin,
                          direction);

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

  closestT = result->distance;
  return PickResult{static_cast<LayerType>(result->layer_tag),
                    result->vertex_index};
}

int GeoViewerWidget::AddRoutingPath(const std::vector<odr::LaneKey>& path,
                                    const QString& name) {
  if (!map_ || path.empty() || !routing_buf_mgr_) return -1;
  makeCurrent();
  const int id = routing_buf_mgr_->Add(path, map_, right_hand_traffic_);
  doneCurrent();
  update();
  return id;
}

void GeoViewerWidget::RemoveRoutingPath(int id) {
  if (!routing_buf_mgr_) return;
  makeCurrent();
  routing_buf_mgr_->Remove(id);
  doneCurrent();
  update();
}

void GeoViewerWidget::SetRoutingPathVisible(int id, bool visible) {
  if (routing_buf_mgr_) {
    routing_buf_mgr_->SetVisible(id, visible);
    update();
  }
}

void GeoViewerWidget::ClearRoutingPaths() {
  if (!routing_buf_mgr_) return;
  makeCurrent();
  routing_buf_mgr_->Clear();
  doneCurrent();
  update();
}

// UpdateRoutingBuffers() migrated to RoutingBufferManager::BuildBuffers()

void GeoViewerWidget::RendererToLocalCoord(const QVector3D& rendererPos,
                                           double& localX, double& localY,
                                           double& localZ) {
  localX = rendererPos.x();
  localY = rendererPos.z();
  if (right_hand_traffic_) localY = -localY;  // Reverse mirroring
  localZ = rendererPos.y();
}

bool GeoViewerWidget::LocalToWGS84(double localX, double localY, double localZ,
                                   double& lon, double& lat, double& alt) {
  try {
    double x = localX, y = localY;
    CoordinateUtil::Instance().LocalToWGS84(&x, &y, nullptr);
    lon = x;
    lat = y;
    alt = localZ;
    return true;
  } catch (...) {
    return false;
  }
}

bool GeoViewerWidget::GetWorldPosAt(int x, int y, QVector3D& worldPos,
                                    std::optional<PickResult>& pickedIdx) {
  pickedIdx = GetPickedVertexIndex(x, y);

  float mouseX = (2.0f * x) / viewport_size_.width() - 1.0f;
  float mouseY = 1.0f - (2.0f * y) / viewport_size_.height();

  QMatrix4x4 invMVP = (proj_ * GetViewMatrix()).inverted();
  QVector4D rayOrigin = invMVP * QVector4D(mouseX, mouseY, -1.0f, 1.0f);
  QVector4D rayEnd = invMVP * QVector4D(mouseX, mouseY, 1.0f, 1.0f);
  rayOrigin /= rayOrigin.w();
  rayEnd /= rayEnd.w();

  QVector3D origin(rayOrigin.x(), rayOrigin.y(), rayOrigin.z());
  QVector3D direction = (rayEnd - rayOrigin).toVector3D().normalized();

  if (pickedIdx.has_value()) {
    worldPos = origin + direction * closestT;
    return true;
  }

  // Fallback: if no object picked, intersect with ground plane (Renderer Y=0
  // coordinate system)
  if (std::abs(direction.y()) > 1e-6f) {
    float t = -origin.y() / direction.y();
    if (t > 0) {
      worldPos = origin + direction * t;
      return true;
    }
  }

  return false;
}

void GeoViewerWidget::contextMenuEvent(QContextMenuEvent* ev) {
  camera_.EndDrag();
  QVector3D worldPos;
  std::optional<PickResult> pickedIdx;
  if (!GetWorldPosAt(ev->pos().x(), ev->pos().y(), worldPos, pickedIdx)) {
    return;
  }

  double localX, localY, localZ;
  RendererToLocalCoord(worldPos, localX, localY, localZ);

  double lon, lat, alt;
  if (!LocalToWGS84(localX, localY, localZ, lon, lat, alt)) {
    return;
  }

  QString coordText = QString("%1,%2,%3")
                          .arg(lon, 0, 'f', 8)
                          .arg(lat, 0, 'f', 8)
                          .arg(alt, 0, 'f', 2);
  QString infoText;

  if (pickedIdx.has_value() && map_) {
    size_t vi = pickedIdx->vertexIndex;
    if (pickedIdx->layer == LayerType::kLanes) {
      std::string rId = network_mesh_.lanes_mesh.get_road_id(vi);
      if (map_->id_to_road.count(rId)) {
        infoText =
            QString("%1/%2/%3")
                .arg(rId.c_str())
                .arg(network_mesh_.lanes_mesh.get_lanesec_s0(vi), 0, 'f', 2)
                .arg(network_mesh_.lanes_mesh.get_lane_id(vi));
      }
    } else if (pickedIdx->layer == LayerType::kRoadmarks) {
      std::string rId = network_mesh_.roadmarks_mesh.get_road_id(vi);
      if (map_->id_to_road.count(rId)) {
        const auto& r = map_->id_to_road.at(rId);
        std::string mType = network_mesh_.roadmarks_mesh.get_roadmark_type(vi);
        infoText = QString("Roadmark %1 in kRoad %2 (Name: %3)")
                       .arg(mType.c_str())
                       .arg(rId.c_str())
                       .arg(r.name.c_str());
      }
    } else if (pickedIdx->layer == LayerType::kObjects) {
      std::string rId = network_mesh_.road_objects_mesh.get_road_id(vi);
      if (map_->id_to_road.count(rId)) {
        const auto& r = map_->id_to_road.at(rId);
        std::string oId =
            network_mesh_.road_objects_mesh.get_road_object_id(vi);
        if (r.id_to_object.count(oId)) {
          const auto& obj = r.id_to_object.at(oId);
          infoText = QString("kObject %1 (Name: %2, Type: %3) in kRoad %4")
                         .arg(oId.c_str())
                         .arg(obj.name.c_str())
                         .arg(obj.type.c_str())
                         .arg(rId.c_str());
        }
      }
    } else if (pickedIdx->layer == LayerType::kSignalLights ||
               pickedIdx->layer == LayerType::kSignalSigns) {
      std::string rId = network_mesh_.road_signals_mesh.get_road_id(vi);
      if (map_->id_to_road.count(rId)) {
        const auto& r = map_->id_to_road.at(rId);
        std::string sId =
            network_mesh_.road_signals_mesh.get_road_signal_id(vi);
        if (r.id_to_signal.count(sId)) {
          const auto& sig = r.id_to_signal.at(sId);
          infoText = QString("Signal %1 (Name: %2, Type: %3) in kRoad %4")
                         .arg(sId.c_str())
                         .arg(sig.name.c_str())
                         .arg(sig.type.c_str())
                         .arg(rId.c_str());
        }
      }
    }
  }

  QMenu menu(this);
  QAction* copyCoord =
      menu.addAction(tr("📋 Copy coordinate: %1").arg(coordText));
  QAction* copyInfo = nullptr;
  QAction* copyAll = nullptr;
  if (!infoText.isEmpty()) {
    copyInfo = menu.addAction(tr("🏷️ Copy info: %1").arg(infoText));
    copyAll = menu.addAction(tr("📋 Copy all"));
  }

  QAction* hideElement = menu.addAction(tr("👁️ Hide current object"));
  QAction* addFav = menu.addAction(tr("⭐ Add to favorites"));

  QAction* setStartRouting = nullptr;
  QAction* setEndRouting = nullptr;
  if (pickedIdx && pickedIdx->layer == LayerType::kLanes) {
    menu.addSeparator();
    setStartRouting = menu.addAction(tr("🚩 Set as routing start"));
    setEndRouting = menu.addAction(tr("🏁 Set as routing end"));
  }

  QAction* selected = menu.exec(ev->globalPos());
  if (selected == hideElement) {
    QString roadId, elementId, group;
    size_t vi = pickedIdx->vertexIndex;
    if (pickedIdx->layer == LayerType::kLanes) {
      roadId = QString::fromStdString(network_mesh_.lanes_mesh.get_road_id(vi));
      double s0 = network_mesh_.lanes_mesh.get_lanesec_s0(vi);
      int laneId = network_mesh_.lanes_mesh.get_lane_id(vi);
      group = "lane";
      elementId = QString::fromStdString(FormatSectionValue(s0)) + ":" +
                  QString::number(laneId);
    } else if (pickedIdx->layer == LayerType::kObjects) {
      roadId = QString::fromStdString(
          network_mesh_.road_objects_mesh.get_road_id(vi));
      elementId = QString::fromStdString(
          network_mesh_.road_objects_mesh.get_road_object_id(vi));
      group = "objects";
    } else if (pickedIdx->layer == LayerType::kSignalLights) {
      roadId = QString::fromStdString(
          network_mesh_.road_signals_mesh.get_road_id(vi));
      elementId = QString::fromStdString(
          network_mesh_.road_signals_mesh.get_road_signal_id(vi));
      group = "lights";
    } else if (pickedIdx->layer == LayerType::kSignalSigns) {
      roadId = QString::fromStdString(
          network_mesh_.road_signals_mesh.get_road_id(vi));
      elementId = QString::fromStdString(
          network_mesh_.road_signals_mesh.get_road_signal_id(vi));
      group = "signs";
    }
    if (!roadId.isEmpty()) {
      SetElementVisible(
          QString("E:%1:%2:%3").arg(roadId).arg(group).arg(elementId), false);
    }
  } else if (selected == copyCoord) {
    QApplication::clipboard()->setText(coordText);
  } else if (selected == copyInfo && copyInfo) {
    QApplication::clipboard()->setText(infoText);
  } else if (selected == copyAll && copyAll) {
    QApplication::clipboard()->setText(
        QString("%1 | %2").arg(coordText).arg(infoText));
  } else if (selected == addFav) {
    QString roadId, elementId, name;
    TreeNodeType nodeType = TreeNodeType::kRoad;
    size_t vi = pickedIdx->vertexIndex;
    if (pickedIdx->layer == LayerType::kLanes) {
      roadId = QString::fromStdString(network_mesh_.lanes_mesh.get_road_id(vi));
      double s0 = network_mesh_.lanes_mesh.get_lanesec_s0(vi);
      int laneId = network_mesh_.lanes_mesh.get_lane_id(vi);
      elementId = QString::fromStdString(FormatSectionValue(s0)) + ":" +
                  QString::number(laneId);
      nodeType = TreeNodeType::kLane;
      name = QString("kRoad %1 kLane %2").arg(roadId).arg(elementId);
    } else if (pickedIdx->layer == LayerType::kObjects) {
      roadId = QString::fromStdString(
          network_mesh_.road_objects_mesh.get_road_id(vi));
      elementId = QString::fromStdString(
          network_mesh_.road_objects_mesh.get_road_object_id(vi));
      nodeType = TreeNodeType::kObject;
      name = QString("kObject %1").arg(elementId);
    } else if (pickedIdx->layer == LayerType::kSignalLights) {
      roadId = QString::fromStdString(
          network_mesh_.road_signals_mesh.get_road_id(vi));
      elementId = QString::fromStdString(
          network_mesh_.road_signals_mesh.get_road_signal_id(vi));
      nodeType = TreeNodeType::kLight;
      name = QString("kLight %1").arg(elementId);
    } else if (pickedIdx->layer == LayerType::kSignalSigns) {
      roadId = QString::fromStdString(
          network_mesh_.road_signals_mesh.get_road_id(vi));
      elementId = QString::fromStdString(
          network_mesh_.road_signals_mesh.get_road_signal_id(vi));
      nodeType = TreeNodeType::kSign;
      name = QString("kSign %1").arg(elementId);
    }
    if (!roadId.isEmpty()) {
      emit addFavoriteRequested(roadId, nodeType, elementId, name);
    }
  } else if (selected == setStartRouting || selected == setEndRouting) {
    size_t vi = pickedIdx->vertexIndex;
    QString roadId =
        QString::fromStdString(network_mesh_.lanes_mesh.get_road_id(vi));
    double s0 = network_mesh_.lanes_mesh.get_lanesec_s0(vi);
    int laneId = network_mesh_.lanes_mesh.get_lane_id(vi);
    const std::string lane_pos =
        BuildLanePosition(roadId.toStdString(), FormatSectionValue(s0),
                          QString::number(laneId).toStdString());
    QString lanePos = QString::fromStdString(lane_pos);
    if (selected == setStartRouting)
      emit routingStartRequested(lanePos.trimmed());
    else
      emit routingEndRequested(lanePos.trimmed());
  }
}

void GeoViewerWidget::UpdateHighlight(size_t vertIdx, LayerType type) {
  size_t start = 0, end = 0;
  const odr::Mesh3D* mesh = nullptr;

  if (type == LayerType::kLanes) {
    auto interval = network_mesh_.lanes_mesh.get_idx_interval_lane(vertIdx);
    start = interval[0];
    end = interval[1];
    mesh = &network_mesh_.lanes_mesh;
  } else if (type == LayerType::kObjects) {
    auto interval =
        network_mesh_.road_objects_mesh.get_idx_interval_road_object(vertIdx);
    start = interval[0];
    end = interval[1];
    mesh = &network_mesh_.road_objects_mesh;
  } else if (type == LayerType::kSignalLights ||
             type == LayerType::kSignalSigns) {
    auto interval =
        network_mesh_.road_signals_mesh.get_idx_interval_signal(vertIdx);
    start = interval[0];
    end = interval[1];
    mesh = &network_mesh_.road_signals_mesh;
  } else {
    ClearHighlight();
    return;
  }

  if (start == highlight_mgr_->cur_start && end == highlight_mgr_->cur_end &&
      type == highlight_mgr_->cur_layer) {
    return;
  }
  highlight_mgr_->cur_start = start;
  highlight_mgr_->cur_end = end;
  highlight_mgr_->cur_layer = type;

  std::vector<uint32_t> indices;
  size_t vOffset = layers_[(int)type].vertex_offset;

  for (size_t i = 0; i < mesh->indices.size(); i += 3) {
    uint32_t i0 = mesh->indices[i];
    uint32_t i1 = mesh->indices[i + 1];
    uint32_t i2 = mesh->indices[i + 2];
    if (i0 >= start && i0 < end && i1 >= start && i1 < end && i2 >= start &&
        i2 < end) {
      indices.push_back(i0 + vOffset);
      indices.push_back(i1 + vOffset);
      indices.push_back(i2 + vOffset);
    }
  }

  SetHighlightIndices(indices, type, type == LayerType::kLanes, vertIdx);
}

void GeoViewerWidget::ClearHighlight() {
  if (!highlight_mgr_) return;
  if (highlight_mgr_->HasHighlight() ||
      highlight_mgr_->HasNeighborHighlight()) {
    highlight_mgr_->Clear();
    update();
  }
}

QString GeoViewerWidget::BuildOpenScenarioEntityKey(
    const QString& file_id, const QString& entity_name) const {
  return file_id + "::" + entity_name;
}

double GeoViewerWidget::ResolveLaneCenterT(const odr::Road& road, double s,
                                           int lane_id, double offset) const {
  const auto lane_section = road.get_lanesection(s);
  auto lane_it = lane_section.id_to_lane.find(lane_id);
  if (lane_it == lane_section.id_to_lane.end()) return offset;

  const auto& lane = lane_it->second;
  const double lane_width = lane.lane_width.get(s);
  const double t_outer = lane.outer_border.get(s);
  const double side_sign = lane_id > 0 ? 0.5 : -0.5;
  return t_outer - side_sign * lane_width + offset;
}

std::optional<QVector3D> GeoViewerWidget::ResolveOpenScenarioPosition(
    const OpenScenarioPosition& pos) const {
  if (pos.type == OpenScenarioPositionType::kWorld) {
    return LocalToRendererPoint({pos.x, pos.y, pos.z});
  }

  if (!map_) return std::nullopt;
  if (pos.road_id.isEmpty()) return std::nullopt;

  auto road_it = map_->id_to_road.find(pos.road_id.toStdString());
  if (road_it == map_->id_to_road.end()) return std::nullopt;

  const odr::Road& road = road_it->second;
  if (pos.type == OpenScenarioPositionType::kRoad) {
    return LocalToRendererPoint(road.get_xyz(pos.s, pos.t, 0.0));
  }
  if (pos.type == OpenScenarioPositionType::kLane) {
    const double t = ResolveLaneCenterT(road, pos.s, pos.lane_id, pos.offset);
    return LocalToRendererPoint(road.get_xyz(pos.s, t, 0.0));
  }
  return std::nullopt;
}

QString GeoViewerWidget::DescribeOpenScenarioPosition(
    const OpenScenarioPosition& pos) const {
  if (pos.type == OpenScenarioPositionType::kWorld) {
    return QString("World (x=%1, y=%2, z=%3)")
        .arg(pos.x, 0, 'f', 2)
        .arg(pos.y, 0, 'f', 2)
        .arg(pos.z, 0, 'f', 2);
  }
  if (pos.type == OpenScenarioPositionType::kRoad) {
    return QString("Road (road=%1, s=%2, t=%3)")
        .arg(pos.road_id)
        .arg(pos.s, 0, 'f', 2)
        .arg(pos.t, 0, 'f', 2);
  }
  if (pos.type == OpenScenarioPositionType::kLane) {
    return QString("Lane (road=%1, lane=%2, s=%3, offset=%4)")
        .arg(pos.road_id)
        .arg(pos.lane_id)
        .arg(pos.s, 0, 'f', 2)
        .arg(pos.offset, 0, 'f', 2);
  }
  return "Unknown";
}

void GeoViewerWidget::ResolveOpenScenarioData() {
  for (auto& file : open_scenarios_) {
    for (auto& entity : file.entities) {
      entity.has_position = false;
      entity.trajectory.clear();
      entity.position_desc.clear();

      if (entity.source.initial_position.has_value()) {
        entity.position_desc =
            DescribeOpenScenarioPosition(*entity.source.initial_position);
        const auto resolved =
            ResolveOpenScenarioPosition(*entity.source.initial_position);
        if (resolved.has_value()) {
          entity.position = *resolved;
          entity.has_position = true;
        }
      }

      for (const auto& pos : entity.source.storyboard_positions) {
        const auto resolved = ResolveOpenScenarioPosition(pos);
        if (!resolved.has_value()) continue;
        entity.trajectory.push_back(*resolved);
        if (!entity.has_position) {
          entity.position = *resolved;
          entity.position_desc = DescribeOpenScenarioPosition(pos);
          entity.has_position = true;
        }
      }
    }
  }
}

bool GeoViewerWidget::LoadOpenScenarioFile(const QString& file_path,
                                           QString* error_message) {
  OpenScenarioFile parsed;
  if (!ParseOpenScenarioFile(file_path, &parsed, error_message)) {
    return false;
  }

  QString file_id = QFileInfo(file_path).canonicalFilePath();
  if (file_id.isEmpty()) {
    file_id = QFileInfo(file_path).absoluteFilePath();
  }

  auto replace_it =
      std::find_if(open_scenarios_.begin(), open_scenarios_.end(),
                   [&file_id](const OpenScenarioFileState& state) {
                     return state.file_id == file_id;
                   });
  if (replace_it != open_scenarios_.end()) {
    open_scenarios_.erase(replace_it);
  }

  OpenScenarioFileState state;
  state.file_id = file_id;
  state.visible = true;
  state.source = std::move(parsed);
  state.entities.reserve(state.source.entities.size());
  for (const auto& entity : state.source.entities) {
    OpenScenarioEntityState entity_state;
    entity_state.source = entity;
    entity_state.visible = true;
    state.entities.push_back(std::move(entity_state));
  }
  open_scenarios_.push_back(std::move(state));

  ResolveOpenScenarioData();
  emit openScenarioDataChanged();
  update();
  return true;
}

bool GeoViewerWidget::RemoveOpenScenarioFile(const QString& file_id) {
  auto it = std::find_if(open_scenarios_.begin(), open_scenarios_.end(),
                         [&file_id](const OpenScenarioFileState& state) {
                           return state.file_id == file_id;
                         });
  if (it == open_scenarios_.end()) return false;
  open_scenarios_.erase(it);
  emit openScenarioDataChanged();
  update();
  return true;
}

std::vector<GeoViewerWidget::OpenScenarioFileSnapshot>
GeoViewerWidget::OpenScenarioSnapshots() const {
  std::vector<OpenScenarioFileSnapshot> snapshots;
  snapshots.reserve(open_scenarios_.size());
  for (const auto& file : open_scenarios_) {
    OpenScenarioFileSnapshot file_snapshot;
    file_snapshot.file_id = file.file_id;
    file_snapshot.file_name = file.source.file_name;
    file_snapshot.file_path = file.source.path;
    file_snapshot.version = file.source.version;
    file_snapshot.visible = file.visible;
    file_snapshot.entities.reserve(file.entities.size());
    for (const auto& entity : file.entities) {
      OpenScenarioEntitySnapshot entity_snapshot;
      entity_snapshot.name = entity.source.name;
      entity_snapshot.object_type = entity.source.object_type;
      entity_snapshot.visible = entity.visible;
      entity_snapshot.has_position = entity.has_position;
      entity_snapshot.position_desc = entity.position_desc;
      file_snapshot.entities.push_back(std::move(entity_snapshot));
    }
    snapshots.push_back(std::move(file_snapshot));
  }
  return snapshots;
}

void GeoViewerWidget::SetOpenScenarioFileVisible(const QString& file_id,
                                                 bool visible) {
  for (auto& file : open_scenarios_) {
    if (file.file_id != file_id) continue;
    file.visible = visible;
    for (auto& entity : file.entities) {
      entity.visible = visible;
    }
    emit openScenarioDataChanged();
    update();
    return;
  }
}

void GeoViewerWidget::SetOpenScenarioEntityVisible(const QString& file_id,
                                                   const QString& entity_name,
                                                   bool visible) {
  for (auto& file : open_scenarios_) {
    if (file.file_id != file_id) continue;
    for (auto& entity : file.entities) {
      if (entity.source.name == entity_name) {
        entity.visible = visible;
        break;
      }
    }
    int visible_count = 0;
    for (const auto& entity : file.entities) {
      if (entity.visible) ++visible_count;
    }
    file.visible = visible_count > 0;
    emit openScenarioDataChanged();
    update();
    return;
  }
}

void GeoViewerWidget::HighlightOpenScenarioEntity(const QString& file_id,
                                                  const QString& entity_name) {
  if (file_id.isEmpty() || entity_name.isEmpty()) {
    hovered_open_scenario_entity_key_.clear();
    update();
    return;
  }
  hovered_open_scenario_entity_key_ =
      BuildOpenScenarioEntityKey(file_id, entity_name);
  update();
}

void GeoViewerWidget::CenterOnOpenScenarioEntity(const QString& file_id,
                                                 const QString& entity_name) {
  for (const auto& file : open_scenarios_) {
    if (file.file_id != file_id) continue;
    for (const auto& entity : file.entities) {
      if (entity.source.name != entity_name || !entity.has_position) continue;
      camera_.SetTarget(entity.position);
      camera_.SetPitch(-70.0f);
      camera_.SetYaw(30.0f);
      camera_.SetDistance(80.0f);
      hovered_open_scenario_entity_key_ =
          BuildOpenScenarioEntityKey(file_id, entity_name);
      update();
      return;
    }
  }
}

void GeoViewerWidget::RenderOpenScenarioOverlay(QPainter& painter,
                                                const QMatrix4x4& viewProj) {
  if (open_scenarios_.empty()) return;

  QPen traj_pen(QColor(80, 180, 255, 180));
  traj_pen.setWidth(2);
  QPen marker_pen(QColor(255, 255, 255, 220));
  marker_pen.setWidth(1);
  QPen highlight_pen(QColor(120, 255, 180, 255));
  highlight_pen.setWidth(3);

  painter.setFont(QFont("Menlo", 9));

  for (const auto& file : open_scenarios_) {
    if (!file.visible) continue;
    for (const auto& entity : file.entities) {
      if (!entity.visible || !entity.has_position) continue;

      const QString key =
          BuildOpenScenarioEntityKey(file.file_id, entity.source.name);
      const bool highlighted = (hovered_open_scenario_entity_key_ == key);

      if (entity.trajectory.size() >= 2) {
        painter.setPen(traj_pen);
        for (std::size_t i = 1; i < entity.trajectory.size(); ++i) {
          QVector4D a_clip = viewProj * QVector4D(entity.trajectory[i - 1], 1);
          QVector4D b_clip = viewProj * QVector4D(entity.trajectory[i], 1);
          if (a_clip.w() <= 0.0f || b_clip.w() <= 0.0f) continue;
          const QPointF a((a_clip.x() / a_clip.w() + 1.0f) * width() * 0.5f,
                          (1.0f - a_clip.y() / a_clip.w()) * height() * 0.5f);
          const QPointF b((b_clip.x() / b_clip.w() + 1.0f) * width() * 0.5f,
                          (1.0f - b_clip.y() / b_clip.w()) * height() * 0.5f);
          painter.drawLine(a, b);
        }
      }

      QVector4D clip = viewProj * QVector4D(entity.position, 1.0f);
      if (clip.w() <= 0.0f) continue;
      const float ndc_x = clip.x() / clip.w();
      const float ndc_y = clip.y() / clip.w();
      if (ndc_x < -1.2f || ndc_x > 1.2f || ndc_y < -1.2f || ndc_y > 1.2f) {
        continue;
      }

      const QPointF screen((ndc_x + 1.0f) * width() * 0.5f,
                           (1.0f - ndc_y) * height() * 0.5f);
      painter.setPen(highlighted ? highlight_pen : marker_pen);
      painter.setBrush(highlighted ? QColor(120, 255, 180, 220)
                                   : QColor(70, 180, 255, 210));
      const qreal r = highlighted ? 6.0 : 4.0;
      painter.drawEllipse(screen, r, r);

      painter.setPen(Qt::white);
      painter.drawText(screen + QPointF(8.0, -6.0), entity.source.name);
    }
  }
}

void GeoViewerWidget::UpdateHoverInfo(int x, int y) {
  QVector3D worldPos;
  std::optional<PickResult> pickedIdx;

  QString typeStr, idStr, nameStr;

  float mouseX = (2.0f * x) / viewport_size_.width() - 1.0f;
  float mouseY = 1.0f - (2.0f * y) / viewport_size_.height();
  QMatrix4x4 invMVP = (proj_ * GetViewMatrix()).inverted();
  QVector4D ro4 = invMVP * QVector4D(mouseX, mouseY, -1.0f, 1.0f);
  QVector4D re4 = invMVP * QVector4D(mouseX, mouseY, 1.0f, 1.0f);
  ro4 /= ro4.w();
  re4 /= re4.w();
  QVector3D origin(ro4.x(), ro4.y(), ro4.z());
  QVector3D direction = (re4 - ro4).toVector3D().normalized();

  pickedIdx = GetPickedVertexIndex(x, y);

  if (pickedIdx.has_value() && map_) {
    worldPos = origin + direction * closestT;
    size_t vi = pickedIdx->vertexIndex;

    if (pickedIdx->layer == LayerType::kLanes) {
      std::string rId = network_mesh_.lanes_mesh.get_road_id(vi);
      double s0 = network_mesh_.lanes_mesh.get_lanesec_s0(vi);
      int lId = network_mesh_.lanes_mesh.get_lane_id(vi);

      typeStr = "kLane";
      idStr = QString("%1 / %2 / %3").arg(rId.c_str()).arg(s0).arg(lId);

      if (map_->id_to_road.count(rId)) {
        auto& r = map_->id_to_road.at(rId);
        nameStr =
            QString("kRoad: %1 | Length: %2").arg(r.name.c_str()).arg(r.length);

        if (r.s_to_lanesection.count(s0)) {
          auto& lanesec = r.s_to_lanesection.at(s0);
          if (lanesec.id_to_lane.count(lId)) {
            typeStr = QString("kLane (%1)")
                          .arg(lanesec.id_to_lane.at(lId).type.c_str());
          }
        }
      }
      UpdateHighlight(vi, pickedIdx->layer);
    } else if (pickedIdx->layer == LayerType::kRoadmarks) {
      std::string rId = network_mesh_.roadmarks_mesh.get_road_id(vi);
      typeStr = "Roadmark";
      idStr = QString::fromStdString(
          network_mesh_.roadmarks_mesh.get_roadmark_type(vi));
      if (map_->id_to_road.count(rId)) {
        const auto& r = map_->id_to_road.at(rId);
        nameStr = QString("kRoad: %1").arg(r.name.c_str());
      }
      ClearHighlight();
    } else if (pickedIdx->layer == LayerType::kObjects) {
      std::string rId = network_mesh_.road_objects_mesh.get_road_id(vi);
      std::string oId = network_mesh_.road_objects_mesh.get_road_object_id(vi);
      typeStr = "kObject";
      if (map_->id_to_road.count(rId)) {
        const auto& r = map_->id_to_road.at(rId);
        if (r.id_to_object.count(oId)) {
          idStr = QString("%1 (%2)")
                      .arg(oId.c_str())
                      .arg(r.id_to_object.at(oId).type.c_str());
          nameStr = QString::fromStdString(r.id_to_object.at(oId).name);
        } else {
          idStr = QString::fromStdString(oId);
        }
      } else {
        idStr = QString::fromStdString(oId);
      }
      UpdateHighlight(vi, pickedIdx->layer);
    } else if (pickedIdx->layer == LayerType::kSignalLights ||
               pickedIdx->layer == LayerType::kSignalSigns) {
      std::string rId = network_mesh_.road_signals_mesh.get_road_id(vi);
      std::string sId = network_mesh_.road_signals_mesh.get_road_signal_id(vi);
      typeStr = pickedIdx->layer == LayerType::kSignalLights ? "TrafficLight"
                                                             : "TrafficSign";
      if (map_->id_to_road.count(rId)) {
        const auto& r = map_->id_to_road.at(rId);
        if (r.id_to_signal.count(sId)) {
          idStr = QString("%1 (%2)")
                      .arg(sId.c_str())
                      .arg(r.id_to_signal.at(sId).type.c_str());
          nameStr = QString::fromStdString(r.id_to_signal.at(sId).name);
        } else {
          idStr = QString::fromStdString(sId);
        }
      } else {
        idStr = QString::fromStdString(sId);
      }
      UpdateHighlight(vi, pickedIdx->layer);
    } else if (pickedIdx->layer == LayerType::kJunctions) {
      auto group_id = FindJunctionGroupByVertex(vi);
      if (group_id.has_value()) {
        const std::string& groupId = *group_id;
        typeStr = "Junction";
        idStr = QString::fromStdString(groupId);
        auto groupIndexIt = junction_group_index_by_id_.find(groupId);
        if (groupIndexIt != junction_group_index_by_id_.end()) {
          const auto& group =
              junction_cluster_result_.groups[groupIndexIt->second];
          nameStr = QString("%1 | %2 roads | %3")
                        .arg(JunctionClusterUtil::SemanticTypeToString(
                            group.semantic_type))
                        .arg((int)group.external_road_ids.size())
                        .arg((int)group.junction_ids.size());
        }
        auto indices = CollectIndicesForCachedElements(
            LayerType::kJunctions, junction_element_items_,
            junction_mesh_.indices, [&](const SceneCachedElement& element) {
              return element.elementKey == ("JG:" + groupId);
            });
        SetHighlightIndices(indices, LayerType::kJunctions);
      } else {
        ClearHighlight();
      }
    }
  } else {
    worldPos = origin + direction * 1000.0f;
    ClearHighlight();
  }

  double localX, localY, localZ;
  RendererToLocalCoord(worldPos, localX, localY, localZ);
  double lon = 0.0, lat = 0.0, alt = 0.0;
  LocalToWGS84(localX, localY, localZ, lon, lat, alt);

  emit hoverInfoChanged(lon, lat, alt, typeStr, idStr, nameStr);
}

void GeoViewerWidget::SearchObject(LayerType type, const QString& idStr) {
  if (!map_) return;
  std::string targetId = idStr.toStdString();
  size_t targetStartVertex = SIZE_MAX;
  const odr::Mesh3D* targetMesh = nullptr;

  auto findIdStr = [&](const std::map<size_t, std::string>& m) {
    for (const auto& kv : m) {
      if (kv.second == targetId) return kv.first;
    }
    return SIZE_MAX;
  };

  if (type == LayerType::kLanes) {
    targetStartVertex = findIdStr(network_mesh_.lanes_mesh.road_start_indices);
    targetMesh = &network_mesh_.lanes_mesh;
  } else if (type == LayerType::kObjects) {
    targetStartVertex =
        findIdStr(network_mesh_.road_objects_mesh.road_object_start_indices);
    targetMesh = &network_mesh_.road_objects_mesh;
  } else if (type == LayerType::kSignalLights ||
             type == LayerType::kSignalSigns) {
    targetStartVertex =
        findIdStr(network_mesh_.road_signals_mesh.road_signal_start_indices);
    targetMesh = &network_mesh_.road_signals_mesh;
    if (targetStartVertex != SIZE_MAX) {
      std::string rId =
          network_mesh_.road_signals_mesh.get_road_id(targetStartVertex);
      if (map_->id_to_road.count(rId)) {
        bool is_light = false;
        auto& r = map_->id_to_road.at(rId);
        if (r.id_to_signal.count(targetId))
          is_light = (r.id_to_signal.at(targetId).name == "TrafficLight");
        if ((type == LayerType::kSignalLights && !is_light) ||
            (type == LayerType::kSignalSigns && is_light)) {
          targetStartVertex = SIZE_MAX;
        }
      }
    }
  }

  if (targetStartVertex == SIZE_MAX || !targetMesh) return;

  UpdateHighlight(targetStartVertex, type);

  if (highlight_mgr_ && highlight_mgr_->cur_start < highlight_mgr_->cur_end) {
    double cx = 0, cy = 0, cz = 0;
    int count = 0;
    for (size_t i = highlight_mgr_->cur_start;
         i < highlight_mgr_->cur_end && i < targetMesh->vertices.size(); i++) {
      cx += targetMesh->vertices[i][0];
      cy += targetMesh->vertices[i][1];
      cz += targetMesh->vertices[i][2];
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

    const uint32_t triStart =
        static_cast<uint32_t>(junction_mesh_.indices.size() / 3);
    for (uint32_t i = 1; i <= points.size(); ++i) {
      const uint32_t next = (i == points.size()) ? 1 : (i + 1);
      junction_mesh_.indices.insert(junction_mesh_.indices.end(),
                                    {base, base + i, base + next});
    }

    SceneCachedElement element;
    element.roadKey = "JG:" + group.group_id;
    element.groupKey = "junctions";
    element.elementKey = "JG:" + group.group_id;
    element.ranges.push_back({triStart, static_cast<uint32_t>(points.size())});
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
  return CollectSceneIndices(elements, source_indices,
                             layers_[(int)type].vertex_offset, predicate);
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
  if (!highlight_mgr_) return;

  highlight_mgr_->cur_layer = type;
  highlight_mgr_->bounds_valid = false;

  const odr::Mesh3D* mesh = MeshForLayer(type);

  // Calculate highlighted bounding box
  if (mesh && !indices.empty()) {
    const size_t vOffset = layers_[(int)type].vertex_offset;
    QVector3D minB(std::numeric_limits<float>::max(),
                   std::numeric_limits<float>::max(),
                   std::numeric_limits<float>::max());
    QVector3D maxB(std::numeric_limits<float>::lowest(),
                   std::numeric_limits<float>::lowest(),
                   std::numeric_limits<float>::lowest());
    for (uint32_t globalIdx : indices) {
      if (globalIdx < vOffset) continue;
      const size_t localIdx = static_cast<size_t>(globalIdx - vOffset);
      if (localIdx >= mesh->vertices.size()) continue;
      const auto& v = mesh->vertices[localIdx];
      minB.setX(std::min(minB.x(), static_cast<float>(v[0])));
      minB.setY(std::min(minB.y(), static_cast<float>(v[1])));
      minB.setZ(std::min(minB.z(), static_cast<float>(v[2])));
      maxB.setX(std::max(maxB.x(), static_cast<float>(v[0])));
      maxB.setY(std::max(maxB.y(), static_cast<float>(v[1])));
      maxB.setZ(std::max(maxB.z(), static_cast<float>(v[2])));
    }
    if (minB.x() <= maxB.x()) {
      highlight_mgr_->bounds_valid = true;
      highlight_mgr_->min_bound = minB;
      highlight_mgr_->max_bound = maxB;
    }
  }

  // Upload primary highlight
  makeCurrent();
  highlight_mgr_->UploadHighlight(indices);

  // Neighbor highlight
  if (with_neighbors && type == LayerType::kLanes && routing_graph_) {
    std::vector<uint32_t> n_indices;
    const std::string rId =
        network_mesh_.lanes_mesh.get_road_id(reference_vertex);
    const double s0 = network_mesh_.lanes_mesh.get_lanesec_s0(reference_vertex);
    const int lId = network_mesh_.lanes_mesh.get_lane_id(reference_vertex);
    const odr::LaneKey key(rId, s0, lId);

    std::vector<odr::LaneKey> neighbors;
    auto succs = routing_graph_->get_lane_successors(key);
    auto preds = routing_graph_->get_lane_predecessors(key);
    neighbors.insert(neighbors.end(), succs.begin(), succs.end());
    neighbors.insert(neighbors.end(), preds.begin(), preds.end());

    const size_t laneVOffset = layers_[(int)LayerType::kLanes].vertex_offset;
    for (const auto& nKey : neighbors) {
      auto itemIt = lane_element_index_by_key_.find(nKey);
      if (itemIt != lane_element_index_by_key_.end()) {
        const auto& el = lane_element_items_[itemIt->second];
        for (const auto& range : el.ranges) {
          const std::size_t base = static_cast<std::size_t>(range.start) * 3;
          for (uint32_t k = 0; k < range.count * 3; ++k) {
            n_indices.push_back(network_mesh_.lanes_mesh.indices[base + k] +
                                static_cast<uint32_t>(laneVOffset));
          }
        }
      }
    }
    highlight_mgr_->UploadNeighborHighlight(n_indices);
  } else {
    // Clear neighbor highlights
    highlight_mgr_->UploadNeighborHighlight({});
  }
  doneCurrent();

  if (!highlight_mgr_->HasHighlight()) {
    highlight_mgr_->cur_start = SIZE_MAX;
    highlight_mgr_->cur_end = 0;
    highlight_mgr_->bounds_valid = false;
  }
  update();
}

void GeoViewerWidget::HighlightElement(const QString& roadId, TreeNodeType type,
                                       const QString& elementId) {
  if (type == TreeNodeType::kJunction || type == TreeNodeType::kJunctionGroup) {
    selected_junction_group_id_ =
        (type == TreeNodeType::kJunctionGroup) ? elementId : roadId;
    selected_junction_id_ =
        (type == TreeNodeType::kJunction) ? elementId : QString();
    ClearHighlight();
    update();
    return;
  }

  selected_junction_group_id_.clear();
  selected_junction_id_.clear();
  if (!map_ || roadId.isEmpty()) {
    ClearHighlight();
    return;
  }

  LayerType layerType = LayerType::kLanes;

  std::string roadIdStr = roadId.toStdString();
  std::string elementIdStr = elementId.toStdString();

  if (type == TreeNodeType::kRoad) {
    layerType = LayerType::kLanes;
    auto indices = CollectIndicesForCachedElements(
        layerType, lane_element_items_, network_mesh_.lanes_mesh.indices,
        [&](const SceneCachedElement& element) {
          return element.roadKey == ("R:" + roadIdStr);
        });
    SetHighlightIndices(indices, layerType);
    return;
  } else if (type == TreeNodeType::kSectionGroup) {
    layerType = LayerType::kLanes;
    auto indices = CollectIndicesForCachedElements(
        layerType, lane_element_items_, network_mesh_.lanes_mesh.indices,
        [&](const SceneCachedElement& element) {
          return element.groupKey == ("G:" + roadIdStr + ":section");
        });
    SetHighlightIndices(indices, layerType);
    return;
  } else if (type == TreeNodeType::kSection || type == TreeNodeType::kLane) {
    layerType = LayerType::kLanes;
    const std::string prefix = "E:" + roadIdStr + ":lane:" +
                               elementIdStr.substr(0, elementIdStr.find(':'));
    auto indices = CollectIndicesForCachedElements(
        layerType, lane_element_items_, network_mesh_.lanes_mesh.indices,
        [&](const SceneCachedElement& element) {
          if (type == TreeNodeType::kSection) {
            return element.elementKey.rfind(prefix + ":", 0) == 0;
          }
          return element.elementKey ==
                 ("E:" + roadIdStr + ":lane:" + elementIdStr);
        });
    SetHighlightIndices(indices, layerType);
    return;
  } else if (type == TreeNodeType::kObjectGroup) {
    layerType = LayerType::kObjects;
    auto indices = CollectIndicesForCachedElements(
        layerType, object_element_items_,
        network_mesh_.road_objects_mesh.indices,
        [&](const SceneCachedElement& element) {
          return element.groupKey == ("G:" + roadIdStr + ":objects");
        });
    SetHighlightIndices(indices, layerType);
    return;
  } else if (type == TreeNodeType::kObject) {
    layerType = LayerType::kObjects;
    auto indices = CollectIndicesForCachedElements(
        layerType, object_element_items_,
        network_mesh_.road_objects_mesh.indices,
        [&](const SceneCachedElement& element) {
          return element.elementKey ==
                 ("E:" + roadIdStr + ":objects:" + elementIdStr);
        });
    SetHighlightIndices(indices, layerType);
    return;
  } else if (type == TreeNodeType::kLightGroup ||
             type == TreeNodeType::kSignGroup) {
    layerType = (type == TreeNodeType::kLightGroup) ? LayerType::kSignalLights
                                                    : LayerType::kSignalSigns;
    const std::string group =
        (type == TreeNodeType::kLightGroup) ? "light" : "sign";
    auto indices = CollectIndicesForCachedElements(
        layerType, signal_element_items_,
        network_mesh_.road_signals_mesh.indices,
        [&](const SceneCachedElement& element) {
          return element.groupKey == ("G:" + roadIdStr + ":" + group);
        });
    SetHighlightIndices(indices, layerType);
    return;
  } else if (type == TreeNodeType::kLight || type == TreeNodeType::kSign) {
    layerType = (type == TreeNodeType::kLight) ? LayerType::kSignalLights
                                               : LayerType::kSignalSigns;
    const std::string group = (type == TreeNodeType::kLight) ? "light" : "sign";
    auto indices = CollectIndicesForCachedElements(
        layerType, signal_element_items_,
        network_mesh_.road_signals_mesh.indices,
        [&](const SceneCachedElement& element) {
          return element.elementKey ==
                 ("E:" + roadIdStr + ":" + group + ":" + elementIdStr);
        });
    SetHighlightIndices(indices, layerType);
    return;
  }

  ClearHighlight();
}

void GeoViewerWidget::CenterOnElement(const QString& roadId, TreeNodeType type,
                                      const QString& elementId) {
  if (!map_) return;

  if (type == TreeNodeType::kJunction || type == TreeNodeType::kJunctionGroup) {
    HighlightElement(roadId, type, elementId);
    QString groupId =
        (type == TreeNodeType::kJunctionGroup) ? elementId : roadId;
    auto groupIt = junction_group_index_by_id_.find(groupId.toStdString());
    if (groupIt != junction_group_index_by_id_.end()) {
      camera_.SetTarget(JunctionGroupCenter(
          junction_cluster_result_.groups[groupIt->second]));
      camera_.SetPitch(-75.0f);
      camera_.SetYaw(25.0f);
      camera_.SetDistance(80.0f);
      update();
    }
    return;
  }

  if (roadId.isEmpty()) return;

  HighlightElement(roadId, type, elementId);

  // Find the interval and mesh again for centering (or we could pass it from
  // HighlightElement)
  std::string roadIdStr = roadId.toStdString();
  std::string elementIdStr = elementId.toStdString();

  const size_t targetStartVertex =
      highlight_mgr_ ? highlight_mgr_->cur_start : SIZE_MAX;
  const LayerType layerType =
      highlight_mgr_ ? highlight_mgr_->cur_layer : LayerType::kCount;
  const odr::Mesh3D* targetMesh = nullptr;

  if (layerType == LayerType::kLanes)
    targetMesh = &network_mesh_.lanes_mesh;
  else if (layerType == LayerType::kObjects)
    targetMesh = &network_mesh_.road_objects_mesh;
  else if (layerType == LayerType::kSignalLights ||
           layerType == LayerType::kSignalSigns)
    targetMesh = &network_mesh_.road_signals_mesh;

  if (highlight_mgr_ && highlight_mgr_->bounds_valid) {
    camera_.SetTarget((highlight_mgr_->min_bound + highlight_mgr_->max_bound) *
                      0.5f);
    camera_.SetPitch(-60.0f);
    camera_.SetYaw(45.0f);
    camera_.SetDistance(
        qMax(20.0f,
             (highlight_mgr_->max_bound - highlight_mgr_->min_bound).length() *
                 1.8f));
    update();
    return;
  }

  if (targetStartVertex != SIZE_MAX && targetMesh) {
    // Compute center of the element
    double cx = 0, cy = 0, cz = 0;
    int count = 0;

    size_t start = 0, end = 0;
    if (layerType == LayerType::kLanes) {
      if (type == TreeNodeType::kRoad) {
        start = targetStartVertex;
        end = network_mesh_.lanes_mesh.vertices.size();
        for (size_t i = start; i < network_mesh_.lanes_mesh.vertices.size();
             i++) {
          if (network_mesh_.lanes_mesh.get_road_id(i) != roadIdStr) {
            end = i;
            break;
          }
        }
      } else {
        auto interval =
            network_mesh_.lanes_mesh.get_idx_interval_lane(targetStartVertex);
        start = interval[0];
        end = interval[1];
      }
    } else if (layerType == LayerType::kObjects) {
      auto interval =
          network_mesh_.road_objects_mesh.get_idx_interval_road_object(
              targetStartVertex);
      start = interval[0];
      end = interval[1];
    } else if (layerType == LayerType::kSignalLights ||
               layerType == LayerType::kSignalSigns) {
      auto interval = network_mesh_.road_signals_mesh.get_idx_interval_signal(
          targetStartVertex);
      start = interval[0];
      end = interval[1];
    }

    for (size_t i = start; i < end && i < targetMesh->vertices.size(); i++) {
      cx += targetMesh->vertices[i][0];
      cy += targetMesh->vertices[i][1];
      cz += targetMesh->vertices[i][2];
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

void GeoViewerWidget::HighlightRoads(const QStringList& roadIds) {
  if (!map_ || roadIds.isEmpty()) {
    ClearHighlight();
    return;
  }
  std::set<std::string> ids;
  for (const auto& roadId : roadIds) {
    ids.insert(roadId.toStdString());
  }
  auto indices = CollectIndicesForCachedElements(
      LayerType::kLanes, lane_element_items_, network_mesh_.lanes_mesh.indices,
      [&](const SceneCachedElement& element) {
        const std::string prefix = "R:";
        return element.roadKey.rfind(prefix, 0) == 0 &&
               ids.count(element.roadKey.substr(prefix.size())) > 0;
      });
  SetHighlightIndices(indices, LayerType::kLanes);
}

void GeoViewerWidget::JumpToLocation(double lon, double lat, double alt) {
  if (!map_) return;

  double x = lon, y = lat, z = alt;
  try {
    CoordinateUtil::Instance().WGS84ToLocal(&x, &y, &z);

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
  } catch (const std::exception& e) {
    qDebug() << "Jump to location error:" << e.what();
  }
}

void GeoViewerWidget::GenerateRefLinePoints(
    std::shared_ptr<odr::OpenDriveMap> map, std::vector<float>& allVertices,
    std::map<std::string, VertRange>& ranges) {
  if (!map) return;
  for (const auto& [roadId, road] : map->id_to_road) {
    size_t startIdx = allVertices.size() / 3;
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
      allVertices.push_back(p1.x());
      allVertices.push_back(p1.y());
      allVertices.push_back(p1.z());
      allVertices.push_back(p2.x());
      allVertices.push_back(p2.y());
      allVertices.push_back(p2.z());
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
        allVertices.push_back(tri[0][0]);
        allVertices.push_back(tri[0][1]);
        allVertices.push_back(tri[0][2]);
        allVertices.push_back(tri[1][0]);
        allVertices.push_back(tri[1][1]);
        allVertices.push_back(tri[1][2]);

        allVertices.push_back(tri[1][0]);
        allVertices.push_back(tri[1][1]);
        allVertices.push_back(tri[1][2]);
        allVertices.push_back(tri[2][0]);
        allVertices.push_back(tri[2][1]);
        allVertices.push_back(tri[2][2]);

        allVertices.push_back(tri[2][0]);
        allVertices.push_back(tri[2][1]);
        allVertices.push_back(tri[2][2]);
        allVertices.push_back(tri[0][0]);
        allVertices.push_back(tri[0][1]);
        allVertices.push_back(tri[0][2]);
      }
    }

    ranges[roadId] = {startIdx, (allVertices.size() / 3) - startIdx};
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

bool GeoViewerWidget::IsJunctionVisible(const QString& groupId,
                                        const QString& junctionId) const {
  if (hidden_elements_.count(("JG:" + groupId).toStdString())) return false;
  if (!junctionId.isEmpty()) {
    if (hidden_elements_.count(
            ("J:" + groupId + ":" + junctionId).toStdString()))
      return false;
  } else {
    // Check group-level visibility (JG:groupId)
    if (hidden_elements_.count(("JG:" + groupId).toStdString())) return false;

    // Hide cluster overlay only when all child junctions are hidden.
    auto groupIndexit = junction_group_index_by_id_.find(groupId.toStdString());
    if (groupIndexit != junction_group_index_by_id_.end()) {
      const auto& group = junction_cluster_result_.groups[groupIndexit->second];
      if (!group.junction_ids.empty()) {
        bool all_children_hidden = true;
        for (const auto& jid : group.junction_ids) {
          std::string jKey = "J:" + group.group_id + ":" + jid;
          if (hidden_elements_.count(jKey) == 0) {
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
                                            const QMatrix4x4& viewProj) {
  if (!layers_[(int)LayerType::kJunctions].visible) return;
  if (junction_cluster_result_.groups.empty()) return;

  QPen ringPen(QColor(255, 200, 60, 220));
  ringPen.setWidth(2);
  QPen selectedPen(QColor(80, 255, 140, 240));
  selectedPen.setWidth(3);
  painter.setFont(QFont("Menlo", 9));

  for (const auto& group : junction_cluster_result_.groups) {
    const QString groupId = QString::fromStdString(group.group_id);
    if (!IsJunctionVisible(groupId)) continue;

    const QVector3D center = JunctionGroupCenter(group);
    QVector4D clipPos = viewProj * QVector4D(center, 1.0f);
    if (clipPos.w() <= 0.0f) continue;
    const float ndcX = clipPos.x() / clipPos.w();
    const float ndcY = clipPos.y() / clipPos.w();
    if (ndcX < -1.2f || ndcX > 1.2f || ndcY < -1.2f || ndcY > 1.2f) continue;

    const int sx = (int)((ndcX + 1.0f) * width() * 0.5f);
    const int sy = (int)((1.0f - ndcY) * height() * 0.5f);
    const bool selected = (selected_junction_group_id_ == groupId);

    painter.setPen(selected ? selectedPen : ringPen);
    painter.setBrush(selected ? QColor(80, 255, 140, 80)
                              : QColor(255, 200, 60, 50));
    painter.drawEllipse(QPointF(sx, sy), selected ? 9.0 : 7.0,
                        selected ? 9.0 : 7.0);

    for (const auto& junctionIdStr : group.junction_ids) {
      const QString junctionId = QString::fromStdString(junctionIdStr);
      if (!IsJunctionVisible(groupId, junctionId)) continue;
      auto memberIndexIt = junction_member_index_by_id_.find(junctionIdStr);
      if (memberIndexIt == junction_member_index_by_id_.end()) {
        continue;
      }
      const auto& member =
          junction_cluster_result_.junctions[memberIndexIt->second];
      if (!member.incoming_box.valid) {
        continue;
      }
      const odr::Vec3D local_center{
          (member.incoming_box.min[0] + member.incoming_box.max[0]) * 0.5,
          (member.incoming_box.min[1] + member.incoming_box.max[1]) * 0.5,
          (member.incoming_box.min[2] + member.incoming_box.max[2]) * 0.5};
      const QVector3D memberCenter = LocalToRendererPoint(local_center);
      QVector4D memberClip = viewProj * QVector4D(memberCenter, 1.0f);
      if (memberClip.w() <= 0.0f) continue;
      const int msx =
          (int)(((memberClip.x() / memberClip.w()) + 1.0f) * width() * 0.5f);
      const int msy =
          (int)((1.0f - (memberClip.y() / memberClip.w())) * height() * 0.5f);
      const bool memberSelected =
          selected && (selected_junction_id_.isEmpty() ||
                       selected_junction_id_ == junctionId);
      painter.setPen(Qt::NoPen);
      painter.setBrush(memberSelected ? QColor(80, 255, 140, 240)
                                      : QColor(255, 230, 160, 220));
      painter.drawEllipse(QPointF(msx, msy), memberSelected ? 4.0 : 3.0,
                          memberSelected ? 4.0 : 3.0);
    }

    const QString text = QString("%1 [%2]").arg(groupId).arg(
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
  emit measureModeChanged(active);
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
