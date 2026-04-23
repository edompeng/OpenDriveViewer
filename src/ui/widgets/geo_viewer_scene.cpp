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

  map_ = std::move(map);
  if (!map_) {
    ResetSceneData();
    if (had_user_points) {
      ClearUserPoints();
    }
    update();
    return;
  }

  network_mesh_ = std::make_shared<odr::RoadNetworkMesh>(std::move(network_mesh));
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
  routing_graph_ =
      std::make_unique<odr::RoutingGraph>(map_->get_routing_graph());

  signal_id_to_road_id_.clear();
  for (const auto& [road_id, road]: map_->id_to_road){
    for (const auto& [signal_id, signal]: road.id_to_signal) {
      signal_id_to_road_id_[signal_id] = road_id;
    }
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
  auto& network_mesh = *network_mesh_;
  // Pre-initialize libOpenDRIVE caches sequentially to avoid races in
  // background tasks
  if (!network_mesh.lanes_mesh.indices.empty()) {
    static_cast<void>(
        network_mesh.lanes_mesh.get_road_id(network_mesh.lanes_mesh.indices[0]));
    static_cast<void>(network_mesh.lanes_mesh.get_lane_outline_indices());
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

  lane_key_to_interval_.reserve(
      network_mesh.lanes_mesh.lane_start_indices.size());
  for (const auto& [start_idx, lane_id] :
       network_mesh.lanes_mesh.lane_start_indices) {
    const std::string road_id = network_mesh.lanes_mesh.get_road_id(start_idx);
    const double s0 = network_mesh.lanes_mesh.get_lanesec_s0(start_idx);
    lane_key_to_interval_[odr::LaneKey(road_id, s0, lane_id)] =
        network_mesh.lanes_mesh.get_idx_interval_lane(start_idx);
  }
}

void GeoViewerWidget::RebuildSceneCaches() {
  auto& pool = geoviewer::utility::ThreadPool::Instance();
  auto map = map_;
  auto network_mesh = network_mesh_;

  auto f_lane = pool.Enqueue(
      [this]() { return BuildLaneElementCache(network_mesh_); });
  auto f_roadmark = pool.Enqueue(
      [this]() { return BuildRoadmarkElementCache(network_mesh_); });
  auto f_object = pool.Enqueue([this]() {
    return BuildObjectElementCache(network_mesh_);
  });
  auto f_signal = pool.Enqueue([this, map]() { return BuildSignalElementCache(map, network_mesh_);
  });
  auto f_outline = pool.Enqueue(
      [this]() { return BuildOutlineElementCache(network_mesh_); });

  // Collect results and update member variables sequentially
  auto lane_res = f_lane.get();
  lane_element_items_ = std::move(lane_res.items);
  lane_element_index_by_key_ = std::move(lane_res.index_by_key);

  roadmark_element_items_ = f_roadmark.get();
  object_element_items_ = f_object.get();
  signal_element_items_ = f_signal.get();

  auto outline_res = f_outline.get();
  outline_element_items_ = std::move(outline_res.items);
  lane_outline_indices_ = std::move(outline_res.indices);
}

GeoViewerWidget::LaneCacheResult GeoViewerWidget::BuildLaneElementCache(
    const std::shared_ptr<odr::RoadNetworkMesh> network_mesh) const {
  const auto& mesh = network_mesh->lanes_mesh;
  LaneCacheResult result;
  std::map<odr::LaneKey, size_t> item_map;

  for (size_t i = 0; i < mesh.indices.size(); i += 3) {
    const uint32_t vertex_index = mesh.indices[i];
    const std::string road_id = mesh.get_road_id(vertex_index);
    const double s0 = mesh.get_lanesec_s0(vertex_index);
    const int lane_id = mesh.get_lane_id(vertex_index);
    auto key = odr::LaneKey(road_id, s0, lane_id);

    AddTriangleRange(result.items, item_map, key, static_cast<uint32_t>(i / 3),
                     [&]() {
                       SceneCachedElement element;
                       element.road_key = "R:" + road_id;
                       element.group_key = "G:" + road_id + ":section";
                       element.element_key = "E:" + road_id + ":lane:" +
                                             FormatSectionValue(s0) + ":" +
                                             std::to_string(lane_id);
                       return element;
                     });
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

  for (size_t i = 0; i < mesh.indices.size(); i += 3) {
    const uint32_t vertex_index = mesh.indices[i];
    const std::string road_id = mesh.get_road_id(vertex_index);
    const std::string type = mesh.get_roadmark_type(vertex_index);
    auto key = std::make_pair(road_id, type);

    AddTriangleRange(items, item_map, key, static_cast<uint32_t>(i / 3), [&]() {
      SceneCachedElement element;
      element.road_key = "R:" + road_id;
      element.group_key = "G:" + road_id + ":section";
      element.element_key = "E:" + road_id + ":roadmark:" + type;
      return element;
    });
  }

  return items;
}

std::vector<SceneCachedElement> GeoViewerWidget::BuildObjectElementCache(
    const std::shared_ptr<odr::RoadNetworkMesh> network_mesh)const {
  const auto& mesh = network_mesh->road_objects_mesh;
  std::vector<SceneCachedElement> items;
  std::map<std::pair<std::string, std::string>, size_t> item_map;

  for (size_t i = 0; i < mesh.indices.size(); i += 3) {
    const uint32_t vertex_index = mesh.indices[i];
    const std::string road_id = mesh.get_road_id(vertex_index);
    const std::string object_id = mesh.get_road_object_id(vertex_index);
    auto key = std::make_pair(road_id, object_id);

    AddTriangleRange(items, item_map, key, static_cast<uint32_t>(i / 3), [&]() {
      SceneCachedElement element;
      element.road_key = "R:" + road_id;
      element.group_key = "G:" + road_id + ":objects";
      element.element_key = "E:" + road_id + ":objects:" + object_id;
      return element;
    });
  }

  return items;
}

std::vector<SceneCachedElement> GeoViewerWidget::BuildSignalElementCache(
    const std::shared_ptr<odr::OpenDriveMap> map,
    const std::shared_ptr<odr::RoadNetworkMesh> network_mesh) const{
  const auto& mesh = network_mesh->road_signals_mesh;
  std::vector<SceneCachedElement> items;
  std::map<std::pair<std::string, std::string>, size_t> item_map;

  for (size_t i = 0; i < mesh.indices.size(); i += 3) {
    const uint32_t vertex_index = mesh.indices[i];
    //const std::string road_id = mesh.get_road_id(vertex_index);
    const std::string signal_id = mesh.get_road_signal_id(vertex_index);
    std::string road_id = "";
    auto itr = signal_id_to_road_id_.find(signal_id);
    if (itr != signal_id_to_road_id_.end()){
      road_id = itr->second;
    }
    auto key = std::make_pair(road_id, signal_id);

    AddTriangleRange(items, item_map, key, static_cast<uint32_t>(i / 3), [&]() {
      SceneCachedElement element;
      bool is_light = false;
      if (map->id_to_road.count(road_id)) {
        const auto& road = map->id_to_road.at(road_id);
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

  return items;
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

  for (size_t i = 0; i < result.indices.size(); i += 2) {
    const size_t vertex_index = result.indices[i];
    const std::string road_id = mesh.get_road_id(vertex_index);
    const double s0 = mesh.get_lanesec_s0(vertex_index);
    const int lane_id = mesh.get_lane_id(vertex_index);
    auto key = std::make_tuple(road_id, s0, lane_id);

    if (item_map.find(key) == item_map.end()) {
      item_map[key] = result.items.size();
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

      result.items.push_back(element);
    }

    auto& element = result.items[item_map[key]];
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
  auto transform = [this](odr::Vec3D& v) {
    auto p = LocalToRendererPoint(v);
    v[0] = p.x();
    v[1] = p.y();
    v[2] = p.z();
  };

  std::vector<std::future<void>> tasks;
  auto& pool = geoviewer::utility::ThreadPool::Instance();
  tasks.push_back(pool.Enqueue([&]() {
    for (auto& v : network_mesh.lanes_mesh.vertices) transform(v);
  }));
  tasks.push_back(pool.Enqueue([&]() {
    for (auto& v : network_mesh.roadmarks_mesh.vertices) transform(v);
  }));
  tasks.push_back(pool.Enqueue([&]() {
    for (auto& v : network_mesh.road_objects_mesh.vertices) transform(v);
  }));
  tasks.push_back(pool.Enqueue([&]() {
    for (auto& v : network_mesh.road_signals_mesh.vertices) transform(v);
  }));

  for (auto& task : tasks) {
    task.get();
  }
}

std::vector<float> GeoViewerWidget::BuildSceneVertexBufferData() {
  if (!gl_renderer_ || !network_mesh_ || !junction_mesh_) return {};
  auto& network_mesh = *network_mesh_;
  auto& junction_mesh = *junction_mesh_;
  std::vector<float> vertices;
  std::size_t total_vertex_count =
      network_mesh.lanes_mesh.vertices.size() +
      network_mesh.roadmarks_mesh.vertices.size() +
      network_mesh.road_objects_mesh.vertices.size() +
      junction_mesh.vertices.size() +
      network_mesh.road_signals_mesh.vertices.size();

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

  append_mesh(network_mesh.lanes_mesh, LayerType::kLanes);
  append_mesh(network_mesh.roadmarks_mesh, LayerType::kRoadmarks);
  append_mesh(network_mesh.road_objects_mesh, LayerType::kObjects);
  append_mesh(junction_mesh, LayerType::kJunctions);

  gl_renderer_->SetLayerVertexOffset(LayerType::kReferenceLines,
                                     vertices.size() / 3);
  GenerateRefLinePoints(map_, vertices, road_ref_line_vert_ranges_);

  gl_renderer_->SetLayerVertexOffset(LayerType::kSignalLights,
                                     vertices.size() / 3);
  gl_renderer_->SetLayerVertexOffset(LayerType::kSignalSigns,
                                     vertices.size() / 3);
  for (const auto& vertex : network_mesh.road_signals_mesh.vertices) {
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

  road_ref_line_vert_ranges_.clear();
  auto vertices = BuildSceneVertexBufferData();

  bool was_current = (QOpenGLContext::currentContext() == context());
  if (!was_current) makeCurrent();
  UploadVertexBufferData(vertices);
  if (!was_current) doneCurrent();

  UpdateMeshIndices();
}


std::string GeoViewerWidget::GetRoadIdBySignalId(
    const std::string& signal_id) const noexcept {
    auto itr = signal_id_to_road_id_.find(signal_id);
  if (itr == signal_id_to_road_id_.end()) {
      return "";
  }
  return itr->second;
}
