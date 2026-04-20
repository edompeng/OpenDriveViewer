
add_library(GeoViewerUIWidgets ${LIB_TYPE}
  geo_viewer.cpp geo_viewer.h
  geo_viewer_scene.cpp
  floating_panel_widget.cpp floating_panel_widget.h
  layer_control_widget.cpp layer_control_widget.h
  coordinate_points_widget.cpp coordinate_points_widget.h
  routing_widget.cpp routing_widget.h
  loading_progress_widget.cpp loading_progress_widget.h
  favorites_widget.cpp favorites_widget.h
  async_map_loader.cpp async_map_loader.h
  layer_tree_model.cpp layer_tree_model.h
)

target_include_directories(GeoViewerUIWidgets PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../
)


target_link_libraries(GeoViewerUIWidgets PUBLIC 
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    GeoViewerCore
    GeoViewerGlRenderer
    GeoViewerLogic
)

if(WIN32)
    target_link_libraries(GeoViewerUIWidgets PRIVATE opengl32)
endif()
