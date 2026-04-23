add_library(GeoViewerLogic ${LIB_TYPE}
    favorites_store.cpp
    input_parsing.cpp
    routing_logic.cpp
    camera_controller.cpp
    highlight_manager.cpp
    measure_tool_controller.cpp
    routing_buffer_manager.cpp
    scene_index_builder.cpp
    spatial_grid_index.cpp
)

target_include_directories(GeoViewerLogic PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../
)


target_link_libraries(GeoViewerLogic PUBLIC 
    GeoViewerCore
    Qt6::Core Qt6::Gui
    OpenDrive
    pugixml
)
