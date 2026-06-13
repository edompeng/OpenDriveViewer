#pragma once

#include <functional>
#include <memory>
#include <string>
#include "RoadNetworkMesh.h"
#include "src/core/junction_grouping.h"

namespace odr {
class OpenDriveMap;
class RoutingGraph;
}  // namespace odr

struct MapSceneData {
  std::shared_ptr<odr::OpenDriveMap> map;
  odr::RoadNetworkMesh mesh;
  JunctionClusterResult junction_grouping;
  std::shared_ptr<odr::RoutingGraph> routing_graph;
  bool georeference_valid = false;

  bool IsValid() const { return static_cast<bool>(map); }
  bool IsWgs84ModeAvailable() const { return georeference_valid; }
};

class IMapSceneLoader {
 public:
  virtual ~IMapSceneLoader() = default;
  virtual MapSceneData Load(const std::string& path,
                            std::function<void(float, const std::string&)>
                                progress_callback = nullptr) const = 0;
};

class OpenDriveMapSceneLoader : public IMapSceneLoader {
 public:
  MapSceneData Load(const std::string& path,
                    std::function<void(float, const std::string&)>
                        progress_callback = nullptr) const override;
};
