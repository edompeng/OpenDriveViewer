
# Logic and data components shared between UI and App
add_library(GeoViewerAppCore SHARED
  scene_index_builder.cpp scene_index_builder.h
  spatial_grid_index.cpp spatial_grid_index.h
  scene_geometry_types.h
  async_map_loader.cpp async_map_loader.h
  camera_controller.cpp camera_controller.h
  highlight_manager.cpp highlight_manager.h
  measure_tool_controller.cpp measure_tool_controller.h
  routing_buffer_manager.cpp routing_buffer_manager.h
  layer_tree_model.cpp layer_tree_model.h
  open_scenario_parser.cpp open_scenario_parser.h
)

target_include_directories(GeoViewerAppCore PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(GeoViewerAppCore PUBLIC
  Qt6::Widgets Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Concurrent
  GeoViewerUtility 
  OpenGL::GL
  pugixml
)

add_executable(OpenDriveViewer WIN32 MACOSX_BUNDLE
  main.cpp
)

qt_add_translations(OpenDriveViewer
  TS_FILES
  ${CMAKE_SOURCE_DIR}/translations/geoviewer_zh_CN.ts
  ${CMAKE_SOURCE_DIR}/translations/geoviewer_en_US.ts
  RESOURCE_PREFIX "/i18n"
)

# Enable automatic lupdate on each build to capture new strings
qt_add_lupdate(OpenDriveViewer
  TS_FILES
  ${CMAKE_SOURCE_DIR}/translations/geoviewer_zh_CN.ts
  ${CMAKE_SOURCE_DIR}/translations/geoviewer_en_US.ts
)
# Ensure the lupdate target is created and captured
add_dependencies(OpenDriveViewer OpenDriveViewer_lupdate)

target_include_directories(OpenDriveViewer PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(OpenDriveViewer PRIVATE
  GeoViewerAppCore
  MainWindow
)

set(GEOVIEWER_APP_TEST_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/scene_index_builder.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/spatial_grid_index.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/camera_controller.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/layer_tree_model.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/measure_tool_controller.cpp
  PARENT_SCOPE)

if(APPLE)
  qt_finalize_executable(OpenDriveViewer)
elseif(WIN32)
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")
  set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} /SUBSYSTEM:CONSOLE")
  set(q_deploy ${Qt6_DIR}/bin/windeployqt.exe)
  add_custom_command(TARGET OpenDriveViewer POST_BUILD
    COMMAND "${q_deploy}" --no-translations --no-compiler-runtime
    "$<TARGET_FILE:OpenDriveViewer>"
  )
endif()
