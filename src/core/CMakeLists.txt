add_library(GeoViewerCore SHARED
    project_context.cpp
)

target_include_directories(GeoViewerCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../
)

target_link_libraries(GeoViewerCore PUBLIC 
    GeoViewerUtilityCore
)
