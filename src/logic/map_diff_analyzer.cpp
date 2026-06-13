#include "src/logic/map_diff_analyzer.h"

#include <cmath>
#include <unordered_set>
#include <utility>

namespace geoviewer::logic {

namespace {

constexpr double kWidthTolerance = 0.05; // 5 cm

struct LaneKeyHash {
  size_t operator()(const odr::LaneKey& k) const {
    size_t h1 = std::hash<std::string>{}(k.road_id);
    size_t h2 = std::hash<double>{}(k.lanesection_s0);
    size_t h3 = std::hash<int>{}(k.lane_id);
    // Use boost-style hash combiner to avoid collisions
    size_t seed = h1;
    seed ^= h2 + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    seed ^= h3 + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    return seed;
  }
};

struct LaneKeyEqual {
  bool operator()(const odr::LaneKey& a, const odr::LaneKey& b) const {
    return a.road_id == b.road_id &&
           a.lanesection_s0 == b.lanesection_s0 &&
           a.lane_id == b.lane_id;
  }
};

}  // namespace

DiffResult MapDiffAnalyzer::Analyze(
    const std::shared_ptr<odr::OpenDriveMap>& base_map,
    const std::shared_ptr<odr::OpenDriveMap>& target_map) {
  DiffResult result;
  if (!base_map || !target_map) return result;

  std::vector<odr::LaneKey> base_keys;
  for (const auto& [road_id, road] : base_map->id_to_road) {
    for (const auto& [s0, section] : road.s_to_lanesection) {
      for (const auto& [lane_id, lane] : section.id_to_lane) {
        base_keys.push_back(odr::LaneKey(road_id, s0, lane_id));
      }
    }
  }

  std::vector<odr::LaneKey> target_keys;
  for (const auto& [road_id, road] : target_map->id_to_road) {
    for (const auto& [s0, section] : road.s_to_lanesection) {
      for (const auto& [lane_id, lane] : section.id_to_lane) {
        target_keys.push_back(odr::LaneKey(road_id, s0, lane_id));
      }
    }
  }

  std::unordered_set<odr::LaneKey, LaneKeyHash, LaneKeyEqual> base_set(
      base_keys.begin(), base_keys.end());
  std::unordered_set<odr::LaneKey, LaneKeyHash, LaneKeyEqual> target_set(
      target_keys.begin(), target_keys.end());

  // Added lanes
  for (const auto& key : target_keys) {
    if (base_set.find(key) == base_set.end()) {
      result.added_lanes.push_back(key);
    }
  }

  // Removed lanes
  for (const auto& key : base_keys) {
    if (target_set.find(key) == target_set.end()) {
      result.removed_lanes.push_back(key);
    }
  }

  // Modified lanes
  for (const auto& key : target_keys) {
    auto base_it = base_set.find(key);
    if (base_it != base_set.end()) {
      auto base_road_it = base_map->id_to_road.find(base_it->road_id);
      if (base_road_it == base_map->id_to_road.end()) continue;
      const auto& base_road = base_road_it->second;

      auto base_sec_it = base_road.s_to_lanesection.find(base_it->lanesection_s0);
      if (base_sec_it == base_road.s_to_lanesection.end()) continue;
      const auto& base_sec = base_sec_it->second;

      auto base_lane_it = base_sec.id_to_lane.find(base_it->lane_id);
      if (base_lane_it == base_sec.id_to_lane.end()) continue;
      const auto& base_lane = base_lane_it->second;

      auto target_road_it = target_map->id_to_road.find(key.road_id);
      if (target_road_it == target_map->id_to_road.end()) continue;
      const auto& target_road = target_road_it->second;

      auto target_sec_it = target_road.s_to_lanesection.find(key.lanesection_s0);
      if (target_sec_it == target_road.s_to_lanesection.end()) continue;
      const auto& target_sec = target_sec_it->second;

      auto target_lane_it = target_sec.id_to_lane.find(key.lane_id);
      if (target_lane_it == target_sec.id_to_lane.end()) continue;
      const auto& target_lane = target_lane_it->second;

      bool modified = false;
      if (base_lane.type != target_lane.type) {
        modified = true;
      }

      if (!modified) {
        double s_start = target_sec.s0;
        double s_end = target_road.get_lanesection_end(target_sec.s0);
        double w_start_base = base_lane.lane_width.get(s_start);
        double w_start_target = target_lane.lane_width.get(s_start);
        double w_end_base = base_lane.lane_width.get(s_end);
        double w_end_target = target_lane.lane_width.get(s_end);

        if (std::abs(w_start_base - w_start_target) > kWidthTolerance ||
            std::abs(w_end_base - w_end_target) > kWidthTolerance) {
          modified = true;
        }
      }

      if (modified) {
        result.modified_lanes.push_back(key);
      }
    }
  }

  return result;
}

}  // namespace geoviewer::logic
