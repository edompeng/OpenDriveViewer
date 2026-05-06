#include "src/core/map_loader.h"

#include <future>
#include <iostream>
#include "OpenDriveMap.h"
#include "src/core/coordinate_util.h"

MapSceneData OpenDriveMapSceneLoader::Load(const std::string& path) const {
  MapSceneData data;
  try {
    data.map = std::make_shared<odr::OpenDriveMap>(path);
    auto junction_future = std::async(std::launch::async, [&]() {
      return JunctionClusterUtil::Analyze(*data.map);
    });

    auto mesh_future = std::async(std::launch::async, [&]() {
      return data.map->get_road_network_mesh(0.75);
    });

    try {
      CoordinateUtil::Instance().Init(data.map->proj4, data.map->x_offs,
                                      data.map->y_offs);
      data.georeference_valid = true;
    } catch (const std::exception& georef_error) {
      std::cerr << "Invalid georeference, falling back to local coordinates: "
                << georef_error.what() << '\n';
      data.georeference_valid = false;
    }

    data.junction_grouping = junction_future.get();
    data.mesh = mesh_future.get();
  } catch (const std::exception& e) {
    std::cerr << "OpenDRIVE load error: " << e.what() << '\n';
    data = MapSceneData();
  }
  return data;
}
