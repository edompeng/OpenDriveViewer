#pragma once

#include <QHash>
#include <QString>
#include <memory>
#include <unordered_set>
#include <vector>
#include "src/core/junction_grouping.h"
#include "src/core/scene_enums.h"
#include "third_party/libOpenDRIVE/include/OpenDriveMap.h"

struct RoadChildSnapshot {
  QString element_id;
  QString label;
  TreeNodeType type = TreeNodeType::kLane;
};

struct RoadSnapshot {
  QString road_id;
  std::vector<RoadChildSnapshot> lanes;
  std::vector<RoadChildSnapshot> objects;
  std::vector<RoadChildSnapshot> lights;
  std::vector<RoadChildSnapshot> signs;
};

struct JunctionGroupSnapshot {
  QString group_id;
  QString label;
  std::vector<QString> junction_ids;
};

struct LayerTreeSnapshot {
  int junction_count = 0;
  std::vector<JunctionGroupSnapshot> junction_groups;
  std::vector<RoadSnapshot> roads;
};

std::shared_ptr<LayerTreeSnapshot> BuildLayerTreeSnapshot(
    const std::shared_ptr<odr::OpenDriveMap>& map,
    const JunctionClusterResult& junction_result);

Qt::CheckState ComputeJunctionGroupCheckState(
    const JunctionGroupSnapshot& group,
    const std::unordered_set<std::string>& hidden);

Qt::CheckState ComputeRoadCheckState(
    const RoadSnapshot& road, const std::unordered_set<std::string>& hidden);

QString BuildLayerTreeFullId(const QString& road_id, TreeNodeType type,
                             const QString& element_id);
