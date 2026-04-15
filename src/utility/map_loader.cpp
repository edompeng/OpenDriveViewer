#include "src/utility/map_loader.h"

#include <iostream>
#include "src/utility/coordinate_util.h"
#include "third_party/libOpenDRIVE/include/OpenDriveMap.h"

MapSceneData OpenDriveMapSceneLoader::Load(const std::string& path) const {
  MapSceneData data;
  try {
    data.map = std::make_shared<odr::OpenDriveMap>(path);
    data.junction_grouping = JunctionClusterUtil::Analyze(*data.map);
    try {
      CoordinateUtil::Instance().Init(data.map->proj4, data.map->x_offs,
                                      data.map->y_offs);
      data.georeference_valid = true;
    } catch (const std::exception& georef_error) {
      std::cerr << "Invalid georeference, falling back to local coordinates: "
                << georef_error.what() << '\n';
      data.georeference_valid = false;
    }
    data.mesh = data.map->get_road_network_mesh(0.75);
  } catch (const std::exception& e) {
    std::cerr << "OpenDRIVE load error: " << e.what() << '\n';
    data = MapSceneData();
  }
  return data;
}
