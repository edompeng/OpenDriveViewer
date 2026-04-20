#pragma once

#include <string>
#include <vector>
#include "src/core/scene_enums.h"
#include "src/geo_viewer_export.h"

struct GEOVIEWER_EXPORT FavoriteEntry {
  std::string road_id;
  TreeNodeType type = TreeNodeType::kRoad;
  std::string element_id;
  std::string display_name;
};

class GEOVIEWER_EXPORT FavoritesStore {
 public:
  bool Add(const std::string& road_id, TreeNodeType type,
           const std::string& element_id, const std::string& display_name);
  bool RemoveAt(int index);
  int Find(const std::string& road_id, TreeNodeType type,
           const std::string& element_id) const;
  const FavoriteEntry* At(int index) const;
  const std::vector<FavoriteEntry>& Entries() const { return entries_; }
  int Size() const { return static_cast<int>(entries_.size()); }

 private:
  std::vector<FavoriteEntry> entries_;
};
