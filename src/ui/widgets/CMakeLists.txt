add_library(GeoViewerUIWidgets SHARED
    geo_viewer_widget.cpp
)

target_include_directories(GeoViewerUIWidgets PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../
)

target_link_libraries(GeoViewerUIWidgets PUBLIC 
    Qt6::Widgets
    GeoViewerCore
    GeoViewerRenderer
    GeoViewerLogic
)
