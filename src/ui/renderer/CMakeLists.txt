add_library(GeoViewerRenderer SHARED
    scene_renderer.cpp
)

target_include_directories(GeoViewerRenderer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../
)

target_link_libraries(GeoViewerRenderer PUBLIC 
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    GeoViewerCore
)
