add_executable(OpenDriveViewer WIN32 MACOSX_BUNDLE main.cpp)

qt_add_translations(
  OpenDriveViewer TS_FILES ${CMAKE_SOURCE_DIR}/translations/geoviewer_zh_CN.ts
  ${CMAKE_SOURCE_DIR}/translations/geoviewer_en_US.ts RESOURCE_PREFIX "/i18n")


target_include_directories(OpenDriveViewer PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(OpenDriveViewer PRIVATE GeoViewerUIWidgets MainWindow GeoViewerMcp)

if(NOT APPLE)
  find_package(gperftools CONFIG QUIET)
  if(gperftools_FOUND)
    target_link_libraries(OpenDriveViewer PRIVATE gperftools::tcmalloc)
  else()
    find_library(TCMALLOC_LIB tcmalloc)
    if(TCMALLOC_LIB)
      target_link_libraries(OpenDriveViewer PRIVATE ${TCMALLOC_LIB})
    endif()
  endif()
endif()

if(APPLE)
  qt_finalize_executable(OpenDriveViewer)
elseif(WIN32)
  set(CMAKE_EXE_LINKER_FLAGS
    "${CMAKE_EXE_LINKER_FLAGS} /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")
  set(CMAKE_EXE_LINKER_FLAGS_DEBUG
    "${CMAKE_EXE_LINKER_FLAGS_DEBUG} /SUBSYSTEM:CONSOLE")
  add_custom_command(
    TARGET OpenDriveViewer
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:OpenDrive>"
    "$<TARGET_FILE_DIR:OpenDriveViewer>")
endif()
