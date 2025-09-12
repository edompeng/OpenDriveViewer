add_library(GeoViewerUtility STATIC
  coordinate_util.cpp coordinate_util.h
  favorites_store.cpp favorites_store.h
  input_parsing.cpp input_parsing.h
  junction_grouping.cpp junction_grouping.h
  map_loader.cpp map_loader.h
  routing_logic.cpp routing_logic.h
  viewer_text_util.cpp viewer_text_util.h
  scene_enums.h
)

target_include_directories(GeoViewerUtility PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(GeoViewerUtility PUBLIC OpenDrive pugixml::pugixml PROJ::proj)
