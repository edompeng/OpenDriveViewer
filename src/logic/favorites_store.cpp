#include "src/logic/favorites_store.h"

bool FavoritesStore::Add(const std::string& road_id, TreeNodeType type,
                         const std::string& element_id,
                         const std::string& display_name) {
  if (Find(road_id, type, element_id) >= 0) {
    return false;
  }
  entries_.push_back(FavoriteEntry{road_id, type, element_id, display_name});
  return true;
}

bool FavoritesStore::RemoveAt(int index) {
  if (index < 0 || index >= Size()) {
    return false;
  }
  entries_.erase(entries_.begin() + index);
  return true;
}

int FavoritesStore::Find(const std::string& road_id, TreeNodeType type,
                         const std::string& element_id) const {
  for (int i = 0; i < Size(); ++i) {
    const auto& entry = entries_[i];
    if (entry.road_id == road_id && entry.type == type &&
        entry.element_id == element_id) {
      return i;
    }
  }
  return -1;
}

const FavoriteEntry* FavoritesStore::At(int index) const {
  if (index < 0 || index >= Size()) {
    return nullptr;
  }
  return &entries_[index];
}
