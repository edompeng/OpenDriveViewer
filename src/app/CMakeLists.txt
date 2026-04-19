
add_executable(OpenDriveViewer WIN32 MACOSX_BUNDLE
  main.cpp
)

qt_add_translations(OpenDriveViewer
  TS_FILES
  ${CMAKE_SOURCE_DIR}/translations/geoviewer_zh_CN.ts
  ${CMAKE_SOURCE_DIR}/translations/geoviewer_en_US.ts
  RESOURCE_PREFIX "/i18n"
)

# Automatic lupdate is disabled to prevent accidental wiping of .ts files.
# Run lupdate manually or configure it to scan all sub-libraries if needed.
# qt_add_lupdate(OpenDriveViewer
#   TS_FILES
#   ${CMAKE_SOURCE_DIR}/translations/geoviewer_zh_CN.ts
#   ${CMAKE_SOURCE_DIR}/translations/geoviewer_en_US.ts
# )
# add_dependencies(OpenDriveViewer OpenDriveViewer_lupdate)

target_include_directories(OpenDriveViewer PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(OpenDriveViewer PRIVATE
  GeoViewerUIWidgets
  MainWindow
)

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
