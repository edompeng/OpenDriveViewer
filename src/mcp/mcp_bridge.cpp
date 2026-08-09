#include "src/mcp/mcp_bridge.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QMetaObject>
#include <QThread>
#include <QVector3D>
#include <cmath>

#include "OpenDriveMap.h"
#include "RoutingGraph.h"
#include "src/core/coordinate_util.h"
#include "src/core/scene_enums.h"
#include "src/ui/widgets/geo_viewer.h"

namespace geoviewer::mcp {

McpBridge::McpBridge(GeoViewerWidget* viewer_widget,
                     std::function<void(const QString&)> load_map_fn,
                     QObject* parent)
    : QObject(parent),
      viewer_widget_(viewer_widget),
      load_map_fn_(load_map_fn) {}

template <typename Func>
QJsonObject McpBridge::RunOnMainThread(Func&& func) {
  if (QThread::currentThread() == QCoreApplication::instance()->thread()) {
    return func();
  }

  QJsonObject result;
  QMetaObject::invokeMethod(
      this,
      [&result, f = std::forward<Func>(func)]() { result = f(); },
      Qt::BlockingQueuedConnection);
  return result;
}

// --- UI Operations ---

QJsonObject McpBridge::SetCamera(const QJsonObject& args) {
  return RunOnMainThread([this, &args]() -> QJsonObject {
    if (!viewer_widget_) {
      return QJsonObject{{"error", "Viewer widget not available"}};
    }
    const auto& camera = viewer_widget_->GetCamera();
    QVector3D target = camera.GetTarget();
    float yaw = camera.GetYaw();
    float pitch = camera.GetPitch();
    float distance = camera.GetDistance();

    if (args.contains("target_x"))
      target.setX(static_cast<float>(args.value("target_x").toDouble()));
    if (args.contains("target_y"))
      target.setY(static_cast<float>(args.value("target_y").toDouble()));
    if (args.contains("target_z"))
      target.setZ(static_cast<float>(args.value("target_z").toDouble()));
    if (args.contains("yaw"))
      yaw = static_cast<float>(args.value("yaw").toDouble());
    if (args.contains("pitch"))
      pitch = static_cast<float>(args.value("pitch").toDouble());
    if (args.contains("distance"))
      distance = static_cast<float>(args.value("distance").toDouble());

    viewer_widget_->SetCameraState(target, yaw, pitch, distance);

    QJsonObject res;
    res["status"] = "success";
    res["target_x"] = target.x();
    res["target_y"] = target.y();
    res["target_z"] = target.z();
    res["yaw"] = yaw;
    res["pitch"] = pitch;
    res["distance"] = distance;
    return res;
  });
}

QJsonObject McpBridge::JumpToLocation(const QJsonObject& args) {
  return RunOnMainThread([this, &args]() -> QJsonObject {
    if (!viewer_widget_) {
      return QJsonObject{{"error", "Viewer widget not available"}};
    }
    if (args.contains("lon") && args.contains("lat")) {
      double lon = args.value("lon").toDouble();
      double lat = args.value("lat").toDouble();
      double alt = args.value("alt").toDouble(0.0);
      viewer_widget_->JumpToLocation(lon, lat, alt);
      return QJsonObject{{"status", "success"}, {"mode", "wgs84"}};
    } else if (args.contains("x") && args.contains("y")) {
      double x = args.value("x").toDouble();
      double y = args.value("y").toDouble();
      double z = args.value("z").toDouble(0.0);
      viewer_widget_->JumpToLocalLocation(x, y, z);
      return QJsonObject{{"status", "success"}, {"mode", "local"}};
    }
    return QJsonObject{
        {"error", "Invalid arguments: provide (lon, lat) or (x, y)"}};
  });
}

QJsonObject McpBridge::SetViewMode(const QJsonObject& args) {
  return RunOnMainThread([this, &args]() -> QJsonObject {
    if (!viewer_widget_) {
      return QJsonObject{{"error", "Viewer widget not available"}};
    }
    QString mode_str = args.value("mode").toString().toLower();
    if (mode_str == "2d") {
      viewer_widget_->SetViewMode(CameraController::ViewMode::k2D);
    } else if (mode_str == "3d") {
      viewer_widget_->SetViewMode(CameraController::ViewMode::k3D);
    } else {
      return QJsonObject{{"error", "Invalid mode: expected '2d' or '3d'"}};
    }
    return QJsonObject{{"status", "success"}, {"mode", mode_str}};
  });
}

QJsonObject McpBridge::AddUserPoints(const QJsonObject& args) {
  return RunOnMainThread([this, &args]() -> QJsonObject {
    if (!viewer_widget_) {
      return QJsonObject{{"error", "Viewer widget not available"}};
    }
    QJsonArray points = args.value("points").toArray();
    if (points.isEmpty()) {
      return QJsonObject{{"error", "Points array is empty"}};
    }

    viewer_widget_->BeginUserPointsBatch();
    int added_count = 0;
    for (const QJsonValue& val : points) {
      QJsonObject pt = val.toObject();
      std::optional<QVector3D> color;
      if (pt.contains("color")) {
        QJsonArray col_arr = pt.value("color").toArray();
        if (col_arr.size() >= 3) {
          color = QVector3D(static_cast<float>(col_arr[0].toDouble()),
                            static_cast<float>(col_arr[1].toDouble()),
                            static_cast<float>(col_arr[2].toDouble()));
        }
      }

      if (pt.contains("lon") && pt.contains("lat")) {
        double lon = pt.value("lon").toDouble();
        double lat = pt.value("lat").toDouble();
        std::optional<double> alt;
        if (pt.contains("alt")) alt = pt.value("alt").toDouble();
        viewer_widget_->AddUserPoint(lon, lat, alt, color);
        added_count++;
      } else if (pt.contains("x") && pt.contains("y")) {
        double x = pt.value("x").toDouble();
        double y = pt.value("y").toDouble();
        std::optional<double> z;
        if (pt.contains("z")) z = pt.value("z").toDouble();
        viewer_widget_->AddUserPointLocal(x, y, z, color);
        added_count++;
      }
    }
    viewer_widget_->EndUserPointsBatch();

    return QJsonObject{{"status", "success"},
                       {"added_count", added_count},
                       {"total_points", viewer_widget_->UserPointCount()}};
  });
}

QJsonObject McpBridge::ClearUserPoints(const QJsonObject& args) {
  Q_UNUSED(args);
  return RunOnMainThread([this]() -> QJsonObject {
    if (!viewer_widget_) {
      return QJsonObject{{"error", "Viewer widget not available"}};
    }
    viewer_widget_->ClearUserPoints();
    return QJsonObject{{"status", "success"}};
  });
}

QJsonObject McpBridge::AddRoutingPath(const QJsonObject& args) {
  return RunOnMainThread([this, &args]() -> QJsonObject {
    if (!viewer_widget_) {
      return QJsonObject{{"error", "Viewer widget not available"}};
    }
    auto map = viewer_widget_->GetMap();
    if (!map) {
      return QJsonObject{{"error", "Map or routing graph not loaded"}};
    }

    double sx = 0, sy = 0, ex = 0, ey = 0;

    if (args.contains("start_x") && args.contains("end_x")) {
      sx = args.value("start_x").toDouble();
      sy = args.value("start_y").toDouble();
      ex = args.value("end_x").toDouble();
      ey = args.value("end_y").toDouble();
    } else if (args.contains("start_lon") && args.contains("end_lon")) {
      sx = args.value("start_lon").toDouble();
      sy = args.value("start_lat").toDouble();
      CoordinateUtil::Instance().WGS84ToLocal(&sx, &sy, nullptr);

      ex = args.value("end_lon").toDouble();
      ey = args.value("end_lat").toDouble();
      CoordinateUtil::Instance().WGS84ToLocal(&ex, &ey, nullptr);
    } else {
      return QJsonObject{
          {"error",
           "Provide (start_x, start_y, end_x, end_y) or (start_lon, "
           "start_lat, end_lon, end_lat)"}};
    }

    // Match start and end points to closest roads
    std::string start_road_id;
    std::string end_road_id;
    double min_start_dist = 1e9;
    double min_end_dist = 1e9;

    for (const auto& [id, road] : map->id_to_road) {
      double matched_s_start = road.ref_line.match(sx, sy);
      matched_s_start = std::max(0.0, std::min(matched_s_start, road.length));
      odr::Vec3D pt_s = road.get_xyz(matched_s_start, 0.0, 0.0);
      double dist_s = std::hypot(pt_s[0] - sx, pt_s[1] - sy);
      if (dist_s < min_start_dist) {
        min_start_dist = dist_s;
        start_road_id = id;
      }

      double matched_s_end = road.ref_line.match(ex, ey);
      matched_s_end = std::max(0.0, std::min(matched_s_end, road.length));
      odr::Vec3D pt_e = road.get_xyz(matched_s_end, 0.0, 0.0);
      double dist_e = std::hypot(pt_e[0] - ex, pt_e[1] - ey);
      if (dist_e < min_end_dist) {
        min_end_dist = dist_e;
        end_road_id = id;
      }
    }

    if (start_road_id.empty() || end_road_id.empty()) {
      return QJsonObject{{"error", "Could not map start/end points to road network"}};
    }

    odr::RoutingGraph rgraph = map->get_routing_graph();
    odr::LaneKey start_key{start_road_id, 0.0, -1};
    odr::LaneKey end_key{end_road_id, 0.0, -1};

    std::vector<odr::LaneKey> path = rgraph.shortest_path(start_key, end_key);
    if (path.empty()) {
      start_key.lane_id = 1;
      end_key.lane_id = 1;
      path = rgraph.shortest_path(start_key, end_key);
    }

    if (path.empty()) {
      return QJsonObject{{"error", "No route found between points"}};
    }

    QString name = args.value("name").toString("MCP Route");
    int path_id = viewer_widget_->AddRoutingPath(path, name);

    return QJsonObject{{"status", "success"},
                       {"route_id", path_id},
                       {"lane_count", static_cast<int>(path.size())}};
  });
}

QJsonObject McpBridge::ClearRoutingPaths(const QJsonObject& args) {
  Q_UNUSED(args);
  return RunOnMainThread([this]() -> QJsonObject {
    if (!viewer_widget_) {
      return QJsonObject{{"error", "Viewer widget not available"}};
    }
    viewer_widget_->ClearRoutingPaths();
    return QJsonObject{{"status", "success"}};
  });
}

QJsonObject McpBridge::HighlightElement(const QJsonObject& args) {
  return RunOnMainThread([this, &args]() -> QJsonObject {
    if (!viewer_widget_) {
      return QJsonObject{{"error", "Viewer widget not available"}};
    }
    QString road_id = args.value("road_id").toString();
    QString type_str = args.value("type").toString().toLower();
    QString element_id = args.value("element_id").toString();

    TreeNodeType type = TreeNodeType::kLane;
    if (type_str == "roadmark" || type_str == "object") {
      type = TreeNodeType::kObject;
    } else if (type_str == "signal") {
      type = TreeNodeType::kSign;
    } else if (type_str == "junction") {
      type = TreeNodeType::kJunction;
    }

    viewer_widget_->HighlightElement(road_id, type, element_id);
    return QJsonObject{{"status", "success"}};
  });
}

QJsonObject McpBridge::SetLayerVisibility(const QJsonObject& args) {
  return RunOnMainThread([this, &args]() -> QJsonObject {
    if (!viewer_widget_) {
      return QJsonObject{{"error", "Viewer widget not available"}};
    }
    QString layer_name = args.value("layer").toString().toLower();
    bool visible = args.value("visible").toBool(true);

    LayerType type = LayerType::kLanes;
    if (layer_name == "roadmark" || layer_name == "roadmarks") {
      type = LayerType::kRoadmarks;
    } else if (layer_name == "object" || layer_name == "objects") {
      type = LayerType::kObjects;
    } else if (layer_name == "signal" || layer_name == "signals") {
      type = LayerType::kSignalLights;
    } else if (layer_name == "junction" || layer_name == "junctions") {
      type = LayerType::kJunctions;
    }

    viewer_widget_->SetLayerVisible(type, visible);
    return QJsonObject{{"status", "success"},
                       {"layer", layer_name},
                       {"visible", visible}};
  });
}

QJsonObject McpBridge::LoadMap(const QJsonObject& args) {
  return RunOnMainThread([this, &args]() -> QJsonObject {
    if (!load_map_fn_) {
      return QJsonObject{{"error", "Load map function not available"}};
    }
    QString path = args.value("path").toString();
    if (path.isEmpty()) {
      return QJsonObject{{"error", "Map path is empty"}};
    }
    load_map_fn_(path);
    return QJsonObject{{"status", "success"}, {"path", path}};
  });
}

QJsonObject McpBridge::TakeScreenshot(const QJsonObject& args) {
  return RunOnMainThread([this, &args]() -> QJsonObject {
    if (!viewer_widget_) {
      return QJsonObject{{"error", "Viewer widget not available"}};
    }
    QImage screenshot = viewer_widget_->grabFramebuffer();
    if (screenshot.isNull()) {
      return QJsonObject{{"error", "Failed to capture framebuffer"}};
    }

    QString output_path = args.value("output_path").toString();
    if (!output_path.isEmpty()) {
      bool saved = screenshot.save(output_path);
      if (saved) {
        return QJsonObject{{"status", "success"}, {"output_path", output_path}};
      } else {
        return QJsonObject{{"error", "Failed to save screenshot to file"}};
      }
    }

    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    screenshot.save(&buffer, "PNG");
    QString base64_img = QString::fromLatin1(bytes.toBase64());

    QJsonObject content_item;
    content_item["type"] = "image";
    content_item["data"] = base64_img;
    content_item["mimeType"] = "image/png";

    return QJsonObject{{"status", "success"},
                       {"content", QJsonArray{content_item}}};
  });
}

// --- Data Query Operations ---

QJsonObject McpBridge::GetMapInfo(const QJsonObject& args) {
  Q_UNUSED(args);
  return RunOnMainThread([this]() -> QJsonObject {
    if (!viewer_widget_) {
      return QJsonObject{{"error", "Viewer widget not available"}};
    }
    auto map = viewer_widget_->GetMap();
    if (!map) {
      return QJsonObject{{"loaded", false}};
    }

    QJsonObject info;
    info["loaded"] = true;
    info["proj_string"] = QString::fromStdString(map->proj4);
    info["road_count"] = static_cast<int>(map->id_to_road.size());
    info["junction_count"] = static_cast<int>(map->id_to_junction.size());
    info["wgs84_available"] = viewer_widget_->IsGeoreferenceAvailable();
    info["right_hand_traffic"] = viewer_widget_->IsRightHandTraffic();
    return info;
  });
}

QJsonObject McpBridge::GetRoads(const QJsonObject& args) {
  return RunOnMainThread([this, &args]() -> QJsonObject {
    if (!viewer_widget_) {
      return QJsonObject{{"error", "Viewer widget not available"}};
    }
    auto map = viewer_widget_->GetMap();
    if (!map) {
      return QJsonObject{{"error", "Map not loaded"}};
    }

    int offset = args.value("offset").toInt(0);
    int limit = args.value("limit").toInt(100);

    QJsonArray roads_arr;
    const auto& roads_map = map->id_to_road;
    int count = 0;
    int idx = 0;
    for (const auto& [id, road] : roads_map) {
      if (idx++ < offset) continue;
      if (count++ >= limit) break;

      QJsonObject r_obj;
      r_obj["id"] = QString::fromStdString(road.id);
      r_obj["name"] = QString::fromStdString(road.name);
      r_obj["length"] = road.length;
      r_obj["junction"] = QString::fromStdString(road.junction);
      r_obj["lane_sections"] = static_cast<int>(road.get_lanesections().size());
      roads_arr.append(r_obj);
    }

    return QJsonObject{{"total", static_cast<int>(roads_map.size())},
                       {"offset", offset},
                       {"limit", limit},
                       {"roads", roads_arr}};
  });
}

QJsonObject McpBridge::GetRoadDetail(const QJsonObject& args) {
  return RunOnMainThread([this, &args]() -> QJsonObject {
    if (!viewer_widget_) {
      return QJsonObject{{"error", "Viewer widget not available"}};
    }
    auto map = viewer_widget_->GetMap();
    if (!map) {
      return QJsonObject{{"error", "Map not loaded"}};
    }

    std::string road_id = args.value("road_id").toString().toStdString();
    if (road_id.empty()) {
      return QJsonObject{{"error", "road_id is required"}};
    }

    const auto& roads = map->id_to_road;
    auto it = roads.find(road_id);
    if (it == roads.end()) {
      return QJsonObject{
          {"error", "Road not found: " + QString::fromStdString(road_id)}};
    }

    const auto& road = it->second;
    QJsonObject detail;
    detail["id"] = QString::fromStdString(road.id);
    detail["name"] = QString::fromStdString(road.name);
    detail["length"] = road.length;
    detail["junction"] = QString::fromStdString(road.junction);

    // Lane sections
    QJsonArray sections_arr;
    for (const auto& sec : road.get_lanesections()) {
      QJsonObject sec_obj;
      sec_obj["s0"] = sec.s0;
      QJsonArray lanes_arr;
      for (const auto& [lane_id, lane] : sec.id_to_lane) {
        QJsonObject lane_obj;
        lane_obj["id"] = lane.id;
        lane_obj["type"] = QString::fromStdString(lane.type);
        lanes_arr.append(lane_obj);
      }
      sec_obj["lanes"] = lanes_arr;
      sections_arr.append(sec_obj);
    }
    detail["lane_sections"] = sections_arr;

    // Signals
    QJsonArray signals_arr;
    for (const auto& [sig_id, sig] : road.id_to_signal) {
      QJsonObject sig_obj;
      sig_obj["id"] = QString::fromStdString(sig.id);
      sig_obj["name"] = QString::fromStdString(sig.name);
      sig_obj["type"] = QString::fromStdString(sig.type);
      sig_obj["s"] = sig.s0;
      sig_obj["t"] = sig.t0;
      signals_arr.append(sig_obj);
    }
    detail["signals"] = signals_arr;

    // Objects
    QJsonArray objects_arr;
    for (const auto& [obj_id, obj] : road.id_to_object) {
      QJsonObject obj_struct;
      obj_struct["id"] = QString::fromStdString(obj.id);
      obj_struct["name"] = QString::fromStdString(obj.name);
      obj_struct["type"] = QString::fromStdString(obj.type);
      obj_struct["s"] = obj.s0;
      obj_struct["t"] = obj.t0;
      objects_arr.append(obj_struct);
    }
    detail["objects"] = objects_arr;

    return detail;
  });
}

QJsonObject McpBridge::GetLaneGeometry(const QJsonObject& args) {
  return RunOnMainThread([this, &args]() -> QJsonObject {
    if (!viewer_widget_) {
      return QJsonObject{{"error", "Viewer widget not available"}};
    }
    auto map = viewer_widget_->GetMap();
    if (!map) {
      return QJsonObject{{"error", "Map not loaded"}};
    }

    std::string road_id = args.value("road_id").toString().toStdString();
    int lane_id = args.value("lane_id").toInt(0);
    double eps = args.value("eps").toDouble(1.0);

    const auto& roads = map->id_to_road;
    auto it = roads.find(road_id);
    if (it == roads.end()) {
      return QJsonObject{{"error", "Road not found"}};
    }

    const auto& road = it->second;
    QJsonArray pts_arr;
    for (const auto& sec : road.get_lanesections()) {
      const auto& lanes = sec.id_to_lane;
      auto lane_it = lanes.find(lane_id);
      if (lane_it == lanes.end()) continue;

      const auto& lane = lane_it->second;
      double s_start = args.value("s_start").toDouble(sec.s0);
      double s_end =
          args.value("s_end").toDouble(road.get_lanesection_end(sec.s0));

      for (double s = s_start; s <= s_end; s += eps) {
        const double lane_w = lane.lane_width.get(s);
        const double t_outer = lane.outer_border.get(s);
        const double t_center = t_outer - 0.5 * lane_w;
        odr::Vec3D pt = road.get_xyz(s, t_center, 0.0);

        QJsonObject pt_obj;
        pt_obj["x"] = pt[0];
        pt_obj["y"] = pt[1];
        pt_obj["z"] = pt[2];
        pt_obj["s"] = s;

        if (viewer_widget_->IsGeoreferenceAvailable()) {
          double lon, lat, alt;
          if (viewer_widget_->LocalToWGS84(pt[0], pt[1], pt[2], lon, lat,
                                           alt)) {
            pt_obj["lon"] = lon;
            pt_obj["lat"] = lat;
            pt_obj["alt"] = alt;
          }
        }
        pts_arr.append(pt_obj);
      }
    }

    return QJsonObject{{"road_id", QString::fromStdString(road_id)},
                       {"lane_id", lane_id},
                       {"points", pts_arr}};
  });
}

QJsonObject McpBridge::GetJunctions(const QJsonObject& args) {
  Q_UNUSED(args);
  return RunOnMainThread([this]() -> QJsonObject {
    if (!viewer_widget_) {
      return QJsonObject{{"error", "Viewer widget not available"}};
    }
    auto map = viewer_widget_->GetMap();
    if (!map) {
      return QJsonObject{{"error", "Map not loaded"}};
    }

    QJsonArray junctions_arr;
    for (const auto& [id, junc] : map->id_to_junction) {
      QJsonObject j_obj;
      j_obj["id"] = QString::fromStdString(junc.id);
      j_obj["name"] = QString::fromStdString(junc.name);

      QJsonArray conns_arr;
      for (const auto& [conn_id, conn] : junc.id_to_connection) {
        QJsonObject c_obj;
        c_obj["id"] = QString::fromStdString(conn.id);
        c_obj["incoming_road"] = QString::fromStdString(conn.incoming_road);
        c_obj["connecting_road"] =
            QString::fromStdString(conn.connecting_road);
        c_obj["contact_point"] =
            (conn.contact_point == odr::JunctionConnection::ContactPoint_Start)
                ? "start"
                : "end";
        conns_arr.append(c_obj);
      }
      j_obj["connections"] = conns_arr;
      junctions_arr.append(j_obj);
    }

    return QJsonObject{{"junctions", junctions_arr}};
  });
}

QJsonObject McpBridge::GetSignals(const QJsonObject& args) {
  return RunOnMainThread([this, &args]() -> QJsonObject {
    if (!viewer_widget_) {
      return QJsonObject{{"error", "Viewer widget not available"}};
    }
    auto map = viewer_widget_->GetMap();
    if (!map) {
      return QJsonObject{{"error", "Map not loaded"}};
    }

    std::string filter_road = args.value("road_id").toString().toStdString();

    QJsonArray signals_arr;
    for (const auto& [id, road] : map->id_to_road) {
      if (!filter_road.empty() && id != filter_road) continue;

      for (const auto& [sig_id, sig] : road.id_to_signal) {
        QJsonObject s_obj;
        s_obj["id"] = QString::fromStdString(sig.id);
        s_obj["name"] = QString::fromStdString(sig.name);
        s_obj["road_id"] = QString::fromStdString(id);
        s_obj["type"] = QString::fromStdString(sig.type);
        s_obj["subtype"] = QString::fromStdString(sig.subtype);
        s_obj["s"] = sig.s0;
        s_obj["t"] = sig.t0;

        odr::Vec3D pt = road.get_xyz(sig.s0, sig.t0, sig.zOffset);
        s_obj["x"] = pt[0];
        s_obj["y"] = pt[1];
        s_obj["z"] = pt[2];

        if (viewer_widget_->IsGeoreferenceAvailable()) {
          double lon, lat, alt;
          if (viewer_widget_->LocalToWGS84(pt[0], pt[1], pt[2], lon, lat,
                                           alt)) {
            s_obj["lon"] = lon;
            s_obj["lat"] = lat;
            s_obj["alt"] = alt;
          }
        }
        signals_arr.append(s_obj);
      }
    }

    return QJsonObject{{"signals", signals_arr}};
  });
}

QJsonObject McpBridge::GetObjects(const QJsonObject& args) {
  return RunOnMainThread([this, &args]() -> QJsonObject {
    if (!viewer_widget_) {
      return QJsonObject{{"error", "Viewer widget not available"}};
    }
    auto map = viewer_widget_->GetMap();
    if (!map) {
      return QJsonObject{{"error", "Map not loaded"}};
    }

    std::string filter_road = args.value("road_id").toString().toStdString();

    QJsonArray objects_arr;
    for (const auto& [id, road] : map->id_to_road) {
      if (!filter_road.empty() && id != filter_road) continue;

      for (const auto& [obj_id, obj] : road.id_to_object) {
        QJsonObject o_obj;
        o_obj["id"] = QString::fromStdString(obj.id);
        o_obj["name"] = QString::fromStdString(obj.name);
        o_obj["road_id"] = QString::fromStdString(id);
        o_obj["type"] = QString::fromStdString(obj.type);
        o_obj["s"] = obj.s0;
        o_obj["t"] = obj.t0;

        odr::Vec3D pt = road.get_xyz(obj.s0, obj.t0, obj.z0);
        o_obj["x"] = pt[0];
        o_obj["y"] = pt[1];
        o_obj["z"] = pt[2];

        if (viewer_widget_->IsGeoreferenceAvailable()) {
          double lon, lat, alt;
          if (viewer_widget_->LocalToWGS84(pt[0], pt[1], pt[2], lon, lat,
                                           alt)) {
            o_obj["lon"] = lon;
            o_obj["lat"] = lat;
            o_obj["alt"] = alt;
          }
        }
        objects_arr.append(o_obj);
      }
    }

    return QJsonObject{{"objects", objects_arr}};
  });
}

QJsonObject McpBridge::QueryPoint(const QJsonObject& args) {
  return RunOnMainThread([this, &args]() -> QJsonObject {
    if (!viewer_widget_) {
      return QJsonObject{{"error", "Viewer widget not available"}};
    }
    auto map = viewer_widget_->GetMap();
    if (!map) {
      return QJsonObject{{"error", "Map not loaded"}};
    }

    double tx = 0, ty = 0, tz = 0;

    if (args.contains("lon") && args.contains("lat")) {
      double lon = args.value("lon").toDouble();
      double lat = args.value("lat").toDouble();
      CoordinateUtil::Instance().WGS84ToLocal(&lon, &lat, nullptr);
      tx = lon;
      ty = lat;
      tz = args.value("alt").toDouble(0.0);
    } else if (args.contains("x") && args.contains("y")) {
      tx = args.value("x").toDouble();
      ty = args.value("y").toDouble();
      tz = args.value("z").toDouble(0.0);
    } else {
      return QJsonObject{
          {"error", "Provide (lon, lat) or (x, y) coordinates"}};
    }

    std::string best_road_id;
    double best_s = 0;
    double best_t = 0;
    double min_dist = 1e9;

    for (const auto& [id, road] : map->id_to_road) {
      double matched_s = road.ref_line.match(tx, ty);
      matched_s = std::max(0.0, std::min(matched_s, road.length));

      odr::Vec3D e_s, e_t, e_h;
      odr::Vec3D p0 = road.get_xyz(matched_s, 0.0, 0.0, &e_s, &e_t, &e_h);

      odr::Vec3D d = {tx - p0[0], ty - p0[1], tz - p0[2]};
      double t = d[0] * e_t[0] + d[1] * e_t[1] + d[2] * e_t[2];
      double dist = std::hypot(d[0], d[1]);

      if (dist < min_dist) {
        min_dist = dist;
        best_road_id = id;
        best_s = matched_s;
        best_t = t;
      }
    }

    QJsonObject res;
    res["road_id"] = QString::fromStdString(best_road_id);
    res["s"] = best_s;
    res["t"] = best_t;
    res["distance"] = min_dist;

    const auto& roads = map->id_to_road;
    auto it = roads.find(best_road_id);
    if (it != roads.end()) {
      odr::Vec3D e_s = it->second.ref_line.get_grad(best_s);
      res["heading"] = std::atan2(e_s[1], e_s[0]);
    }

    return res;
  });
}

QJsonObject McpBridge::CoordinateTransform(const QJsonObject& args) {
  QString from = args.value("from").toString().toLower();
  QString to = args.value("to").toString().toLower();

  if (from == "wgs84" && to == "local") {
    double lon = args.value("lon").toDouble(args.value("x").toDouble());
    double lat = args.value("lat").toDouble(args.value("y").toDouble());
    double alt = args.value("alt").toDouble(args.value("z").toDouble(0.0));
    double x = lon, y = lat;
    CoordinateUtil::Instance().WGS84ToLocal(&x, &y, nullptr);
    return QJsonObject{{"x", x}, {"y", y}, {"z", alt}};
  } else if (from == "local" && to == "wgs84") {
    double x = args.value("x").toDouble();
    double y = args.value("y").toDouble();
    double z = args.value("z").toDouble(0.0);
    double lon = x, lat = y;
    CoordinateUtil::Instance().LocalToWGS84(&lon, &lat, nullptr);
    return QJsonObject{{"lon", lon}, {"lat", lat}, {"alt", z}};
  }
  return QJsonObject{
      {"error",
       "Invalid transform direction: use from='wgs84'/to='local' or vice versa"}};
}

}  // namespace geoviewer::mcp
