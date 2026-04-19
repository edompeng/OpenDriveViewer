# Core utility (no PROJ dependency)
add_library(GeoViewerUtilityCore SHARED
  favorites_store.cpp
  input_parsing.cpp
  junction_grouping.cpp
  routing_logic.cpp
  viewer_text_util.cpp
)

target_include_directories(GeoViewerUtilityCore PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(GeoViewerUtilityCore PUBLIC OpenDrive pugixml::pugixml)

# Coordinate utility (PROJ dependency)
add_library(GeoViewerCoordinateUtil SHARED
  coordinate_util.cpp
)

target_include_directories(GeoViewerCoordinateUtil PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(GeoViewerCoordinateUtil PUBLIC PROJ::proj)

# Map loader (depends on both)
add_library(GeoViewerMapLoader SHARED
  map_loader.cpp
)

target_include_directories(GeoViewerMapLoader PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(GeoViewerMapLoader PUBLIC GeoViewerUtilityCore GeoViewerCoordinateUtil OpenDrive)

# Umbrella target for backward compatibility or ease of use
add_library(GeoViewerUtility INTERFACE)
target_link_libraries(GeoViewerUtility INTERFACE 
  GeoViewerUtilityCore 
  GeoViewerCoordinateUtil 
  GeoViewerMapLoader
)
