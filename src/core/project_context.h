#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "src/core/scene_enums.h"
#include "src/geo_viewer_export.h"

namespace geoviewer::core {

class GEOVIEWER_EXPORT ProjectContext {
 public:
  static ProjectContext& Instance();

  // Map state
  void SetMapPath(const std::string& path);
  const std::string& MapPath() const { return map_path_; }

  // Visibility state
  void SetLayerVisible(LayerType type, bool visible);
  bool IsLayerVisible(LayerType type) const;

  void SetElementVisible(const std::string& id, bool visible);
  bool IsElementVisible(const std::string& id) const;

  // Selection state
  void SetSelectedElement(const std::string& road_id, TreeNodeType type,
                          const std::string& element_id);

  // Callbacks (Signals)
  using MapChangedCallback = std::function<void(const std::string&)>;
  using LayerVisibilityCallback = std::function<void(LayerType, bool)>;
  using SelectionCallback =
      std::function<void(const std::string&, TreeNodeType, const std::string&)>;

  void OnMapChanged(MapChangedCallback callback);
  void OnLayerVisibilityChanged(LayerVisibilityCallback callback);
  void OnSelectionChanged(SelectionCallback callback);

 private:
  ProjectContext() = default;
  ~ProjectContext() = default;
  ProjectContext(const ProjectContext&) = delete;
  ProjectContext& operator=(const ProjectContext&) = delete;

  std::string map_path_;
  std::unordered_map<LayerType, bool> layer_visibility_;
  std::unordered_map<std::string, bool> element_visibility_;

  std::vector<MapChangedCallback> map_changed_listeners_;
  std::vector<LayerVisibilityCallback> layer_listeners_;
  std::vector<SelectionCallback> selection_listeners_;
};

}  // namespace geoviewer::core
