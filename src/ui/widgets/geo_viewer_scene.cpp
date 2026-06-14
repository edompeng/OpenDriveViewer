#include <QtGui/qvectornd.h>
#include "src/logic/simulation_controller.h"
#include "src/ui/widgets/geo_viewer.h"

#include <Math.hpp>
#include <future>
#include <limits>
#include <tuple>
#include <vector>
#include "src/core/thread_pool.h"
#include "src/core/viewer_text_util.h"

namespace {

template <typename T>
struct FastMapLookup {
  const std::map<size_t, T>* map_ptr = nullptr;
  size_t last_start = 0;
  size_t last_end = 0;
  T last_val{};
  bool has_last = false;

  void Init(const std::map<size_t, T>& m) {
    map_ptr = &m;
    last_start = 0;
    last_end = 0;
    has_last = false;
  }

  T Lookup(size_t idx) {
    if (has_last && idx >= last_start && idx < last_end) {
      return last_val;
    }
    if (!map_ptr || map_ptr->empty()) return T{};

    auto it = map_ptr->upper_bound(idx);
    if (it != map_ptr->begin()) {
      auto prev_it = std::prev(it);
      last_start = prev_it->first;
      last_val = prev_it->second;
      if (it != map_ptr->end()) {
        last_end = it->first;
      } else {
        last_end = std::numeric_limits<size_t>::max();
      }
      has_last = true;
      return last_val;
    }
    return T{};
  }
};

template <typename ItemContainer, typename ItemMap, typename Key,
          typename Factory>
void AddTriangleRange(ItemContainer& items, ItemMap& item_map, const Key& key,
                      uint32_t triangle_index, Factory&& factory,
                      const Key*& last_key, size_t& last_index) {
  size_t idx;
  if (last_key && std::equal_to<Key>{}(*last_key, key)) {
    idx = last_index;
  } else {
    auto [it, inserted] = item_map.try_emplace(key, items.size());
    if (inserted) {
      items.push_back(factory());
    }
    idx = it->second;
    last_key = &it->first;
    last_index = idx;
  }

  auto& element = items[idx];
  if (!element.ranges.empty() &&
      element.ranges.back().start + element.ranges.back().count ==
          triangle_index) {
    element.ranges.back().count++;
  } else {
    element.ranges.push_back({triangle_index, 1});
  }
}

}  // namespace

void GeoViewerWidget::SetMapAndMesh(
    std::shared_ptr<odr::OpenDriveMap> map, odr::RoadNetworkMesh network_mesh,
    const JunctionClusterResult* junction_grouping,
    std::shared_ptr<odr::RoutingGraph> routing_graph) {
  StopSimulation();
  const bool had_user_points = !user_points_.empty();

  map_ = std::move(map);
  if (!map_) {
    ResetSceneData();
    if (had_user_points) {
      ClearUserPoints();
    }
    update();
    return;
  }

  network_mesh_ =
      std::make_shared<odr::RoadNetworkMesh>(std::move(network_mesh));
  junction_cluster_result_ = junction_grouping
                                 ? *junction_grouping
                                 : JunctionClusterUtil::Analyze(*map_);

  ClearMeshAuxiliaryData(network_mesh_->lanes_mesh);
  ClearMeshAuxiliaryData(network_mesh_->roadmarks_mesh);
  ClearMeshAuxiliaryData(network_mesh_->road_objects_mesh);
  ClearMeshAuxiliaryData(network_mesh_->road_signals_mesh);

  ResetSceneData();
  PopulateJunctionLookupMaps();
  PopulateLaneKeyIntervals();
  if (routing_graph) {
    routing_graph_ = std::move(routing_graph);
  } else {
    routing_graph_ =
        std::make_shared<odr::RoutingGraph>(map_->get_routing_graph());
  }

  RebuildSceneCaches();
  TransformSceneMeshes();
  BuildJunctionPlanes();

  auto vertices = BuildSceneVertexBufferData();

  bool was_current = (QOpenGLContext::currentContext() == context());
  if (!was_current) makeCurrent();

  UploadVertexBufferData(vertices);
  ApplyDefaultLayerStyles();

  if (!was_current) doneCurrent();

  UpdateMeshIndices();
  FinalizeSceneUpdate();

  if (had_user_points) {
    ClearUserPoints();
  }
  update();
}

void GeoViewerWidget::ResetSceneData() {
  mesh_updated_ = false;
  if (!map_) {
    network_mesh_ = std::make_shared<odr::RoadNetworkMesh>();
    junction_cluster_result_ = JunctionClusterResult();
  }

  if (gl_renderer_) {
    makeCurrent();
    gl_renderer_->Clear();
    doneCurrent();
  }

  if (measure_ctrl_) {
    measure_ctrl_->ClearPoints();
  }

  hidden_elements_.clear();
  lane_element_items_.clear();
  roadmark_element_items_.clear();
  object_element_items_.clear();
  signal_element_items_.clear();
  junction_element_items_.clear();
  outline_element_items_.clear();
  junction_mesh_ = std::make_shared<odr::Mesh3D>();
  junction_group_centers_.clear();
  junction_group_index_by_id_.clear();
  junction_member_index_by_id_.clear();
  junction_vertex_group_indices_.clear();
  lane_element_index_by_key_.clear();
  road_ref_line_vert_ranges_.clear();
  lane_outline_indices_.clear();
  facility_element_items_.clear();
  facility_mesh_ = std::make_shared<odr::Mesh3D>();
  routing_graph_.reset();
  spatial_index_ready_ = false;
  spatial_index_data_ = SpatialIndexData();

  selected_junction_group_id_.clear();
  selected_junction_id_.clear();
  last_wheel_pick_pos_ = QPoint(-1, -1);

  emit SceneReset();
}

void GeoViewerWidget::ClearMeshAuxiliaryData(odr::Mesh3D& mesh) {
  mesh.normals.clear();
  mesh.normals.shrink_to_fit();
  mesh.st_coordinates.clear();
  mesh.st_coordinates.shrink_to_fit();
}

void GeoViewerWidget::PopulateJunctionLookupMaps() {
  junction_group_index_by_id_.reserve(junction_cluster_result_.groups.size());
  for (std::size_t i = 0; i < junction_cluster_result_.groups.size(); ++i) {
    junction_group_index_by_id_.emplace(
        junction_cluster_result_.groups[i].group_id, i);
  }

  junction_member_index_by_id_.reserve(
      junction_cluster_result_.junctions.size());
  for (std::size_t i = 0; i < junction_cluster_result_.junctions.size(); ++i) {
    junction_member_index_by_id_.emplace(
        junction_cluster_result_.junctions[i].junction_id, i);
  }
}

void GeoViewerWidget::PopulateLaneKeyIntervals() {
  auto& network_mesh = *network_mesh_;
  // Pre-initialize libOpenDRIVE caches sequentially to avoid races in
  // background tasks
  if (!network_mesh.lanes_mesh.indices.empty()) {
    static_cast<void>(network_mesh.lanes_mesh.get_road_id(
        network_mesh.lanes_mesh.indices[0]));
  }

  // Pre-initialize other mesh caches as well
  if (!network_mesh.roadmarks_mesh.indices.empty()) {
    static_cast<void>(network_mesh.roadmarks_mesh.get_road_id(
        network_mesh.roadmarks_mesh.indices[0]));
  }
  if (!network_mesh.road_objects_mesh.indices.empty()) {
    static_cast<void>(network_mesh.road_objects_mesh.get_road_id(
        network_mesh.road_objects_mesh.indices[0]));
  }

  // Redundant cache population removed.
}

void GeoViewerWidget::RebuildSceneCaches() {
  auto& pool = geoviewer::utility::ThreadPool::Instance();
  auto map = map_;
  auto network_mesh = network_mesh_;

  // Precompute facility keys on the main thread
  std::unordered_set<std::string> facility_keys;
  if (map) {
    for (const auto& [road_id, road] : map->id_to_road) {
      for (const auto& [obj_id, obj] : road.id_to_object) {
        auto to_lower = [](std::string s) {
          std::transform(s.begin(), s.end(), s.begin(),
                         [](unsigned char c) { return std::tolower(c); });
          return s;
        };
        if (to_lower(obj.type) == "facility" ||
            to_lower(obj.name) == "facility") {
          facility_keys.insert(road_id + "/" + obj_id);
        }
      }
    }
  }

  auto f_lane = pool.Enqueue(
      [this, network_mesh]() { return BuildLaneElementCache(network_mesh); });
  auto f_roadmark = pool.Enqueue([this, network_mesh]() {
    return BuildRoadmarkElementCache(network_mesh);
  });
  auto f_object = pool.Enqueue([this, network_mesh, facility_keys]() {
    return BuildObjectElementCache(network_mesh, facility_keys);
  });
  auto f_signal = pool.Enqueue([this, map, network_mesh]() {
    return BuildSignalElementCache(map, network_mesh);
  });
  auto f_outline = pool.Enqueue([this, network_mesh]() {
    return BuildOutlineElementCache(network_mesh);
  });
  auto f_facility = pool.Enqueue([this, map, facility_keys]() {
    return BuildFacilityElementCache(map, facility_keys);
  });

  // Collect results and update member variables sequentially
  auto lane_res = f_lane.get();
  lane_element_items_ = std::move(lane_res.items);
  lane_element_index_by_key_ = std::move(lane_res.index_by_key);

  roadmark_element_items_ = f_roadmark.get();
  object_element_items_ = f_object.get();

  auto signal_res = f_signal.get();
  signal_element_items_ = std::move(signal_res.items);
  signal_id_to_road_id_ = std::move(signal_res.signal_id_to_road_id);

  auto outline_res = f_outline.get();
  outline_element_items_ = std::move(outline_res.items);
  lane_outline_indices_ = std::move(outline_res.indices);

  auto facility_res = f_facility.get();
  facility_mesh_ = std::move(facility_res.mesh);
  facility_element_items_ = std::move(facility_res.items);
}

GeoViewerWidget::LaneCacheResult GeoViewerWidget::BuildLaneElementCache(
    const std::shared_ptr<odr::RoadNetworkMesh> network_mesh) const {
  const auto& mesh = network_mesh->lanes_mesh;
  LaneCacheResult result;
  std::map<odr::LaneKey, size_t> item_map;

  FastMapLookup<std::string> road_lookup;
  road_lookup.Init(mesh.road_start_indices);
  FastMapLookup<double> lanesec_lookup;
  lanesec_lookup.Init(mesh.lanesec_start_indices);
  FastMapLookup<int> lane_lookup;
  lane_lookup.Init(mesh.lane_start_indices);

  const odr::LaneKey* last_key = nullptr;
  size_t last_index = 0;

  for (size_t i = 0; i < mesh.indices.size(); i += 3) {
    const uint32_t vertex_index = mesh.indices[i];
    const std::string road_id = road_lookup.Lookup(vertex_index);
    const double s0 = lanesec_lookup.Lookup(vertex_index);
    const int lane_id = lane_lookup.Lookup(vertex_index);
    auto key = odr::LaneKey(road_id, s0, lane_id);

    AddTriangleRange(
        result.items, item_map, key, static_cast<uint32_t>(i / 3),
        [&]() {
          SceneCachedElement element;
          element.road_key = "R:" + road_id;
          element.group_key = "G:" + road_id + ":section";
          element.element_key = "E:" + road_id +
                                ":lane:" + FormatSectionValue(s0) + ":" +
                                std::to_string(lane_id);
          return element;
        },
        last_key, last_index);
  }

  for (auto const& [key, val] : item_map) {
    result.index_by_key[key] = val;
  }
  return result;
}

std::vector<SceneCachedElement> GeoViewerWidget::BuildRoadmarkElementCache(
    const std::shared_ptr<odr::RoadNetworkMesh> network_mesh) const {
  const auto& mesh = network_mesh->roadmarks_mesh;
  std::vector<SceneCachedElement> items;
  std::map<std::pair<std::string, std::string>, size_t> item_map;

  FastMapLookup<std::string> road_lookup;
  road_lookup.Init(mesh.road_start_indices);
  FastMapLookup<std::string> roadmark_type_lookup;
  roadmark_type_lookup.Init(mesh.roadmark_type_start_indices);

  const std::pair<std::string, std::string>* last_key = nullptr;
  size_t last_index = 0;

  for (size_t i = 0; i < mesh.indices.size(); i += 3) {
    const uint32_t vertex_index = mesh.indices[i];
    const std::string road_id = road_lookup.Lookup(vertex_index);
    const std::string type = roadmark_type_lookup.Lookup(vertex_index);
    auto key = std::make_pair(road_id, type);

    AddTriangleRange(
        items, item_map, key, static_cast<uint32_t>(i / 3),
        [&]() {
          SceneCachedElement element;
          element.road_key = "R:" + road_id;
          element.group_key = "G:" + road_id + ":section";
          element.element_key = "E:" + road_id + ":roadmark:" + type;
          return element;
        },
        last_key, last_index);
  }

  return items;
}

std::vector<SceneCachedElement> GeoViewerWidget::BuildObjectElementCache(
    const std::shared_ptr<odr::RoadNetworkMesh> network_mesh,
    const std::unordered_set<std::string>& facility_keys) const {
  const auto& mesh = network_mesh->road_objects_mesh;
  std::vector<SceneCachedElement> items;
  std::map<std::pair<std::string, std::string>, size_t> item_map;

  FastMapLookup<std::string> road_lookup;
  road_lookup.Init(mesh.road_start_indices);
  FastMapLookup<std::string> object_lookup;
  object_lookup.Init(mesh.road_object_start_indices);

  const std::pair<std::string, std::string>* last_key = nullptr;
  size_t last_index = 0;

  std::string last_checked_road;
  std::string last_checked_obj;
  bool last_is_facility = false;

  for (size_t i = 0; i < mesh.indices.size(); i += 3) {
    const uint32_t vertex_index = mesh.indices[i];
    const std::string road_id = road_lookup.Lookup(vertex_index);
    const std::string object_id = object_lookup.Lookup(vertex_index);

    bool is_facility = false;
    if (road_id == last_checked_road && object_id == last_checked_obj) {
      is_facility = last_is_facility;
    } else {
      is_facility = facility_keys.count(road_id + "/" + object_id) > 0;
      last_checked_road = road_id;
      last_checked_obj = object_id;
      last_is_facility = is_facility;
    }

    if (is_facility) continue;
    auto key = std::make_pair(road_id, object_id);

    AddTriangleRange(
        items, item_map, key, static_cast<uint32_t>(i / 3),
        [&]() {
          SceneCachedElement element;
          element.road_key = "R:" + road_id;
          element.group_key = "G:" + road_id + ":objects";
          element.element_key = "E:" + road_id + ":objects:" + object_id;
          return element;
        },
        last_key, last_index);
  }

  return items;
}

bool GeoViewerWidget::IsFacility(const std::string& road_id,
                                 const std::string& object_id) const {
  if (!map_ || !map_->id_to_road.count(road_id)) return false;
  const auto& road = map_->id_to_road.at(road_id);
  if (!road.id_to_object.count(object_id)) return false;
  const auto& obj = road.id_to_object.at(object_id);

  auto to_lower = [](std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
  };
  return to_lower(obj.type) == "facility" || to_lower(obj.name) == "facility";
}

static void AddRibbonMesh(odr::Mesh3D& mesh, const QVector3D& p1,
                          const QVector3D& p2, float width) {
  QVector3D dir = p2 - p1;
  float len = dir.length();
  if (len < 1e-4f) return;
  QVector3D unit_dir = dir / len;
  QVector3D up(0, 1, 0);  // Y is up in renderer coords
  QVector3D side = QVector3D::crossProduct(unit_dir, up).normalized();
  if (side.lengthSquared() < 1e-4f) side = QVector3D(0, 0, 1);

  float half_w = width * 0.5f;
  QVector3D v0 = p1 - side * half_w;
  QVector3D v1 = p1 + side * half_w;
  QVector3D v2 = p2 + side * half_w;
  QVector3D v3 = p2 - side * half_w;

  uint32_t start_idx = static_cast<uint32_t>(mesh.vertices.size());
  auto push_v = [&](const QVector3D& v) {
    mesh.vertices.push_back({static_cast<double>(v.x()),
                             static_cast<double>(v.y()),
                             static_cast<double>(v.z())});
  };
  push_v(v0);
  push_v(v1);
  push_v(v2);
  push_v(v3);

  mesh.indices.push_back(start_idx + 0);
  mesh.indices.push_back(start_idx + 1);
  mesh.indices.push_back(start_idx + 2);
  mesh.indices.push_back(start_idx + 0);
  mesh.indices.push_back(start_idx + 2);
  mesh.indices.push_back(start_idx + 3);
}

GeoViewerWidget::FacilityCacheResult GeoViewerWidget::BuildFacilityElementCache(
    const std::shared_ptr<odr::OpenDriveMap> map,
    const std::unordered_set<std::string>& facility_keys) const {
  FacilityCacheResult result;
  result.mesh = std::make_shared<odr::Mesh3D>();
  if (!map) return result;

  for (const auto& [road_id, road] : map->id_to_road) {
    for (const auto& [obj_id, obj] : road.id_to_object) {
      if (!facility_keys.count(road_id + "/" + obj_id)) continue;

      SceneCachedElement element;
      element.road_key = "R:" + road_id;
      element.group_key = "G:" + road_id + ":objects";
      element.element_key = "E:" + road_id + ":objects:" + obj_id;

      uint32_t tri_start =
          static_cast<uint32_t>(result.mesh->indices.size() / 3);
      odr::Vec3D e_s, e_t, e_h;
      const auto p0 = road.get_xyz(obj.s0, obj.t0, obj.z0, &e_s, &e_t, &e_h);
      const odr::Mat3D base_mat{{{e_s[0], e_t[0], e_h[0]},
                                 {e_s[1], e_t[1], e_h[1]},
                                 {e_s[2], e_t[2], e_h[2]}}};
      const odr::Mat3D rot_mat =
          odr::EulerAnglesToMatrix<double>(obj.roll, obj.pitch, obj.hdg);
      for (const auto& outline : obj.outlines) {
        std::vector<odr::Vec3D> points;
        for (const auto& corner : outline.outline) {
          odr::Vec3D pt_world;
          if (corner.type == odr::RoadObjectCorner::Type_Road) {
            pt_world = road.get_xyz(corner.pt[0], corner.pt[1],
                                    corner.pt[2] + corner.height);
          } else {
            odr::Vec3D pt_local = {corner.pt[0], corner.pt[1], corner.pt[2]};
            if (corner.type == odr::RoadObjectCorner::Type_Local_AbsZ) {
              pt_local[2] -= p0[2];  // Convert to relative coordinates
            }
            pt_local = odr::add(pt_local, odr::Vec3D{0.0, 0.0, corner.height});
            pt_world = odr::add(
                odr::MatVecMultiplication(
                    base_mat, odr::MatVecMultiplication(rot_mat, pt_local)),
                p0);
            // points.push_back(LocalToRendererPoint(pt_world));
          }
          points.push_back(pt_world);
        }

        for (size_t i = 0; i + 1 < points.size(); ++i) {
          const QVector3D p1(points[i][0], points[i][1], points[i][2]);
          const QVector3D p2(points[i + 1][0], points[i + 1][1],
                             points[i + 1][2]);
          AddRibbonMesh(*result.mesh, p1, p2, 0.8f);
        }
      }

      uint32_t tri_count =
          static_cast<uint32_t>(result.mesh->indices.size() / 3) - tri_start;
      if (tri_count > 0) {
        element.ranges.push_back({tri_start, tri_count});
        result.items.push_back(element);
      }
    }
  }

  return result;
}

GeoViewerWidget::SignalCacheResult GeoViewerWidget::BuildSignalElementCache(
    const std::shared_ptr<odr::OpenDriveMap> map,
    const std::shared_ptr<odr::RoadNetworkMesh> network_mesh) const {
  SignalCacheResult result;
  if (!map || !network_mesh) return result;

  // Build signal_id_to_road_id map
  for (const auto& [road_id, road] : map->id_to_road) {
    for (const auto& [signal_id, signal] : road.id_to_signal) {
      result.signal_id_to_road_id[signal_id] = road_id;
    }
  }

  const auto& mesh = network_mesh->road_signals_mesh;
  std::map<std::pair<std::string, std::string>, size_t> item_map;

  FastMapLookup<std::string> signal_lookup;
  signal_lookup.Init(mesh.road_signal_start_indices);

  const std::pair<std::string, std::string>* last_key = nullptr;
  size_t last_index = 0;

  for (size_t i = 0; i < mesh.indices.size(); i += 3) {
    const uint32_t vertex_index = mesh.indices[i];
    const std::string signal_id = signal_lookup.Lookup(vertex_index);
    std::string road_id = "";
    auto itr = result.signal_id_to_road_id.find(signal_id);
    if (itr != result.signal_id_to_road_id.end()) {
      road_id = itr->second;
    }
    auto key = std::make_pair(road_id, signal_id);

    AddTriangleRange(
        result.items, item_map, key, static_cast<uint32_t>(i / 3),
        [&]() {
          SceneCachedElement element;
          bool is_light = false;
          if (map->id_to_road.count(road_id)) {
            const auto& road = map->id_to_road.at(road_id);
            if (road.id_to_signal.count(signal_id)) {
              is_light =
                  (road.id_to_signal.at(signal_id).name == "TrafficLight");
            }
          }
          const std::string group = is_light ? "light" : "sign";
          element.road_key = "R:" + road_id;
          element.group_key = "G:" + road_id + ":" + group;
          element.element_key = "E:" + road_id + ":" + group + ":" + signal_id;
          return element;
        },
        last_key, last_index);
  }

  return result;
}

GeoViewerWidget::OutlineCacheResult GeoViewerWidget::BuildOutlineElementCache(
    const std::shared_ptr<odr::RoadNetworkMesh> network_mesh) const {
  const auto& mesh = network_mesh->lanes_mesh;
  OutlineCacheResult result;
  const std::vector<size_t> outline_indices = mesh.get_lane_outline_indices();
  result.indices.reserve(outline_indices.size());
  for (const auto& idx : outline_indices) {
    result.indices.push_back(static_cast<uint32_t>(idx));
  }

  std::map<std::tuple<std::string, double, int>, size_t> item_map;

  FastMapLookup<std::string> road_lookup;
  road_lookup.Init(mesh.road_start_indices);
  FastMapLookup<double> lanesec_lookup;
  lanesec_lookup.Init(mesh.lanesec_start_indices);
  FastMapLookup<int> lane_lookup;
  lane_lookup.Init(mesh.lane_start_indices);

  const std::tuple<std::string, double, int>* last_key = nullptr;
  size_t last_index = 0;

  for (size_t i = 0; i < result.indices.size(); i += 2) {
    const size_t vertex_index = result.indices[i];
    const std::string road_id = road_lookup.Lookup(vertex_index);
    const double s0 = lanesec_lookup.Lookup(vertex_index);
    const int lane_id = lane_lookup.Lookup(vertex_index);
    auto key = std::make_tuple(road_id, s0, lane_id);

    size_t idx;
    if (last_key && *last_key == key) {
      idx = last_index;
    } else {
      auto [it, inserted] = item_map.try_emplace(key, result.items.size());
      if (inserted) {
        SceneOutlineElement element;
        const std::string s0_string = FormatSectionValue(s0);
        element.road_key = "R:" + road_id;
        element.group_key = "G:" + road_id + ":section";
        element.element_key = "E:" + road_id + ":lane:" + s0_string + ":" +
                              std::to_string(lane_id);
        element.is_dashed = false;

        if (map_->id_to_road.count(road_id)) {
          auto& road = map_->id_to_road.at(road_id);
          if (road.s_to_lanesection.count(s0)) {
            auto& section = road.s_to_lanesection.at(s0);
            if (section.id_to_lane.count(lane_id)) {
              auto& lane = section.id_to_lane.at(lane_id);
              for (const auto& group : lane.roadmark_groups) {
                if (group.type == "broken") {
                  element.is_dashed = true;
                  break;
                }
              }
            }
          }
        }

        result.items.push_back(element);
      }
      idx = it->second;
      last_key = &it->first;
      last_index = idx;
    }

    auto& element = result.items[idx];
    const uint32_t line_index = static_cast<uint32_t>(i / 2);
    if (!element.ranges.empty() &&
        element.ranges.back().start + element.ranges.back().count ==
            line_index) {
      element.ranges.back().count++;
    } else {
      element.ranges.push_back({line_index, 1});
    }
  }

  return result;
}

void GeoViewerWidget::TransformSceneMeshes() {
  if (!network_mesh_) return;
  auto& network_mesh = *network_mesh_;

  const bool right_hand_traffic = right_hand_traffic_;
  auto transform = [right_hand_traffic](odr::Vec3D& v) {
    const float rx = static_cast<float>(v[0]);
    const float ry = static_cast<float>(v[2]);
    float rz = static_cast<float>(v[1]);
    if (right_hand_traffic) {
      rz = -rz;
    }
    v[0] = rx;
    v[1] = ry;
    v[2] = rz;
  };

  struct Bounds {
    float min_x = std::numeric_limits<float>::max();
    float min_y = std::numeric_limits<float>::max();
    float min_z = std::numeric_limits<float>::max();
    float max_x = std::numeric_limits<float>::lowest();
    float max_y = std::numeric_limits<float>::lowest();
    float max_z = std::numeric_limits<float>::lowest();
  };
  auto lanes_bounds = std::make_shared<Bounds>();

  std::vector<std::future<void>> tasks;
  auto& pool = geoviewer::utility::ThreadPool::Instance();
  tasks.push_back(pool.Enqueue([&network_mesh, transform, lanes_bounds]() {
    for (auto& v : network_mesh.lanes_mesh.vertices) {
      transform(v);
      float x = static_cast<float>(v[0]);
      float y = static_cast<float>(v[1]);
      float z = static_cast<float>(v[2]);
      if (x < lanes_bounds->min_x) lanes_bounds->min_x = x;
      if (y < lanes_bounds->min_y) lanes_bounds->min_y = y;
      if (z < lanes_bounds->min_z) lanes_bounds->min_z = z;
      if (x > lanes_bounds->max_x) lanes_bounds->max_x = x;
      if (y > lanes_bounds->max_y) lanes_bounds->max_y = y;
      if (z > lanes_bounds->max_z) lanes_bounds->max_z = z;
    }
  }));
  tasks.push_back(pool.Enqueue([&network_mesh, transform]() {
    for (auto& v : network_mesh.roadmarks_mesh.vertices) transform(v);
  }));
  tasks.push_back(pool.Enqueue([&network_mesh, transform]() {
    for (auto& v : network_mesh.road_objects_mesh.vertices) transform(v);
  }));
  tasks.push_back(pool.Enqueue([this, transform]() {
    if (facility_mesh_) {
      for (auto& v : facility_mesh_->vertices) transform(v);
    }
  }));
  tasks.push_back(pool.Enqueue([&network_mesh, transform]() {
    for (auto& v : network_mesh.road_signals_mesh.vertices) transform(v);
  }));

  for (auto& task : tasks) {
    task.get();
  }

  scene_min_bound_ =
      QVector3D(lanes_bounds->min_x, lanes_bounds->min_y, lanes_bounds->min_z);
  scene_max_bound_ =
      QVector3D(lanes_bounds->max_x, lanes_bounds->max_y, lanes_bounds->max_z);
}

std::vector<float> GeoViewerWidget::BuildSceneVertexBufferData() {
  if (!gl_renderer_ || !network_mesh_ || !junction_mesh_) return {};
  auto& network_mesh = *network_mesh_;
  auto& junction_mesh = *junction_mesh_;

  size_t lanes_size = network_mesh.lanes_mesh.vertices.size();
  size_t roadmarks_size = network_mesh.roadmarks_mesh.vertices.size();
  size_t objects_size = network_mesh.road_objects_mesh.vertices.size();
  size_t facilities_size = facility_mesh_ ? facility_mesh_->vertices.size() : 0;
  size_t junctions_size = junction_mesh.vertices.size();
  size_t signals_size = network_mesh.road_signals_mesh.vertices.size();

  size_t static_vertices = lanes_size + roadmarks_size + objects_size +
                           facilities_size + junctions_size;
  std::vector<float> vertices(static_vertices * 3);

  auto convert_mesh = [&vertices](const odr::Mesh3D& mesh,
                                  size_t start_vertex_idx) {
    float* dest = vertices.data() + start_vertex_idx * 3;
    for (const auto& vertex : mesh.vertices) {
      *dest++ = static_cast<float>(vertex[0]);
      *dest++ = static_cast<float>(vertex[1]);
      *dest++ = static_cast<float>(vertex[2]);
    }
  };

  auto& pool = geoviewer::utility::ThreadPool::Instance();
  std::vector<std::future<void>> tasks;
  tasks.reserve(5);

  size_t current_offset = 0;

  // 1. Lanes
  gl_renderer_->SetLayerVertexOffset(LayerType::kLanes, current_offset);
  gl_renderer_->SetLayerVertexOffset(LayerType::kLaneLines, current_offset);
  gl_renderer_->SetLayerVertexOffset(LayerType::kLaneLinesDashed,
                                     current_offset);
  tasks.push_back(pool.Enqueue([&network_mesh, convert_mesh, current_offset]() {
    convert_mesh(network_mesh.lanes_mesh, current_offset);
  }));
  current_offset += lanes_size;

  // 2. Roadmarks
  gl_renderer_->SetLayerVertexOffset(LayerType::kRoadmarks, current_offset);
  tasks.push_back(pool.Enqueue([&network_mesh, convert_mesh, current_offset]() {
    convert_mesh(network_mesh.roadmarks_mesh, current_offset);
  }));
  current_offset += roadmarks_size;

  // 3. Objects
  gl_renderer_->SetLayerVertexOffset(LayerType::kObjects, current_offset);
  tasks.push_back(pool.Enqueue([&network_mesh, convert_mesh, current_offset]() {
    convert_mesh(network_mesh.road_objects_mesh, current_offset);
  }));
  current_offset += objects_size;

  // 4. Facilities
  if (facility_mesh_) {
    gl_renderer_->SetLayerVertexOffset(LayerType::kFacilities, current_offset);
    auto* fac_mesh_ptr = facility_mesh_.get();
    tasks.push_back(
        pool.Enqueue([fac_mesh_ptr, convert_mesh, current_offset]() {
          convert_mesh(*fac_mesh_ptr, current_offset);
        }));
    current_offset += facilities_size;
  } else {
    gl_renderer_->SetLayerVertexOffset(LayerType::kFacilities, 0);
  }

  // 5. Junctions
  gl_renderer_->SetLayerVertexOffset(LayerType::kJunctions, current_offset);
  tasks.push_back(
      pool.Enqueue([&junction_mesh, convert_mesh, current_offset]() {
        convert_mesh(junction_mesh, current_offset);
      }));
  current_offset += junctions_size;

  // Wait for all static mesh conversions to complete
  for (auto& task : tasks) {
    task.get();
  }

  // Reference Lines (Special case: generated on the fly)
  gl_renderer_->SetLayerVertexOffset(LayerType::kReferenceLines,
                                     vertices.size() / 3);
  GenerateRefLinePoints(map_, vertices, road_ref_line_vert_ranges_);

  // Signals (Lights and Signs share the same mesh)
  gl_renderer_->SetLayerVertexOffset(LayerType::kSignalLights,
                                     vertices.size() / 3);
  gl_renderer_->SetLayerVertexOffset(LayerType::kSignalSigns,
                                     vertices.size() / 3);

  size_t signals_start_idx = vertices.size();
  vertices.resize(signals_start_idx + signals_size * 3);
  float* dest = vertices.data() + signals_start_idx;
  for (const auto& vertex : network_mesh.road_signals_mesh.vertices) {
    *dest++ = static_cast<float>(vertex[0]);
    *dest++ = static_cast<float>(vertex[1]);
    *dest++ = static_cast<float>(vertex[2]);
  }

  return vertices;
}

void GeoViewerWidget::UploadVertexBufferData(
    const std::vector<float>& vertices) {
  if (vertices.empty() || !gl_renderer_) return;
  gl_renderer_->UploadSceneVertices(vertices);
}

void GeoViewerWidget::ApplyDefaultLayerStyles() {
  if (!gl_renderer_) return;

  gl_renderer_->SetLayerColor(LayerType::kLanes,
                              QVector3D(0.75f, 0.75f, 0.75f));
  gl_renderer_->SetLayerAlpha(LayerType::kLanes, 1.0f);

  gl_renderer_->SetLayerColor(LayerType::kLaneLines,
                              QVector3D(1.0f, 1.0f, 0.0f));
  gl_renderer_->SetLayerAlpha(LayerType::kLaneLines, 1.0f);
  gl_renderer_->SetLayerDrawMode(LayerType::kLaneLines, GL_LINES);
  gl_renderer_->SetLayerPolygonOffset(LayerType::kLaneLines, -1.0f, -1.0f);

  gl_renderer_->SetLayerColor(LayerType::kLaneLinesDashed,
                              QVector3D(1.0f, 1.0f, 0.0f));
  gl_renderer_->SetLayerAlpha(LayerType::kLaneLinesDashed, 0.8f);
  gl_renderer_->SetLayerDrawMode(LayerType::kLaneLinesDashed, GL_LINES);
  gl_renderer_->SetLayerPolygonOffset(LayerType::kLaneLinesDashed, -1.0f,
                                      -1.0f);

  gl_renderer_->SetLayerColor(LayerType::kRoadmarks,
                              QVector3D(1.0f, 1.0f, 1.0f));
  gl_renderer_->SetLayerAlpha(LayerType::kRoadmarks, 1.0f);
  gl_renderer_->SetLayerPolygonOffset(LayerType::kRoadmarks, -1.0f, -1.0f);

  gl_renderer_->SetLayerColor(LayerType::kObjects, QVector3D(0.8f, 0.5f, 0.3f));
  gl_renderer_->SetLayerAlpha(LayerType::kObjects, 1.0f);
  gl_renderer_->SetLayerPolygonOffset(LayerType::kObjects, -1.0f, -1.0f);

  gl_renderer_->SetLayerColor(LayerType::kFacilities,
                              QVector3D(0.0f, 1.0f, 1.0f));
  gl_renderer_->SetLayerAlpha(LayerType::kFacilities, 1.0f);
  gl_renderer_->SetLayerPolygonOffset(LayerType::kFacilities, -1.0f, -1.0f);

  gl_renderer_->SetLayerColor(LayerType::kReferenceLines,
                              QVector3D(1.0f, 0.5f, 0.0f));
  gl_renderer_->SetLayerAlpha(LayerType::kReferenceLines, 1.0f);
  gl_renderer_->SetLayerDrawMode(LayerType::kReferenceLines, GL_LINES);
  gl_renderer_->SetLayerPolygonOffset(LayerType::kReferenceLines, -2.0f, -2.0f);

  gl_renderer_->SetLayerColor(LayerType::kSignalLights,
                              QVector3D(0.2f, 0.8f, 0.8f));
  gl_renderer_->SetLayerAlpha(LayerType::kSignalLights, 1.0f);

  gl_renderer_->SetLayerColor(LayerType::kSignalSigns,
                              QVector3D(0.8f, 0.2f, 0.2f));
  gl_renderer_->SetLayerAlpha(LayerType::kSignalSigns, 1.0f);

  gl_renderer_->SetLayerColor(LayerType::kJunctions,
                              QVector3D(1.0f, 0.75f, 0.2f));
  gl_renderer_->SetLayerAlpha(LayerType::kJunctions, 1.0f);
  gl_renderer_->SetLayerPolygonOffset(LayerType::kJunctions, -2.5f, -2.5f);
}

void GeoViewerWidget::FinalizeSceneUpdate() {
  mesh_updated_ = true;
  CalculateMeshCenter();
  spatial_index_ready_ = false;
  spatial_index_data_ = SpatialIndexData();
  StartSpatialIndexBuild();
}

std::string GeoViewerWidget::GetRoadIdBySignalId(
    const std::string& signal_id) const noexcept {
  auto itr = signal_id_to_road_id_.find(signal_id);
  if (itr == signal_id_to_road_id_.end()) {
    return "";
  }
  return itr->second;
}
