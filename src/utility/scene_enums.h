#pragma once

enum class LayerType {
  kLanes = 0,
  kLaneLines,
  kLaneLinesDashed,
  kRoadmarks,
  kObjects,
  kSignalLights,
  kSignalSigns,
  kRouting,
  kReferenceLines,
  kJunctions,
  kCount
};

enum class TreeNodeType {
  kRoad,
  kSectionGroup,
  kSection,
  kObjectGroup,
  kObject,
  kLightGroup,
  kLight,
  kSignGroup,
  kSign,
  kLane,
  kRefLine,
  kJunctionGroup,
  kJunction
};
enum class CoordinateMode { kWGS84, kLocal };
