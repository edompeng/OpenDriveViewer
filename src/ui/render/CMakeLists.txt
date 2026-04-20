add_library(GeoViewerGlRenderer ${LIB_TYPE}
    gl_renderer.cpp
)

target_include_directories(GeoViewerGlRenderer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../
)


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
