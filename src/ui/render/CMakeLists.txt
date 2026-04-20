add_library(GeoViewerGlRenderer SHARED
    gl_renderer.cpp
)

target_include_directories(GeoViewerGlRenderer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../
)

target_compile_definitions(GeoViewerGlRenderer PRIVATE GEOVIEWER_RENDER_LIB)

target_link_libraries(GeoViewerGlRenderer PUBLIC
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    GeoViewerCore
    GeoViewerLogic
)
if(WIN32)
    target_link_libraries(GeoViewerGlRenderer PRIVATE opengl32)
endif()
