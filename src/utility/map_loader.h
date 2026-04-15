#pragma once

#include <memory>
#include <string>
#include "src/utility/junction_grouping.h"
#include "src/utility/scene_enums.h"
#include "third_party/libOpenDRIVE/include/RoadNetworkMesh.h"

namespace odr {
class OpenDriveMap;
}

struct MapSceneData {
  std::shared_ptr<odr::OpenDriveMap> map;
  odr::RoadNetworkMesh mesh;
  JunctionClusterResult junction_grouping;
  bool georeference_valid = false;

  bool IsValid() const { return static_cast<bool>(map); }
  bool IsWgs84ModeAvailable() const { return georeference_valid; }
  CoordinateMode DefaultCoordinateMode() const {
    return georeference_valid ? CoordinateMode::kWGS84 : CoordinateMode::kLocal;
  }
};

class IMapSceneLoader {
 public:
  virtual ~IMapSceneLoader() = default;
  virtual MapSceneData Load(const std::string& path) const = 0;
};

class OpenDriveMapSceneLoader : public IMapSceneLoader {
 public:
  MapSceneData Load(const std::string& path) const override;
};
