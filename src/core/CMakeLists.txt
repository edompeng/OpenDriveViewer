add_library(GeoViewerCore SHARED
    project_context.cpp
    coordinate_util.cpp
    map_loader.cpp
    thread_pool.cpp
    viewer_text_util.cpp
    junction_grouping.cpp
    # Headers only
    coordinate_mode_policy.h
    scene_enums.h
    scene_geometry_types.h
)

target_include_directories(GeoViewerCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../
)

# PROJ might be found as a target or as variables
if(TARGET PROJ::proj)
    target_link_libraries(GeoViewerCore PUBLIC PROJ::proj)
elseif(PROJ_LIBRARIES)
    target_link_libraries(GeoViewerCore PUBLIC ${PROJ_LIBRARIES})
    target_include_directories(GeoViewerCore PUBLIC ${PROJ_INCLUDE_DIRS})
elseif(PROJ_LIBRARY)
    target_link_libraries(GeoViewerCore PUBLIC ${PROJ_LIBRARY})
    target_include_directories(GeoViewerCore PUBLIC ${PROJ_INCLUDE_DIR})
else()
    # Fallback for manual brew setup on Mac or system paths
    find_library(PROJ_LIB proj)
    if(PROJ_LIB)
        target_link_libraries(GeoViewerCore PUBLIC ${PROJ_LIB})
    endif()
endif()

target_link_libraries(GeoViewerCore PUBLIC 
    Qt6::Core
    OpenDrive
)
