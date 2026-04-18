#include "src/logic/scene_manager.h"
#include <iostream>

namespace geoviewer::logic {

SceneManager::SceneManager() : right_hand_traffic_(true) {}

SceneManager::~SceneManager() = default;

bool SceneManager::LoadMap(const std::string& file_path) {
  try {
    map_ = std::make_shared<odr::OpenDriveMap>(file_path);
    UpdateMesh();
    return true;
  } catch (const std::exception& e) {
    std::cerr << "Failed to load map: " << e.what() << std::endl;
    return false;
  }
}

void SceneManager::UpdateMesh() {
  if (!map_) return;
  // Use a reasonable resolution for mesh generation
  network_mesh_ = map_->get_road_network_mesh(0.1);
}

}  // namespace geoviewer::logic
