#include "src/ui/widgets/geo_viewer.h"

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
      std::string r_id = network_mesh_->lanes_mesh.get_road_id(vi);
      double s0 = network_mesh_->lanes_mesh.get_lanesec_s0(vi);
      int l_id = network_mesh_->lanes_mesh.get_lane_id(vi);

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
      std::string r_id = network_mesh_->roadmarks_mesh.get_road_id(vi);
      type_str = "Roadmark";
      id_str = QString::fromStdString(
          network_mesh_->roadmarks_mesh.get_roadmark_type(vi));
      if (map_->id_to_road.count(r_id)) {
        const auto& r = map_->id_to_road.at(r_id);
        name_str = QString("kRoad: %1").arg(r.name.c_str());
      }
      ClearHighlight();
    } else if (picked_idx->layer == LayerType::kObjects) {
      std::string r_id = network_mesh_->road_objects_mesh.get_road_id(vi);
      std::string o_id =
          network_mesh_->road_objects_mesh.get_road_object_id(vi);
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
      std::string s_id =
          network_mesh_->road_signals_mesh.get_road_signal_id(vi);
      std::string r_id = GetRoadIdBySignalId(s_id);
      type_str = picked_idx->layer == LayerType::kSignalLights ? "TrafficLight"
                                                               : "TrafficSign";
      if (!r_id.empty() && map_->id_to_road.count(r_id)) {
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
            junction_mesh_->indices, [&](const SceneCachedElement& element) {
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
        find_id_str(network_mesh_->lanes_mesh.road_start_indices);
    target_mesh = &network_mesh_->lanes_mesh;
  } else if (type == LayerType::kObjects) {
    target_start_vertex =
        find_id_str(network_mesh_->road_objects_mesh.road_object_start_indices);
    target_mesh = &network_mesh_->road_objects_mesh;
  } else if (type == LayerType::kSignalLights ||
             type == LayerType::kSignalSigns) {
    target_start_vertex =
        find_id_str(network_mesh_->road_signals_mesh.road_signal_start_indices);
    target_mesh = &network_mesh_->road_signals_mesh;
    if (target_start_vertex != SIZE_MAX) {
      std::string r_id = GetRoadIdBySignalId(target_id);
      if (!r_id.empty() && map_->id_to_road.count(r_id)) {
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
