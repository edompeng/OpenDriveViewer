#pragma once

#include <QVector3D>
#include <cstdint>
#include <string>
#include <vector>

struct SceneIndexRange {
  uint32_t start = 0;
  uint32_t count = 0;
};

struct SceneCachedElement {
  std::string road_key;
  std::string group_key;
  std::string element_key;
  std::vector<SceneIndexRange> ranges;
};

struct SceneOutlineElement : SceneCachedElement {
  bool is_dashed = false;
};

struct SceneMeshChunk {
  QVector3D min_bound{1e9f, 1e9f, 1e9f};
  QVector3D max_bound{-1e9f, -1e9f, -1e9f};
  size_t index_offset = 0;
  size_t index_count = 0;
};

struct SceneGridBox {
  QVector3D min_bound;
  QVector3D max_bound;
  std::vector<uint32_t> triangle_indices;
};
