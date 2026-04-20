#include "src/ui/widgets/geo_viewer.h"

#include <future>
#include <tuple>
#include <vector>
#include "src/core/thread_pool.h"
#include "src/core/viewer_text_util.h"

namespace {

template <typename ItemContainer, typename ItemMap, typename Key,
          typename Factory>
void AddTriangleRange(ItemContainer& items, ItemMap& item_map, Key&& key,
                      uint32_t triangle_index, Factory&& factory) {
  if (item_map.find(key) == item_map.end()) {
    item_map[key] = items.size();
    items.push_back(factory());
  }

  auto& element = items[item_map[key]];
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
    const JunctionClusterResult* junction_grouping) {
  const bool had_user_points = !user_points_.empty();

  makeCurrent();

  map_ = std::move(map);
  if (!map_) {
    ResetSceneData();
    doneCurrent();
    if (had_user_points) {
      ClearUserPoints();
    }
    update();
    return;
  }

  network_mesh_ = std::move(network_mesh);
  junction_cluster_result_ = junction_grouping
                                 ? *junction_grouping
                                 : JunctionClusterUtil::Analyze(*map_);

  ClearMeshAuxiliaryData(network_mesh_.lanes_mesh);
  ClearMeshAuxiliaryData(network_mesh_.roadmarks_mesh);
  ClearMeshAuxiliaryData(network_mesh_.road_objects_mesh);
  ClearMeshAuxiliaryData(network_mesh_.road_signals_mesh);

  ResetSceneData();
  PopulateJunctionLookupMaps();
  PopulateLaneKeyIntervals();
  routing_graph_ =
      std::make_unique<odr::RoutingGraph>(map_->get_routing_graph());

  RebuildSceneCaches();
  TransformSceneMeshes();
  BuildJunctionPlanes();
  UploadVertexBufferData(BuildSceneVertexBufferData());
  UpdateMeshIndices();
  ApplyDefaultLayerStyles();
  FinalizeSceneUpdate();
  doneCurrent();
  if (had_user_points) {
    ClearUserPoints();
  }
  update();
}

void GeoViewerWidget::ResetSceneData() {
  mesh_updated_ = false;
  if (!map_) {
    network_mesh_ = odr::RoadNetworkMesh();
    junction_cluster_result_ = JunctionClusterResult();
  }
  lane_element_items_.clear();
  roadmark_element_items_.clear();
  object_element_items_.clear();
  signal_element_items_.clear();
  junction_element_items_.clear();
  outline_element_items_.clear();
  junction_mesh_ = odr::Mesh3D();
  junction_group_centers_.clear();
  junction_group_index_by_id_.clear();
  junction_member_index_by_id_.clear();
  junction_vertex_group_indices_.clear();
  lane_element_index_by_key_.clear();
  lane_key_to_interval_.clear();
  road_ref_line_vert_ranges_.clear();
  lane_outline_indices_.clear();
  routing_graph_.reset();
  spatial_grid_ready_ = false;
  grid_boxes_.clear();
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
  lane_key_to_interval_.reserve(
      network_mesh_.lanes_mesh.lane_start_indices.size());
  for (const auto& [start_idx, lane_id] :
       network_mesh_.lanes_mesh.lane_start_indices) {
    const std::string road_id = network_mesh_.lanes_mesh.get_road_id(start_idx);
    const double s0 = network_mesh_.lanes_mesh.get_lanesec_s0(start_idx);
    lane_key_to_interval_[odr::LaneKey(road_id, s0, lane_id)] =
        network_mesh_.lanes_mesh.get_idx_interval_lane(start_idx);
  }
}

void GeoViewerWidget::RebuildSceneCaches() {
  std::vector<std::future<void>> tasks;
  auto& pool = geoviewer::utility::ThreadPool::Instance();
  tasks.push_back(pool.Enqueue([this]() { BuildLaneElementCache(); }));
  tasks.push_back(pool.Enqueue([this]() { BuildRoadmarkElementCache(); }));
  tasks.push_back(pool.Enqueue([this]() { BuildObjectElementCache(); }));
  tasks.push_back(pool.Enqueue([this]() { BuildSignalElementCache(); }));
  tasks.push_back(pool.Enqueue([this]() { BuildOutlineElementCache(); }));

  for (auto& task : tasks) {
    task.get();
  }
}

void GeoViewerWidget::BuildLaneElementCache() {
  std::vector<SceneCachedElement> items;
  std::map<std::tuple<std::string, double, int>, size_t> item_map;

  for (size_t i = 0; i < network_mesh_.lanes_mesh.indices.size(); i += 3) {
    const uint32_t vertex_index = network_mesh_.lanes_mesh.indices[i];
    const std::string road_id =
        network_mesh_.lanes_mesh.get_road_id(vertex_index);
    const double s0 = network_mesh_.lanes_mesh.get_lanesec_s0(vertex_index);
    const int lane_id = network_mesh_.lanes_mesh.get_lane_id(vertex_index);
    auto key = std::make_tuple(road_id, s0, lane_id);

    AddTriangleRange(items, item_map, key, static_cast<uint32_t>(i / 3), [&]() {
      SceneCachedElement element;
      const std::string s0_string = FormatSectionValue(s0);
      element.road_key = "R:" + road_id;
      element.group_key = "G:" + road_id + ":section";
      element.element_key =
          "E:" + road_id + ":lane:" + s0_string + ":" + std::to_string(lane_id);
      return element;
    });
  }

  lane_element_items_ = std::move(items);
  lane_element_index_by_key_.clear();
  lane_element_index_by_key_.reserve(item_map.size());
  for (const auto& [key, index] : item_map) {
    lane_element_index_by_key_.emplace(
        odr::LaneKey(std::get<0>(key), std::get<1>(key), std::get<2>(key)),
        index);
  }
}

void GeoViewerWidget::BuildRoadmarkElementCache() {
  std::vector<SceneCachedElement> items;
  std::map<std::pair<std::string, double>, size_t> item_map;

  for (size_t i = 0; i < network_mesh_.roadmarks_mesh.indices.size(); i += 3) {
    const uint32_t vertex_index = network_mesh_.roadmarks_mesh.indices[i];
    const std::string road_id =
        network_mesh_.roadmarks_mesh.get_road_id(vertex_index);
    const double s0 = network_mesh_.roadmarks_mesh.get_lanesec_s0(vertex_index);
    auto key = std::make_pair(road_id, s0);

    AddTriangleRange(items, item_map, key, static_cast<uint32_t>(i / 3), [&]() {
      SceneCachedElement element;
      element.road_key = "R:" + road_id;
      element.group_key = "G:" + road_id + ":section";
      return element;
    });
  }

  roadmark_element_items_ = std::move(items);
}

void GeoViewerWidget::BuildObjectElementCache() {
  std::vector<SceneCachedElement> items;
  std::map<std::pair<std::string, std::string>, size_t> item_map;

  for (size_t i = 0; i < network_mesh_.road_objects_mesh.indices.size();
       i += 3) {
    const uint32_t vertex_index = network_mesh_.road_objects_mesh.indices[i];
    const std::string road_id =
        network_mesh_.road_objects_mesh.get_road_id(vertex_index);
    const std::string object_id =
        network_mesh_.road_objects_mesh.get_road_object_id(vertex_index);
    auto key = std::make_pair(road_id, object_id);

    AddTriangleRange(items, item_map, key, static_cast<uint32_t>(i / 3), [&]() {
      SceneCachedElement element;
      element.road_key = "R:" + road_id;
      element.group_key = "G:" + road_id + ":objects";
      element.element_key = "E:" + road_id + ":objects:" + object_id;
      return element;
    });
  }

  object_element_items_ = std::move(items);
}

void GeoViewerWidget::BuildSignalElementCache() {
  std::vector<SceneCachedElement> items;
  std::map<std::pair<std::string, std::string>, size_t> item_map;

  for (size_t i = 0; i < network_mesh_.road_signals_mesh.indices.size();
       i += 3) {
    const uint32_t vertex_index = network_mesh_.road_signals_mesh.indices[i];
    const std::string road_id =
        network_mesh_.road_signals_mesh.get_road_id(vertex_index);
    const std::string signal_id =
        network_mesh_.road_signals_mesh.get_road_signal_id(vertex_index);
    auto key = std::make_pair(road_id, signal_id);

    AddTriangleRange(items, item_map, key, static_cast<uint32_t>(i / 3), [&]() {
      SceneCachedElement element;
      bool is_light = false;
      if (map_->id_to_road.count(road_id)) {
        const auto& road = map_->id_to_road.at(road_id);
        if (road.id_to_signal.count(signal_id)) {
          is_light = (road.id_to_signal.at(signal_id).name == "TrafficLight");
        }
      }
      const std::string group = is_light ? "light" : "sign";
      element.road_key = "R:" + road_id;
      element.group_key = "G:" + road_id + ":" + group;
      element.element_key = "E:" + road_id + ":" + group + ":" + signal_id;
      return element;
    });
  }

  signal_element_items_ = std::move(items);
}

void GeoViewerWidget::BuildOutlineElementCache() {
  const std::vector<size_t> outline_indices =
      network_mesh_.lanes_mesh.get_lane_outline_indices();
  lane_outline_indices_.clear();
  lane_outline_indices_.reserve(outline_indices.size());
  for (const auto& idx : outline_indices) {
    lane_outline_indices_.push_back(static_cast<uint32_t>(idx));
  }

  std::vector<SceneOutlineElement> items;
  std::map<std::tuple<std::string, double, int>, size_t> item_map;

  for (size_t i = 0; i < lane_outline_indices_.size(); i += 2) {
    const size_t vertex_index = lane_outline_indices_[i];
    const std::string road_id =
        network_mesh_.lanes_mesh.get_road_id(vertex_index);
    const double s0 = network_mesh_.lanes_mesh.get_lanesec_s0(vertex_index);
    const int lane_id = network_mesh_.lanes_mesh.get_lane_id(vertex_index);
    auto key = std::make_tuple(road_id, s0, lane_id);

    if (item_map.find(key) == item_map.end()) {
      item_map[key] = items.size();
      SceneOutlineElement element;
      const std::string s0_string = FormatSectionValue(s0);
      element.road_key = "R:" + road_id;
      element.group_key = "G:" + road_id + ":section";
      element.element_key =
          "E:" + road_id + ":lane:" + s0_string + ":" + std::to_string(lane_id);
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

      items.push_back(element);
    }

    auto& element = items[item_map[key]];
    const uint32_t line_index = static_cast<uint32_t>(i / 2);
    if (!element.ranges.empty() &&
        element.ranges.back().start + element.ranges.back().count ==
            line_index) {
      element.ranges.back().count++;
    } else {
      element.ranges.push_back({line_index, 1});
    }
  }

  outline_element_items_ = std::move(items);
}

void GeoViewerWidget::TransformSceneMeshes() {
  auto transform = [this](odr::Vec3D& vertex) {
    if (right_hand_traffic_) vertex[1] = -vertex[1];
    std::swap(vertex[1], vertex[2]);
  };

  std::vector<std::future<void>> tasks;
  auto& pool = geoviewer::utility::ThreadPool::Instance();
  tasks.push_back(pool.Enqueue([&]() {
    for (auto& v : network_mesh_.lanes_mesh.vertices) transform(v);
  }));
  tasks.push_back(pool.Enqueue([&]() {
    for (auto& v : network_mesh_.roadmarks_mesh.vertices) transform(v);
  }));
  tasks.push_back(pool.Enqueue([&]() {
    for (auto& v : network_mesh_.road_objects_mesh.vertices) transform(v);
  }));
  tasks.push_back(pool.Enqueue([&]() {
    for (auto& v : network_mesh_.road_signals_mesh.vertices) transform(v);
  }));

  for (auto& task : tasks) {
    task.get();
  }
}

std::vector<float> GeoViewerWidget::BuildSceneVertexBufferData() {
  if (!gl_renderer_) return {};
  std::vector<float> vertices;
  std::size_t total_vertex_count =
      network_mesh_.lanes_mesh.vertices.size() +
      network_mesh_.roadmarks_mesh.vertices.size() +
      network_mesh_.road_objects_mesh.vertices.size() +
      junction_mesh_.vertices.size() +
      network_mesh_.road_signals_mesh.vertices.size();

  for (const auto& [road_id, road] : map_->id_to_road) {
    static_cast<void>(road_id);
    total_vertex_count += static_cast<std::size_t>(road.length / 2.0) * 2 + 16;
  }

  vertices.reserve(total_vertex_count * 3);

  auto append_mesh = [&](const odr::Mesh3D& mesh, LayerType type) {
    gl_renderer_->SetLayerVertexOffset(type, vertices.size() / 3);
    for (const auto& vertex : mesh.vertices) {
      vertices.push_back(vertex[0]);
      vertices.push_back(vertex[1]);
      vertices.push_back(vertex[2]);
    }
  };

  append_mesh(network_mesh_.lanes_mesh, LayerType::kLanes);
  append_mesh(network_mesh_.roadmarks_mesh, LayerType::kRoadmarks);
  append_mesh(network_mesh_.road_objects_mesh, LayerType::kObjects);
  append_mesh(junction_mesh_, LayerType::kJunctions);

  gl_renderer_->SetLayerVertexOffset(LayerType::kReferenceLines,
                                     vertices.size() / 3);
  GenerateRefLinePoints(map_, vertices, road_ref_line_vert_ranges_);

  gl_renderer_->SetLayerVertexOffset(LayerType::kSignalLights,
                                     vertices.size() / 3);
  gl_renderer_->SetLayerVertexOffset(LayerType::kSignalSigns,
                                     vertices.size() / 3);
  for (const auto& vertex : network_mesh_.road_signals_mesh.vertices) {
    vertices.push_back(vertex[0]);
    vertices.push_back(vertex[1]);
    vertices.push_back(vertex[2]);
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

  gl_renderer_->SetLayerColor(LayerType::kLanes, QVector3D(0.75f, 0.75f, 0.75f));
  gl_renderer_->SetLayerAlpha(LayerType::kLanes, 0.4f);

  gl_renderer_->SetLayerColor(LayerType::kLaneLines,
                              QVector3D(1.0f, 1.0f, 0.0f));
  gl_renderer_->SetLayerAlpha(LayerType::kLaneLines, 1.0f);
  gl_renderer_->SetLayerDrawMode(LayerType::kLaneLines, GL_LINES);
  gl_renderer_->SetLayerPolygonOffset(LayerType::kLaneLines, -1.0f, -1.0f);

  gl_renderer_->SetLayerColor(LayerType::kLaneLinesDashed,
                              QVector3D(1.0f, 1.0f, 0.0f));
  gl_renderer_->SetLayerAlpha(LayerType::kLaneLinesDashed, 0.8f);
  gl_renderer_->SetLayerDrawMode(LayerType::kLaneLinesDashed, GL_LINES);
  gl_renderer_->SetLayerPolygonOffset(LayerType::kLaneLinesDashed, -1.0f, -1.0f);

  gl_renderer_->SetLayerColor(LayerType::kRoadmarks,
                              QVector3D(1.0f, 1.0f, 1.0f));
  gl_renderer_->SetLayerAlpha(LayerType::kRoadmarks, 1.0f);
  gl_renderer_->SetLayerPolygonOffset(LayerType::kRoadmarks, -1.0f, -1.0f);

  gl_renderer_->SetLayerColor(LayerType::kObjects, QVector3D(0.8f, 0.5f, 0.3f));
  gl_renderer_->SetLayerAlpha(LayerType::kObjects, 1.0f);

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
  gl_renderer_->SetLayerAlpha(LayerType::kJunctions, 0.45f);
  gl_renderer_->SetLayerPolygonOffset(LayerType::kJunctions, -2.5f, -2.5f);
}

void GeoViewerWidget::FinalizeSceneUpdate() {
  mesh_updated_ = true;
  CalculateMeshCenter();
  spatial_grid_ready_ = false;
  grid_boxes_.clear();
  StartSpatialGridBuild();
}

void GeoViewerWidget::ReloadMeshData() {
  if (!map_) return;

  makeCurrent();
  road_ref_line_vert_ranges_.clear();
  UploadVertexBufferData(BuildSceneVertexBufferData());
  UpdateMeshIndices();
  doneCurrent();
}
