#include "src/core/map_loader.h"

#include <future>
#include <iostream>
#include "Lane.h"
#include "OpenDriveMap.h"
#include "Road.h"
#include "RoadNetworkMesh.h"
#include "RoutingGraph.h"
#include "src/core/coordinate_util.h"
#include "src/core/thread_pool.h"

namespace {

odr::RoadNetworkMesh GenerateRoadNetworkMeshParallel(
    const std::shared_ptr<odr::OpenDriveMap>& map, double eps) {
  if (!map || map->id_to_road.empty()) {
    return odr::RoadNetworkMesh();
  }

  std::vector<std::future<odr::RoadNetworkMesh>> futures;
  futures.reserve(map->id_to_road.size());

  for (const auto& [road_id, road] : map->id_to_road) {
    futures.push_back(geoviewer::utility::ThreadPool::Instance().Enqueue(
        [road_ptr = &road, eps]() {
          odr::RoadNetworkMesh out_mesh;
          odr::LanesMesh& lanes_mesh = out_mesh.lanes_mesh;
          odr::RoadmarksMesh& roadmarks_mesh = out_mesh.roadmarks_mesh;
          odr::RoadObjectsMesh& road_objects_mesh = out_mesh.road_objects_mesh;
          odr::RoadSignalsMesh& road_signals_mesh = out_mesh.road_signals_mesh;

          lanes_mesh.road_start_indices[lanes_mesh.vertices.size()] =
              road_ptr->id;
          roadmarks_mesh.road_start_indices[roadmarks_mesh.vertices.size()] =
              road_ptr->id;
          road_objects_mesh
              .road_start_indices[road_objects_mesh.vertices.size()] =
              road_ptr->id;

          for (const auto& s_lanesec : road_ptr->s_to_lanesection) {
            const odr::LaneSection& lanesec = s_lanesec.second;
            lanes_mesh.lanesec_start_indices[lanes_mesh.vertices.size()] =
                lanesec.s0;
            roadmarks_mesh
                .lanesec_start_indices[roadmarks_mesh.vertices.size()] =
                lanesec.s0;
            for (const auto& id_lane : lanesec.id_to_lane) {
              const odr::Lane& lane = id_lane.second;
              const std::size_t lanes_idx_offset = lanes_mesh.vertices.size();
              lanes_mesh.lane_start_indices[lanes_idx_offset] = lane.id;
              lanes_mesh.add_mesh(road_ptr->get_lane_mesh(lane, eps));

              std::size_t roadmarks_idx_offset = roadmarks_mesh.vertices.size();
              roadmarks_mesh.lane_start_indices[roadmarks_idx_offset] = lane.id;
              const std::vector<odr::RoadMark> roadmarks = lane.get_roadmarks(
                  lanesec.s0, road_ptr->get_lanesection_end(lanesec));
              for (const odr::RoadMark& roadmark : roadmarks) {
                roadmarks_idx_offset = roadmarks_mesh.vertices.size();
                roadmarks_mesh
                    .roadmark_type_start_indices[roadmarks_idx_offset] =
                    roadmark.type;
                roadmarks_mesh.add_mesh(
                    road_ptr->get_roadmark_mesh(lane, roadmark, eps));
              }
            }
          }

          for (const auto& id_road_object : road_ptr->id_to_object) {
            const odr::RoadObject& road_object = id_road_object.second;
            const std::size_t road_objs_idx_offset =
                road_objects_mesh.vertices.size();
            road_objects_mesh.road_object_start_indices[road_objs_idx_offset] =
                road_object.id;
            road_objects_mesh.add_mesh(
                road_ptr->get_road_object_mesh(road_object, eps));
          }

          for (const auto& id_signal : road_ptr->id_to_signal) {
            const odr::RoadSignal& road_signal = id_signal.second;
            const std::size_t signals_idx_offset =
                road_signals_mesh.vertices.size();
            road_signals_mesh.road_signal_start_indices[signals_idx_offset] =
                road_signal.id;
            road_signals_mesh.add_mesh(
                road_ptr->get_road_signal_mesh(road_signal));
          }

          return out_mesh;
        }));
  }

  odr::RoadNetworkMesh combined_mesh;

  for (auto& fut : futures) {
    odr::RoadNetworkMesh road_mesh = fut.get();

    // Merge lanes_mesh
    {
      const std::size_t offset = combined_mesh.lanes_mesh.vertices.size();
      combined_mesh.lanes_mesh.add_mesh(road_mesh.lanes_mesh);
      for (const auto& [local_idx, road_id] :
           road_mesh.lanes_mesh.road_start_indices) {
        combined_mesh.lanes_mesh.road_start_indices[local_idx + offset] =
            road_id;
      }
      for (const auto& [local_idx, lanesec_s0] :
           road_mesh.lanes_mesh.lanesec_start_indices) {
        combined_mesh.lanes_mesh.lanesec_start_indices[local_idx + offset] =
            lanesec_s0;
      }
      for (const auto& [local_idx, lane_id] :
           road_mesh.lanes_mesh.lane_start_indices) {
        combined_mesh.lanes_mesh.lane_start_indices[local_idx + offset] =
            lane_id;
      }
    }

    // Merge roadmarks_mesh
    {
      const std::size_t offset = combined_mesh.roadmarks_mesh.vertices.size();
      combined_mesh.roadmarks_mesh.add_mesh(road_mesh.roadmarks_mesh);
      for (const auto& [local_idx, road_id] :
           road_mesh.roadmarks_mesh.road_start_indices) {
        combined_mesh.roadmarks_mesh.road_start_indices[local_idx + offset] =
            road_id;
      }
      for (const auto& [local_idx, lanesec_s0] :
           road_mesh.roadmarks_mesh.lanesec_start_indices) {
        combined_mesh.roadmarks_mesh.lanesec_start_indices[local_idx + offset] =
            lanesec_s0;
      }
      for (const auto& [local_idx, lane_id] :
           road_mesh.roadmarks_mesh.lane_start_indices) {
        combined_mesh.roadmarks_mesh.lane_start_indices[local_idx + offset] =
            lane_id;
      }
      for (const auto& [local_idx, roadmark_type] :
           road_mesh.roadmarks_mesh.roadmark_type_start_indices) {
        combined_mesh.roadmarks_mesh
            .roadmark_type_start_indices[local_idx + offset] = roadmark_type;
      }
    }

    // Merge road_objects_mesh
    {
      const std::size_t offset =
          combined_mesh.road_objects_mesh.vertices.size();
      combined_mesh.road_objects_mesh.add_mesh(road_mesh.road_objects_mesh);
      for (const auto& [local_idx, road_id] :
           road_mesh.road_objects_mesh.road_start_indices) {
        combined_mesh.road_objects_mesh.road_start_indices[local_idx + offset] =
            road_id;
      }
      for (const auto& [local_idx, obj_id] :
           road_mesh.road_objects_mesh.road_object_start_indices) {
        combined_mesh.road_objects_mesh
            .road_object_start_indices[local_idx + offset] = obj_id;
      }
    }

    // Merge road_signals_mesh
    {
      const std::size_t offset =
          combined_mesh.road_signals_mesh.vertices.size();
      combined_mesh.road_signals_mesh.add_mesh(road_mesh.road_signals_mesh);
      for (const auto& [local_idx, road_id] :
           road_mesh.road_signals_mesh.road_start_indices) {
        combined_mesh.road_signals_mesh.road_start_indices[local_idx + offset] =
            road_id;
      }
      for (const auto& [local_idx, sig_id] :
           road_mesh.road_signals_mesh.road_signal_start_indices) {
        combined_mesh.road_signals_mesh
            .road_signal_start_indices[local_idx + offset] = sig_id;
      }
    }
  }

  return combined_mesh;
}

}  // namespace

MapSceneData OpenDriveMapSceneLoader::Load(
    const std::string& path,
    std::function<void(float, const std::string&)> progress_callback) const {
  MapSceneData data;
  try {
    if (progress_callback) {
      progress_callback(0.05f, "Parsing OpenDRIVE XML file...");
    }
    data.map = std::make_shared<odr::OpenDriveMap>(path);

    if (progress_callback) {
      progress_callback(0.35f, "Initializing coordinate system projection...");
    }
    try {
      CoordinateUtil::Instance().Init(data.map->proj4, data.map->x_offs,
                                      data.map->y_offs);
      data.georeference_valid = true;
    } catch (const std::exception& georef_error) {
      std::cerr << "Invalid georeference, falling back to local coordinates: "
                << georef_error.what() << '\n';
      data.georeference_valid = false;
    }

    if (progress_callback) {
      progress_callback(0.40f, "Analyzing junction clusters...");
    }
    data.junction_grouping = JunctionClusterUtil::Analyze(*data.map);

    if (progress_callback) {
      progress_callback(
          0.60f,
          "Generating road network mesh (this may take a few seconds)...");
    }
    data.mesh = GenerateRoadNetworkMeshParallel(data.map, 0.75);

    if (progress_callback) {
      progress_callback(0.85f, "Building routing graph topology...");
    }
    data.routing_graph =
        std::make_shared<odr::RoutingGraph>(data.map->get_routing_graph());

    if (progress_callback) {
      progress_callback(1.00f, "Map loading complete.");
    }
  } catch (const std::exception& e) {
    std::cerr << "OpenDRIVE load error: " << e.what() << '\n';
    data = MapSceneData();
  }
  return data;
}
