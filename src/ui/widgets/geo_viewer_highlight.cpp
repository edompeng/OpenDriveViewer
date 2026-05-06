#include "src/ui/widgets/geo_viewer.h"

#include <QDebug>

#include <unordered_set>

#include "src/core/coordinate_util.h"
#include "src/core/viewer_text_util.h"
#include "src/logic/scene_index_builder.h"

void GeoViewerWidget::UpdateHighlight(size_t vert_idx, LayerType type) {
  if (!network_mesh_) return;
  size_t start = 0, end = 0;
  const odr::Mesh3D* mesh = nullptr;

  if (type == LayerType::kLanes) {
    auto interval = network_mesh_->lanes_mesh.get_idx_interval_lane(vert_idx);
    start = interval[0];
    end = interval[1];
    mesh = &network_mesh_->lanes_mesh;
  } else if (type == LayerType::kObjects) {
    auto interval =
        network_mesh_->road_objects_mesh.get_idx_interval_road_object(vert_idx);
    start = interval[0];
    end = interval[1];
    mesh = &network_mesh_->road_objects_mesh;
  } else if (type == LayerType::kFacilities) {
    if (facility_mesh_) {
      const size_t facility_vi = vert_idx;
      if (facility_vi < facility_mesh_->vertices.size()) {
        for (const auto& el : facility_element_items_) {
          bool found = false;
          for (const auto& range : el.ranges) {
            for (uint32_t k = 0; k < range.count * 3; ++k) {
              if (facility_mesh_->indices[range.start * 3 + k] ==
                  static_cast<uint32_t>(facility_vi)) {
                found = true;
                break;
              }
            }
            if (found) {
              mesh = facility_mesh_.get();
              start = 0;
              end = mesh->vertices.size();
              std::vector<uint32_t> indices;
              size_t v_offset =
                  gl_renderer_->GetLayerVertexOffset(LayerType::kFacilities);
              for (const auto& r : el.ranges) {
                for (uint32_t k = 0; k < r.count * 3; ++k) {
                  indices.push_back(facility_mesh_->indices[r.start * 3 + k] +
                                    static_cast<uint32_t>(v_offset));
                }
              }
              auto* highlight_mgr = gl_renderer_->GetHighlightManager();
              highlight_mgr->cur_start = vert_idx;
              highlight_mgr->cur_end = vert_idx + 1;
              highlight_mgr->cur_layer = type;
              SetHighlightIndices(indices, type, false, vert_idx);
              return;
            }
          }
        }
      }
      ClearHighlight();
      return;
    }
  } else if (type == LayerType::kSignalLights ||
             type == LayerType::kSignalSigns) {
    auto interval =
        network_mesh_->road_signals_mesh.get_idx_interval_signal(vert_idx);
    start = interval[0];
    end = interval[1];
    mesh = &network_mesh_->road_signals_mesh;
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

  // Optimized: Use cached element ranges instead of O(N) scan of all indices
  const std::vector<SceneCachedElement>* elements = nullptr;
  if (type == LayerType::kLanes) elements = &lane_element_items_;
  else if (type == LayerType::kRoadmarks) elements = &roadmark_element_items_;
  else if (type == LayerType::kObjects) elements = &object_element_items_;
  else if (type == LayerType::kSignalLights || type == LayerType::kSignalSigns) elements = &signal_element_items_;
  else if (type == LayerType::kJunctions) elements = &junction_element_items_;

  if (elements) {
    for (const auto& el : *elements) {
      // Find the element that contains this vertex index.
      // This is still a scan of elements, but much smaller than scanning millions of triangles.
      bool el_match = false;
      // Heuristic: for lanes/objects/signals, we can check if any vertex in the first range matches the interval.
      // A more robust way is checking the interval we got from get_idx_interval_*.
      if (!el.ranges.empty()) {
        const uint32_t first_v = mesh->indices[el.ranges[0].start * 3];
        if (first_v >= start && first_v < end) el_match = true;
      }

      if (el_match) {
        for (const auto& range : el.ranges) {
          for (uint32_t k = 0; k < range.count * 3; ++k) {
            indices.push_back(mesh->indices[range.start * 3 + k] + static_cast<uint32_t>(v_offset));
          }
        }
        break; // Found the matching element
      }
    }
  }

  SetHighlightIndices(indices, type, type == LayerType::kLanes, vert_idx);
}

void GeoViewerWidget::BuildJunctionPlanes() {
  junction_mesh_ = std::make_shared<odr::Mesh3D>();
  auto& junction_mesh = *junction_mesh_;
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

    const uint32_t base = static_cast<uint32_t>(junction_mesh.vertices.size());
    QVector3D center_renderer =
        LocalToRendererPoint({centroid[0], centroid[1], centroid[2] + 0.05});
    junction_mesh.vertices.push_back(
        {center_renderer.x(), center_renderer.y(), center_renderer.z()});
    junction_vertex_group_indices_.push_back(group_index);
    for (const auto& point : points) {
      QVector3D renderer =
          LocalToRendererPoint({point[0], point[1], point[2] + 0.05});
      junction_mesh.vertices.push_back(
          {renderer.x(), renderer.y(), renderer.z()});
      junction_vertex_group_indices_.push_back(group_index);
    }
    junction_group_centers_[group.group_id] = LocalToRendererPoint(centroid);

    const uint32_t tri_start =
        static_cast<uint32_t>(junction_mesh.indices.size() / 3);
    for (uint32_t i = 1; i <= points.size(); ++i) {
      const uint32_t next = (i == points.size()) ? 1 : (i + 1);
      junction_mesh.indices.insert(junction_mesh.indices.end(),
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
  if (!network_mesh_) return nullptr;
  if (type == LayerType::kLanes) return &network_mesh_->lanes_mesh;
  if (type == LayerType::kRoadmarks) return &network_mesh_->roadmarks_mesh;
  if (type == LayerType::kObjects) return &network_mesh_->road_objects_mesh;
  if (type == LayerType::kFacilities) return facility_mesh_.get();
  if (type == LayerType::kSignalLights || type == LayerType::kSignalSigns) {
    return &network_mesh_->road_signals_mesh;
  }
  if (type == LayerType::kJunctions)
    return junction_mesh_ ? junction_mesh_.get() : nullptr;
  return nullptr;
}

bool GeoViewerWidget::IsTrianglePickVisible(LayerType type,
                                            uint32_t triangle_index,
                                            size_t vertex_index) const {
  if (!network_mesh_) return false;
  std::string road_id;
  std::string element_id;
  std::string group;
  if (type == LayerType::kLanes) {
    road_id = network_mesh_->lanes_mesh.get_road_id(vertex_index);
    double s0 = network_mesh_->lanes_mesh.get_lanesec_s0(vertex_index);
    element_id = FormatSectionValue(s0);
    group = "section";
    if (!IsElementActuallyVisible(road_id, group, element_id)) return false;
    int lane_id = network_mesh_->lanes_mesh.get_lane_id(vertex_index);
    std::string lane_full_id =
        "E:" + road_id + ":lane:" + element_id + ":" + std::to_string(lane_id);
    return hidden_elements_.count(lane_full_id) == 0;
  } else if (type == LayerType::kRoadmarks) {
    road_id = network_mesh_->roadmarks_mesh.get_road_id(vertex_index);
    group = "section";
  } else if (type == LayerType::kObjects) {
    road_id = network_mesh_->road_objects_mesh.get_road_id(vertex_index);
    element_id =
        network_mesh_->road_objects_mesh.get_road_object_id(vertex_index);
    // Hide facility patches from picking
    if (IsFacility(road_id, element_id)) return false;
    group = "objects";
  } else if (type == LayerType::kFacilities) {
    // Manual facility ribbon pick
    for (const auto& el : facility_element_items_) {
      for (const auto& range : el.ranges) {
        if (triangle_index >= range.start &&
            triangle_index < (range.start + range.count)) {
          QStringList parts = QString::fromStdString(el.element_key).split(":");
          if (parts.size() >= 4) {
            road_id = parts[1].toStdString();
            element_id = parts[3].toStdString();
            group = "objects";
            return IsElementActuallyVisible(road_id, group, element_id);
          }
        }
      }
    }
    return false;
  } else if (type == LayerType::kSignalLights ||
             type == LayerType::kSignalSigns) {
    element_id =
        network_mesh_->road_signals_mesh.get_road_signal_id(vertex_index);
    road_id = GetRoadIdBySignalId(element_id);
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
  if (!indices.empty()) {
    const size_t v_offset = gl_renderer_->GetLayerVertexOffset(type);
    QVector3D min_b(std::numeric_limits<float>::max(),
                    std::numeric_limits<float>::max(),
                    std::numeric_limits<float>::max());
    QVector3D max_b(std::numeric_limits<float>::lowest(),
                    std::numeric_limits<float>::lowest(),
                    std::numeric_limits<float>::lowest());
    for (uint32_t global_idx : indices) {
      if (global_idx < v_offset) continue;
      size_t local_idx = static_cast<size_t>(global_idx - v_offset);
      const odr::Mesh3D* target_mesh = mesh;

      if (type == LayerType::kObjects && target_mesh &&
          local_idx >= target_mesh->vertices.size()) {
        // This was a hack for dual mesh, now we have separate layers.
        // But we'll keep it as a fallback or if kFacilities is mapped back.
        if (facility_mesh_) {
          local_idx -= target_mesh->vertices.size();
          target_mesh = facility_mesh_.get();
        }
      }

      if (!target_mesh || local_idx >= target_mesh->vertices.size()) continue;
      const auto& v = target_mesh->vertices[local_idx];
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
        network_mesh_->lanes_mesh.get_road_id(reference_vertex);
    const double s0 =
        network_mesh_->lanes_mesh.get_lanesec_s0(reference_vertex);
    const int lane_id = network_mesh_->lanes_mesh.get_lane_id(reference_vertex);
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
            n_indices.push_back(network_mesh_->lanes_mesh.indices[base + k] +
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
        layer_type, lane_element_items_, network_mesh_->lanes_mesh.indices,
        [&](const SceneCachedElement& element) {
          return element.road_key == ("R:" + road_id_str);
        });
    SetHighlightIndices(indices, layer_type);
    return;
  } else if (type == TreeNodeType::kSectionGroup) {
    layer_type = LayerType::kLanes;
    auto indices = CollectIndicesForCachedElements(
        layer_type, lane_element_items_, network_mesh_->lanes_mesh.indices,
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
        layer_type, lane_element_items_, network_mesh_->lanes_mesh.indices,
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
    auto indices = CollectIndicesForCachedElements(
        LayerType::kObjects, object_element_items_,
        network_mesh_->road_objects_mesh.indices,
        [&](const SceneCachedElement& element) {
          return element.group_key == ("G:" + road_id_str + ":objects");
        });
    if (!indices.empty()) {
      SetHighlightIndices(indices, LayerType::kObjects);
    }
    if (facility_mesh_) {
      auto f_indices = CollectIndicesForCachedElements(
          LayerType::kFacilities, facility_element_items_,
          facility_mesh_->indices, [&](const SceneCachedElement& element) {
            return element.group_key == ("G:" + road_id_str + ":objects");
          });
      if (!f_indices.empty()) {
        // Note: If both are non-empty, the last one wins in HighlightManager.
        SetHighlightIndices(f_indices, LayerType::kFacilities);
      }
    }
    return;
  } else if (type == TreeNodeType::kObject) {
    auto indices = CollectIndicesForCachedElements(
        LayerType::kObjects, object_element_items_,
        network_mesh_->road_objects_mesh.indices,
        [&](const SceneCachedElement& element) {
          return element.element_key ==
                 ("E:" + road_id_str + ":objects:" + element_id_str);
        });
    if (!indices.empty()) {
      SetHighlightIndices(indices, LayerType::kObjects);
    } else if (facility_mesh_) {
      auto f_indices = CollectIndicesForCachedElements(
          LayerType::kFacilities, facility_element_items_,
          facility_mesh_->indices, [&](const SceneCachedElement& element) {
            return element.element_key ==
                   ("E:" + road_id_str + ":objects:" + element_id_str);
          });
      if (!f_indices.empty()) {
        SetHighlightIndices(f_indices, LayerType::kFacilities);
      }
    }
    return;
  } else if (type == TreeNodeType::kLightGroup ||
             type == TreeNodeType::kSignGroup) {
    layer_type = (type == TreeNodeType::kLightGroup) ? LayerType::kSignalLights
                                                     : LayerType::kSignalSigns;
    const std::string group =
        (type == TreeNodeType::kLightGroup) ? "light" : "sign";
    auto indices = CollectIndicesForCachedElements(
        layer_type, signal_element_items_,
        network_mesh_->road_signals_mesh.indices,
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
        network_mesh_->road_signals_mesh.indices,
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
    target_mesh = &network_mesh_->lanes_mesh;
  else if (layer_type == LayerType::kObjects)
    target_mesh = &network_mesh_->road_objects_mesh;
  else if (layer_type == LayerType::kSignalLights ||
           layer_type == LayerType::kSignalSigns)
    target_mesh = &network_mesh_->road_signals_mesh;

  if (highlight_mgr && highlight_mgr->bounds_valid) {
    camera_.SetTarget((highlight_mgr->min_bound + highlight_mgr->max_bound) *
                      0.5f);
    camera_.SetPitch(-60.0f);
    camera_.SetYaw(45.0f);
    camera_.SetDistance(qMax(
        20.0f,
        (highlight_mgr->max_bound - highlight_mgr->min_bound).length() * 1.8f));
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
        end = network_mesh_->lanes_mesh.vertices.size();
        for (size_t i = start; i < network_mesh_->lanes_mesh.vertices.size();
             i++) {
          if (network_mesh_->lanes_mesh.get_road_id(i) != road_id_str) {
            end = i;
            break;
          }
        }
      } else {
        auto interval = network_mesh_->lanes_mesh.get_idx_interval_lane(
            target_start_vertex);
        start = interval[0];
        end = interval[1];
      }
    } else if (layer_type == LayerType::kObjects) {
      auto interval =
          network_mesh_->road_objects_mesh.get_idx_interval_road_object(
              target_start_vertex);
      start = interval[0];
      end = interval[1];
    } else if (layer_type == LayerType::kSignalLights ||
               layer_type == LayerType::kSignalSigns) {
      auto interval = network_mesh_->road_signals_mesh.get_idx_interval_signal(
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
  std::unordered_set<std::string> ids;
  ids.reserve(static_cast<std::size_t>(road_ids.size()));
  for (const auto& road_id : road_ids) {
    ids.insert(road_id.toStdString());
  }
  auto indices = CollectIndicesForCachedElements(
      LayerType::kLanes, lane_element_items_, network_mesh_->lanes_mesh.indices,
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

  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
    qWarning() << "JumpToLocalLocation: Invalid coordinates (NaN or Inf)";
    return;
  }

  camera_.SetTarget(
      QVector3D(static_cast<float>(x), static_cast<float>(z), rz));
  camera_.SetPitch(-89.0f);  // Look straight down
  camera_.SetYaw(0.0f);
  camera_.SetDistance(100.0f);  // Zoom level
  update();
}
