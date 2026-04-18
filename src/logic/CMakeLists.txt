add_library(GeoViewerLogic SHARED
    scene_manager.cpp
)

target_include_directories(GeoViewerLogic PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../
)

target_link_libraries(GeoViewerLogic PUBLIC 
    GeoViewerCore
    OpenDrive
)
