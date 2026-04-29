#include "src/ui/widgets/geo_viewer.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QMenu>

#include "src/core/viewer_text_util.h"

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

  if (!network_mesh_) return;
  if (picked_idx.has_value() && map_) {
    size_t vi = picked_idx->vertex_index;
    if (picked_idx->layer == LayerType::kLanes) {
      std::string road_id = network_mesh_->lanes_mesh.get_road_id(vi);
      if (map_->id_to_road.count(road_id)) {
        info_text =
            QString("%1/%2/%3")
                .arg(road_id.c_str())
                .arg(network_mesh_->lanes_mesh.get_lanesec_s0(vi), 0, 'f', 2)
                .arg(network_mesh_->lanes_mesh.get_lane_id(vi));
      }
    } else if (picked_idx->layer == LayerType::kRoadmarks) {
      std::string road_id = network_mesh_->roadmarks_mesh.get_road_id(vi);
      if (map_->id_to_road.count(road_id)) {
        const auto& r = map_->id_to_road.at(road_id);
        std::string road_mark_type =
            network_mesh_->roadmarks_mesh.get_roadmark_type(vi);
        info_text = QString("Roadmark %1 in kRoad %2 (Name: %3)")
                        .arg(road_mark_type.c_str())
                        .arg(road_id.c_str())
                        .arg(r.name.c_str());
      }
    } else if (picked_idx->layer == LayerType::kObjects) {
      std::string road_id = network_mesh_->road_objects_mesh.get_road_id(vi);
      if (map_->id_to_road.count(road_id)) {
        const auto& r = map_->id_to_road.at(road_id);
        std::string object_id =
            network_mesh_->road_objects_mesh.get_road_object_id(vi);
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
      std::string signal_id =
          network_mesh_->road_signals_mesh.get_road_signal_id(vi);
      std::string road_id = GetRoadIdBySignalId(signal_id);

      if (map_->id_to_road.count(road_id)) {
        const auto& r = map_->id_to_road.at(road_id);
        if (r.id_to_signal.count(signal_id)) {
          const auto& sig = r.id_to_signal.at(signal_id);
          info_text = QString("Signal %1 (Name: %2, Type: %3) in kRoad %4")
                          .arg(signal_id.c_str())
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
  QAction* copy_info = nullptr;
  QAction* copy_all = nullptr;
  if (!info_text.isEmpty()) {
    copy_info = menu.addAction(tr("🏷️ Copy info: %1").arg(info_text));
    copy_all = menu.addAction(tr("📋 Copy all"));
  }

  QAction* hide_element = menu.addAction(tr("👁️ Hide current object"));
  QAction* add_fav = menu.addAction(tr("⭐ Add to favorites"));

  QAction* set_start_routing = nullptr;
  QAction* set_end_routing = nullptr;
  if (picked_idx && picked_idx->layer == LayerType::kLanes) {
    menu.addSeparator();
    set_start_routing = menu.addAction(tr("🚩 Set as routing start"));
    set_end_routing = menu.addAction(tr("🏁 Set as routing end"));
  }

  QAction* selected = menu.exec(ev->globalPos());
  if (selected == hide_element) {
    QString road_id, element_id, group;
    size_t vi = picked_idx->vertex_index;
    if (picked_idx->layer == LayerType::kLanes) {
      road_id =
          QString::fromStdString(network_mesh_->lanes_mesh.get_road_id(vi));
      double s0 = network_mesh_->lanes_mesh.get_lanesec_s0(vi);
      int lane_id = network_mesh_->lanes_mesh.get_lane_id(vi);
      group = "lane";
      element_id = QString::fromStdString(FormatSectionValue(s0)) + ":" +
                   QString::number(lane_id);
    } else if (picked_idx->layer == LayerType::kObjects) {
      road_id = QString::fromStdString(
          network_mesh_->road_objects_mesh.get_road_id(vi));
      element_id = QString::fromStdString(
          network_mesh_->road_objects_mesh.get_road_object_id(vi));
      group = "objects";
    } else if (picked_idx->layer == LayerType::kSignalLights) {
      std::string signal_id =
          network_mesh_->road_signals_mesh.get_road_signal_id(vi);
      element_id = QString::fromStdString(signal_id);
      road_id = QString::fromStdString(GetRoadIdBySignalId(signal_id));
      group = "lights";
    } else if (picked_idx->layer == LayerType::kSignalSigns) {
      std::string signal_id =
          network_mesh_->road_signals_mesh.get_road_signal_id(vi);
      element_id = QString::fromStdString(signal_id);
      road_id = QString::fromStdString(GetRoadIdBySignalId(signal_id));
      group = "signs";
    }
    if (!road_id.isEmpty()) {
      SetElementVisible(
          QString("E:%1:%2:%3").arg(road_id).arg(group).arg(element_id), false);
    }
  } else if (selected == copy_coord) {
    QApplication::clipboard()->setText(coord_text);
  } else if (selected == copy_info && copy_info) {
    QApplication::clipboard()->setText(info_text);
  } else if (selected == copy_all && copy_all) {
    QApplication::clipboard()->setText(
        QString("%1 | %2").arg(coord_text).arg(info_text));
  } else if (selected == add_fav) {
    QString road_id, element_id, name;
    TreeNodeType node_type = TreeNodeType::kRoad;
    size_t vi = picked_idx->vertex_index;
    if (picked_idx->layer == LayerType::kLanes) {
      road_id =
          QString::fromStdString(network_mesh_->lanes_mesh.get_road_id(vi));
      double s0 = network_mesh_->lanes_mesh.get_lanesec_s0(vi);
      int lane_id = network_mesh_->lanes_mesh.get_lane_id(vi);
      element_id = QString::fromStdString(FormatSectionValue(s0)) + ":" +
                   QString::number(lane_id);
      node_type = TreeNodeType::kLane;
      name = QString("kRoad %1 kLane %2").arg(road_id).arg(element_id);
    } else if (picked_idx->layer == LayerType::kObjects) {
      road_id = QString::fromStdString(
          network_mesh_->road_objects_mesh.get_road_id(vi));
      element_id = QString::fromStdString(
          network_mesh_->road_objects_mesh.get_road_object_id(vi));
      node_type = TreeNodeType::kObject;
      name = QString("kObject %1").arg(element_id);
    } else if (picked_idx->layer == LayerType::kSignalLights) {
      std::string signal_id =
          network_mesh_->road_signals_mesh.get_road_signal_id(vi);
      element_id = QString::fromStdString(signal_id);
      road_id = QString::fromStdString(GetRoadIdBySignalId(signal_id));
      node_type = TreeNodeType::kLight;
      name = QString("kLight %1").arg(element_id);
    } else if (picked_idx->layer == LayerType::kSignalSigns) {
      std::string signal_id =
          network_mesh_->road_signals_mesh.get_road_signal_id(vi);
      element_id = QString::fromStdString(signal_id);
      road_id = QString::fromStdString(GetRoadIdBySignalId(signal_id));
      node_type = TreeNodeType::kSign;
      name = QString("kSign %1").arg(element_id);
    }
    if (!road_id.isEmpty()) {
      emit AddFavoriteRequested(road_id, node_type, element_id, name);
    }
  } else if (selected == set_start_routing || selected == set_end_routing) {
    size_t vi = picked_idx->vertex_index;
    QString road_id =
        QString::fromStdString(network_mesh_->lanes_mesh.get_road_id(vi));
    double s0 = network_mesh_->lanes_mesh.get_lanesec_s0(vi);
    int lane_id = network_mesh_->lanes_mesh.get_lane_id(vi);
    const std::string lane_pos_std =
        BuildLanePosition(road_id.toStdString(), FormatSectionValue(s0),
                          QString::number(lane_id).toStdString());
    QString lane_pos = QString::fromStdString(lane_pos_std);
    if (selected == set_start_routing) {
      emit RoutingStartRequested(lane_pos.trimmed());
    } else {
      emit RoutingEndRequested(lane_pos.trimmed());
    }
  }
}
