
#include "src/core/scene_enums.h"
#include <unordered_map>
std::string LayerTypeToString(const LayerType& type) noexcept {
  static const std::unordered_map<LayerType, std::string> kMapping = {
      {LayerType::kLanes, "Lanes"},
      {LayerType::kLaneLines, "Lines"},
      {LayerType::kRoadmarks, "Marks"},
      {LayerType::kObjects, "Objects"},
      {LayerType::kFacilities, "Facilities"},
      {LayerType::kSignalLights, "SignalLights"},
      {LayerType::kSignalSigns, "Signals"},
      {LayerType::kReferenceLines, "RefLines"},
      {LayerType::kJunctions, "Junctions"},
  };
  auto itr = kMapping.find(type);
  if (itr == kMapping.end()) {
    return "";
  }
  return itr->second;
}

LayerType StringToLayerType(const std::string& type_str) noexcept {
  static const std::unordered_map<std::string, LayerType> kMapping = {
      {"Lanes", LayerType::kLanes},
      {"Lines", LayerType::kLaneLines},
      {"Marks", LayerType::kRoadmarks},
      {"Objects", LayerType::kObjects},
      {"Facilities", LayerType::kFacilities},
      {"SignalLights", LayerType::kSignalLights},
      {"Signals", LayerType::kSignalSigns},
      {"RefLines", LayerType::kReferenceLines},
      {"Junctions", LayerType::kJunctions},
  };
  auto itr = kMapping.find(type_str);
  if (itr == kMapping.end()) {
    return LayerType::kCount;
  }
  return itr->second;
}
