#include "src/app/layer_tree_model.h"

#include "src/utility/viewer_text_util.h"

std::shared_ptr<LayerTreeSnapshot> BuildLayerTreeSnapshot(
    const std::shared_ptr<odr::OpenDriveMap>& map,
    const JunctionClusterResult& junction_result) {
  auto snapshot = std::make_shared<LayerTreeSnapshot>();
  if (!map) return snapshot;

  snapshot->junction_count = static_cast<int>(map->id_to_junction.size());
  snapshot->junction_groups.reserve(junction_result.groups.size());
  for (const auto& group : junction_result.groups) {
    JunctionGroupSnapshot group_snapshot;
    group_snapshot.group_id = QString::fromStdString(group.group_id);
    group_snapshot.label = QString("Group %1 [%2]")
                               .arg(group_snapshot.group_id)
                               .arg(JunctionClusterUtil::SemanticTypeToString(
                                   group.semantic_type));
    group_snapshot.junction_ids.reserve(group.junction_ids.size());
    for (const auto& junction_id : group.junction_ids) {
      group_snapshot.junction_ids.push_back(
          QString::fromStdString(junction_id));
    }
    snapshot->junction_groups.push_back(std::move(group_snapshot));
  }

  snapshot->roads.reserve(map->id_to_road.size());
  for (const auto& [road_id, road] : map->id_to_road) {
    RoadSnapshot road_snapshot;
    road_snapshot.road_id = QString::fromStdString(road_id);

    std::size_t lane_count = 0;
    for (const auto& [s0, section] : road.s_to_lanesection) {
      static_cast<void>(s0);
      lane_count += section.id_to_lane.size();
    }
    road_snapshot.lanes.reserve(lane_count);
    for (const auto& [s0, section] : road.s_to_lanesection) {
      const QString s0_str = QString::fromStdString(FormatSectionValue(s0));
      for (const auto& [lane_id, lane] : section.id_to_lane) {
        if (lane_id == 0) continue;  // Skip lane 0
        static_cast<void>(lane);
        road_snapshot.lanes.push_back(
            {QString("%1:%2").arg(s0_str).arg(lane_id),
             QString("kLane %1 @ s=%2").arg(lane_id).arg(s0_str),
             TreeNodeType::kLane});
      }
    }

    road_snapshot.objects.reserve(road.id_to_object.size());
    for (const auto& [object_id, object] : road.id_to_object) {
      road_snapshot.objects.push_back(
          {QString::fromStdString(object_id),
           QString("%1 (%2)").arg(object_id.c_str()).arg(object.type.c_str()),
           TreeNodeType::kObject});
    }

    for (const auto& [signal_id, signal] : road.id_to_signal) {
      RoadChildSnapshot child{
          QString::fromStdString(signal_id), QString::fromStdString(signal_id),
          signal.name == "TrafficLight" ? TreeNodeType::kLight
                                        : TreeNodeType::kSign};
      if (child.type == TreeNodeType::kLight) {
        road_snapshot.lights.push_back(std::move(child));
      } else {
        road_snapshot.signs.push_back(std::move(child));
      }
    }

    snapshot->roads.push_back(std::move(road_snapshot));
  }

  return snapshot;
}

Qt::CheckState ComputeJunctionGroupCheckState(
    const JunctionGroupSnapshot& group,
    const std::unordered_set<std::string>& hidden) {
  if (hidden.find("JG:" + group.group_id.toStdString()) != hidden.end()) {
    return Qt::Unchecked;
  }
  for (const auto& junction_id : group.junction_ids) {
    if (hidden.find("J:" + group.group_id.toStdString() + ":" +
                    junction_id.toStdString()) != hidden.end()) {
      return Qt::PartiallyChecked;
    }
  }
  return Qt::Checked;
}

Qt::CheckState ComputeRoadCheckState(
    const RoadSnapshot& road, const std::unordered_set<std::string>& hidden) {
  const std::string road_id = road.road_id.toStdString();
  if (hidden.find("R:" + road_id) != hidden.end()) return Qt::Unchecked;

  auto is_hidden = [&](const std::string& id) {
    return hidden.find(id) != hidden.end();
  };

  if (is_hidden("E:" + road_id + ":refline")) return Qt::PartiallyChecked;
  for (const auto& lane : road.lanes) {
    if (is_hidden("E:" + road_id + ":lane:" + lane.element_id.toStdString())) {
      return Qt::PartiallyChecked;
    }
  }
  if (!road.objects.empty() && is_hidden("G:" + road_id + ":objects")) {
    return Qt::PartiallyChecked;
  }
  for (const auto& object : road.objects) {
    if (is_hidden("E:" + road_id +
                  ":objects:" + object.element_id.toStdString())) {
      return Qt::PartiallyChecked;
    }
  }
  if (!road.lights.empty() && is_hidden("G:" + road_id + ":light")) {
    return Qt::PartiallyChecked;
  }
  for (const auto& light : road.lights) {
    if (is_hidden("E:" + road_id +
                  ":light:" + light.element_id.toStdString())) {
      return Qt::PartiallyChecked;
    }
  }
  if (!road.signs.empty() && is_hidden("G:" + road_id + ":sign")) {
    return Qt::PartiallyChecked;
  }
  for (const auto& sign : road.signs) {
    if (is_hidden("E:" + road_id + ":sign:" + sign.element_id.toStdString())) {
      return Qt::PartiallyChecked;
    }
  }
  return Qt::Checked;
}

QString BuildLayerTreeFullId(const QString& road_id, TreeNodeType type,
                             const QString& element_id) {
  if (type == TreeNodeType::kRoad) return "R:" + road_id;
  if (type == TreeNodeType::kJunctionGroup) return "JG:" + element_id;
  if (type == TreeNodeType::kJunction) return "J:" + road_id + ":" + element_id;
  if (type == TreeNodeType::kLane)
    return "E:" + road_id + ":lane:" + element_id;
  if (type == TreeNodeType::kRefLine) return "E:" + road_id + ":refline";
  if (type == TreeNodeType::kObjectGroup) return "G:" + road_id + ":objects";
  if (type == TreeNodeType::kLightGroup) return "G:" + road_id + ":light";
  if (type == TreeNodeType::kSignGroup) return "G:" + road_id + ":sign";
  if (type == TreeNodeType::kObject)
    return "E:" + road_id + ":objects:" + element_id;
  if (type == TreeNodeType::kLight)
    return "E:" + road_id + ":light:" + element_id;
  if (type == TreeNodeType::kSign)
    return "E:" + road_id + ":sign:" + element_id;
  return "";
}
