#include "src/core/project_context.h"

namespace geoviewer::core {

ProjectContext& ProjectContext::Instance() {
  static ProjectContext instance;
  return instance;
}

void ProjectContext::SetMapPath(const std::string& path) {
  if (map_path_ == path) return;
  map_path_ = path;
  for (const auto& callback : map_changed_listeners_) {
    callback(map_path_);
  }
}

void ProjectContext::SetLayerVisible(LayerType type, bool visible) {
  layer_visibility_[type] = visible;
  for (const auto& callback : layer_listeners_) {
    callback(type, visible);
  }
}

bool ProjectContext::IsLayerVisible(LayerType type) const {
  auto it = layer_visibility_.find(type);
  if (it != layer_visibility_.end()) {
    return it->second;
  }
  return true;  // Default visible
}

void ProjectContext::SetElementVisible(const std::string& id, bool visible) {
  element_visibility_[id] = visible;
  // Note: We could add an element visibility signal if needed
}

bool ProjectContext::IsElementVisible(const std::string& id) const {
  auto it = element_visibility_.find(id);
  if (it != element_visibility_.end()) {
    return it->second;
  }
  return true;
}

void ProjectContext::SetSelectedElement(const std::string& road_id,
                                        TreeNodeType type,
                                        const std::string& element_id) {
  for (const auto& callback : selection_listeners_) {
    callback(road_id, type, element_id);
  }
}

void ProjectContext::OnMapChanged(MapChangedCallback callback) {
  map_changed_listeners_.push_back(callback);
}

void ProjectContext::OnLayerVisibilityChanged(LayerVisibilityCallback callback) {
  layer_listeners_.push_back(callback);
}

void ProjectContext::OnSelectionChanged(SelectionCallback callback) {
  selection_listeners_.push_back(callback);
}

}  // namespace geoviewer::core
