#pragma once

#include <memory>
#include <string>
#include <vector>

#include "OpenDriveMap.h"
#include "RoadNetworkMesh.h"

namespace geoviewer::logic {

class SceneManager {
 public:
  SceneManager();
  ~SceneManager();

  bool LoadMap(const std::string& file_path);
  
  std::shared_ptr<odr::OpenDriveMap> GetMap() const { return map_; }
  const odr::RoadNetworkMesh& GetRoadNetworkMesh() const { return network_mesh_; }

  // Geometry generation
  void UpdateMesh();

  // Coordinates
  void SetRightHandTraffic(bool rht) { right_hand_traffic_ = rht; }
  bool IsRightHandTraffic() const { return right_hand_traffic_; }

 private:
  std::shared_ptr<odr::OpenDriveMap> map_;
  odr::RoadNetworkMesh network_mesh_;
  bool right_hand_traffic_ = true;
};

}  // namespace geoviewer::logic
