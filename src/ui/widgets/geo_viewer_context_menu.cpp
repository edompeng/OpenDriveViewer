#include "src/core/scene_enums.h"
#include "src/ui/widgets/geo_viewer.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QMenu>
#include <sstream>

#include "src/core/viewer_text_util.h"

void GeoViewerWidget::contextMenuEvent(QContextMenuEvent* ev) {
  camera_.EndDrag();
  QVector3D world_pos;
  std::optional<PickResult> picked_idx;
  double hdg_val = 0.0;
  double s_val = 0.0;
  double t_val = 0.0;
  bool has_lane_info = false;
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

        const auto& road = map_->id_to_road.at(road_id);
        double lx, ly, lz;
        RendererToLocalCoord(world_pos, lx, ly, lz);
        double matched_s = road.ref_line.match(lx, ly);
        if (matched_s < 0.0) {
          matched_s = 0.0;
        } else if (matched_s > road.length) {
          matched_s = road.length;
        }
        s_val = matched_s;

        odr::Vec3D e_s, e_t, e_h;
        odr::Vec3D p0 = road.get_xyz(matched_s, 0.0, 0.0, &e_s, &e_t, &e_h);

        odr::Vec3D d = {lx - p0[0], ly - p0[1], lz - p0[2]};
        t_val = d[0] * e_t[0] + d[1] * e_t[1] + d[2] * e_t[2];

        hdg_val = std::atan2(e_s[1], e_s[0]);
        has_lane_info = true;
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

  QAction* copy_heading = nullptr;
  QAction* copy_st = nullptr;
  QString heading_text = QString::number(hdg_val, 'f', 6);
  QString st_text =
      QString("%1, %2").arg(s_val, 0, 'f', 3).arg(t_val, 0, 'f', 3);

  if (has_lane_info) {
    copy_heading =
        menu.addAction(tr("📋 Copy heading: %1 rad").arg(heading_text));
    copy_st = menu.addAction(tr("📋 Copy s, t: %1").arg(st_text));
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

  QAction* show_lane_xml = nullptr;
  QAction* show_road_xml = nullptr;
  QAction* show_object_xml = nullptr;
  QAction* show_signal_xml = nullptr;

  if (picked_idx && map_) {
    menu.addSeparator();
    if (picked_idx->layer == LayerType::kLanes) {
      show_lane_xml = menu.addAction(tr("Show Lane XML"));
      show_road_xml = menu.addAction(tr("Show Road XML"));
    } else if (picked_idx->layer == LayerType::kObjects ||
               picked_idx->layer == LayerType::kRoadmarks ||
               picked_idx->layer == LayerType::kFacilities) {
      show_object_xml = menu.addAction(tr("Show Object XML"));
      show_road_xml = menu.addAction(tr("Show Road XML"));
    } else if (picked_idx->layer == LayerType::kSignalLights ||
               picked_idx->layer == LayerType::kSignalSigns) {
      show_signal_xml = menu.addAction(tr("Show Signal XML"));
      show_road_xml = menu.addAction(tr("Show Road XML"));
    } else if (picked_idx->layer == LayerType::kRoadmarks) {
      show_road_xml = menu.addAction(tr("Show Road XML"));
    }
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
    QString all_text = QString("%1 | %2").arg(coord_text).arg(info_text);
    if (has_lane_info) {
      all_text +=
          QString(" | Hdg: %1 rad | s,t: (%2)").arg(heading_text).arg(st_text);
    }
    QApplication::clipboard()->setText(all_text);
  } else if (selected == copy_heading && copy_heading) {
    QApplication::clipboard()->setText(heading_text);
  } else if (selected == copy_st && copy_st) {
    QApplication::clipboard()->setText(st_text);
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
  } else if (selected != nullptr &&
             (selected == set_start_routing || selected == set_end_routing)) {
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
  } else if (selected != nullptr &&
             (selected == show_lane_xml || selected == show_road_xml ||
              selected == show_object_xml || selected == show_signal_xml)) {
    size_t vi = picked_idx->vertex_index;
    std::string xml_str;
    geoviewer::ui::XmlTarget xml_target;

    if (selected == show_road_xml) {
      std::string road_id;
      if (picked_idx->layer == LayerType::kLanes) {
        road_id = network_mesh_->lanes_mesh.get_road_id(vi);
      } else if (picked_idx->layer == LayerType::kRoadmarks) {
        road_id = network_mesh_->roadmarks_mesh.get_road_id(vi);
      } else if (picked_idx->layer == LayerType::kObjects ||
                 picked_idx->layer == LayerType::kFacilities ||
                 picked_idx->layer == LayerType::kRoadmarks) {
        road_id = network_mesh_->road_objects_mesh.get_road_id(vi);
      } else if (picked_idx->layer == LayerType::kSignalLights ||
                 picked_idx->layer == LayerType::kSignalSigns) {
        std::string signal_id =
            network_mesh_->road_signals_mesh.get_road_signal_id(vi);
        road_id = GetRoadIdBySignalId(signal_id);
      }

      if (!road_id.empty()) {
        xml_target.type = geoviewer::ui::XmlTargetType::kRoad;
        xml_target.road_id = road_id;
        for (pugi::xml_node node :
             map_->xml_doc.child("OpenDRIVE").children("road")) {
          if (node.attribute("id").value() == road_id) {
            std::stringstream ss;
            node.print(ss, "  ");
            xml_str = ss.str();
            break;
          }
        }
      }
    } else if (selected == show_lane_xml) {
      std::string road_id = network_mesh_->lanes_mesh.get_road_id(vi);
      double s0 = network_mesh_->lanes_mesh.get_lanesec_s0(vi);
      int lane_id = network_mesh_->lanes_mesh.get_lane_id(vi);

      xml_target.type = geoviewer::ui::XmlTargetType::kLane;
      xml_target.road_id = road_id;
      xml_target.lane_s0 = s0;
      xml_target.lane_id = lane_id;

      pugi::xml_node road_node;
      for (pugi::xml_node r_node :
           map_->xml_doc.child("OpenDRIVE").children("road")) {
        if (r_node.attribute("id").value() == road_id) {
          road_node = r_node;
          break;
        }
      }
      if (road_node) {
        pugi::xml_node lanes_node = road_node.child("lanes");
        pugi::xml_node target_sec_node;
        for (pugi::xml_node sec_node : lanes_node.children("laneSection")) {
          double s_val = sec_node.attribute("s").as_double();
          if (std::abs(s_val - s0) < 1e-3) {
            target_sec_node = sec_node;
            break;
          }
        }
        if (target_sec_node) {
          pugi::xml_node target_lane_node;
          for (pugi::xml_node side_node :
               {target_sec_node.child("left"), target_sec_node.child("center"),
                target_sec_node.child("right")}) {
            if (!side_node) continue;
            for (pugi::xml_node lane_node : side_node.children("lane")) {
              if (lane_node.attribute("id").as_int() == lane_id) {
                target_lane_node = lane_node;
                break;
              }
            }
            if (target_lane_node) break;
          }
          if (target_lane_node) {
            std::stringstream ss;
            target_lane_node.print(ss, "  ");
            xml_str = ss.str();
          }
        }
      }
    } else if (selected == show_object_xml) {
      std::string road_id = network_mesh_->road_objects_mesh.get_road_id(vi);
      std::string object_id =
          network_mesh_->road_objects_mesh.get_road_object_id(vi);

      xml_target.type = geoviewer::ui::XmlTargetType::kObject;
      xml_target.road_id = road_id;
      xml_target.element_id = object_id;

      pugi::xml_node road_node;
      for (pugi::xml_node r_node :
           map_->xml_doc.child("OpenDRIVE").children("road")) {
        if (r_node.attribute("id").value() == road_id) {
          road_node = r_node;
          break;
        }
      }
      if (road_node) {
        pugi::xml_node objs_node = road_node.child("objects");
        pugi::xml_node target_obj_node;
        for (pugi::xml_node obj_node : objs_node.children("object")) {
          if (obj_node.attribute("id").value() == object_id) {
            target_obj_node = obj_node;
            break;
          }
        }
        if (target_obj_node) {
          std::stringstream ss;
          target_obj_node.print(ss, "  ");
          xml_str = ss.str();
        }
      }
    } else if (selected == show_signal_xml) {
      std::string signal_id =
          network_mesh_->road_signals_mesh.get_road_signal_id(vi);
      std::string road_id = GetRoadIdBySignalId(signal_id);

      xml_target.type = geoviewer::ui::XmlTargetType::kSignal;
      xml_target.road_id = road_id;
      xml_target.element_id = signal_id;

      pugi::xml_node road_node;
      for (pugi::xml_node r_node :
           map_->xml_doc.child("OpenDRIVE").children("road")) {
        if (r_node.attribute("id").value() == road_id) {
          road_node = r_node;
          break;
        }
      }
      if (road_node) {
        pugi::xml_node sigs_node = road_node.child("signals");
        pugi::xml_node target_sig_node;
        for (pugi::xml_node sig_node : sigs_node.children("signal")) {
          if (sig_node.attribute("id").value() == signal_id) {
            target_sig_node = sig_node;
            break;
          }
        }
        if (target_sig_node) {
          std::stringstream ss;
          target_sig_node.print(ss, "  ");
          xml_str = ss.str();
        }
      }
    }

    if (!xml_str.empty()) {
      emit ShowXmlRequested(xml_target, QString::fromStdString(xml_str));
    }
  }
}
