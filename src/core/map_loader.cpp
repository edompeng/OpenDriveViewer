#include "src/core/map_loader.h"

#include <future>
#include <iostream>
#include "OpenDriveMap.h"
#include "src/core/coordinate_util.h"

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
      progress_callback(0.60f, "Generating road network mesh (this may take a few seconds)...");
    }
    data.mesh = data.map->get_road_network_mesh(0.75);

    if (progress_callback) {
      progress_callback(1.00f, "Map loading complete.");
    }
  } catch (const std::exception& e) {
    std::cerr << "OpenDRIVE load error: " << e.what() << '\n';
    data = MapSceneData();
  }
  return data;
}
