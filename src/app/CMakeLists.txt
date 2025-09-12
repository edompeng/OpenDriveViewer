add_executable(OpenDriveViewer WIN32 MACOSX_BUNDLE
  main.cpp
  main_window.cpp main_window.h
  geo_viewer.cpp geo_viewer.h
  geo_viewer_scene.cpp
  scene_index_builder.cpp scene_index_builder.h
  spatial_grid_index.cpp spatial_grid_index.h
  scene_geometry_types.h
  async_map_loader.cpp async_map_loader.h
  floating_panel_widget.cpp floating_panel_widget.h
  layer_control_widget.cpp layer_control_widget.h
  open_scenario_widget.cpp open_scenario_widget.h
  coordinate_points_widget.cpp coordinate_points_widget.h
  routing_widget.cpp routing_widget.h
  loading_progress_widget.cpp loading_progress_widget.h
  favorites_widget.cpp favorites_widget.h
  camera_controller.cpp camera_controller.h
  highlight_manager.cpp highlight_manager.h
  measure_tool_controller.cpp measure_tool_controller.h
  routing_buffer_manager.cpp routing_buffer_manager.h
  layer_tree_model.cpp layer_tree_model.h
  open_scenario_parser.cpp open_scenario_parser.h
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
  Qt6::Widgets Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Concurrent
  GeoViewerUtility OpenGL::GL)

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
