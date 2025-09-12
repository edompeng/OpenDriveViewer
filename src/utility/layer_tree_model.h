#pragma once

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>
#include "src/utility/junction_grouping.h"
#include "src/utility/scene_enums.h"
#include "third_party/libOpenDRIVE/include/OpenDriveMap.h"

enum class TriCheckState {
  kUnchecked = 0,
  kPartiallyChecked,
  kChecked,
};

struct RoadChildSnapshot {
  std::string element_id;
  std::string label;
  TreeNodeType type = TreeNodeType::kLane;
};

struct RoadSnapshot {
  std::string road_id;
  std::vector<RoadChildSnapshot> lanes;
  std::vector<RoadChildSnapshot> objects;
  std::vector<RoadChildSnapshot> lights;
  std::vector<RoadChildSnapshot> signs;
};

struct JunctionGroupSnapshot {
  std::string group_id;
  std::string label;
  std::vector<std::string> junction_ids;
};

struct LayerTreeSnapshot {
  int junction_count = 0;
  std::vector<JunctionGroupSnapshot> junction_groups;
  std::vector<RoadSnapshot> roads;
};

std::shared_ptr<LayerTreeSnapshot> BuildLayerTreeSnapshot(
    const std::shared_ptr<odr::OpenDriveMap>& map,
    const JunctionClusterResult& junction_result);

TriCheckState ComputeJunctionGroupCheckState(
    const JunctionGroupSnapshot& group,
    const std::unordered_set<std::string>& hidden);

TriCheckState ComputeRoadCheckState(
    const RoadSnapshot& road, const std::unordered_set<std::string>& hidden);

std::string BuildLayerTreeFullId(const std::string& road_id, TreeNodeType type,
                                 const std::string& element_id);
