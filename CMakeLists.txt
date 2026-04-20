cmake_minimum_required(VERSION 3.16)
project(OpenDriveViewer LANGUAGES CXX)

# --- Platform & Toolchain ---
if(MSVC)
  add_compile_options(/Zc:__cplusplus /utf-8 /W3 /wd4005 /wd4267 /FIiterator)
  add_definitions(-D_CRT_SECURE_NO_WARNINGS -D_USE_MATH_DEFINES -DPUGIXML_STATIC)
else()
  add_compile_options(-Wall -Wextra)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

# --- Find Dependencies ---
# Attempt to use VCPKG if present
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif()

find_package(Qt6 COMPONENTS Widgets Gui OpenGL OpenGLWidgets Concurrent LinguistTools REQUIRED)
find_package(OpenGL REQUIRED)

# Proj finding with PkgConfig fallback for Linux
find_package(PROJ CONFIG QUIET)
if(NOT PROJ_FOUND)
  find_package(PROJ QUIET)
endif()

if(NOT PROJ_FOUND AND NOT WIN32)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(PROJ QUIET proj)
  endif()
endif()

# Fallback to PROJ_ROOT if provided or if PkgConfig found it
if(NOT PROJ_FOUND AND PROJ_ROOT)
  set(PROJ_INCLUDE_DIR "${PROJ_ROOT}/include")
  if(WIN32)
    set(PROJ_LIBRARY "${PROJ_ROOT}/lib/proj.lib")
  else()
    set(PROJ_LIBRARY "${PROJ_ROOT}/lib/libproj.so")
    if(NOT EXISTS ${PROJ_LIBRARY})
       set(PROJ_LIBRARY "${PROJ_ROOT}/lib/libproj.dylib")
    endif()
  endif()
  set(PROJ_FOUND TRUE)
endif()

if(PROJ_FOUND AND NOT PROJ_LIBRARIES AND PROJ_LIBRARY)
  set(PROJ_LIBRARIES ${PROJ_LIBRARY})
  set(PROJ_INCLUDE_DIRS ${PROJ_INCLUDE_DIR})
endif()

# --- Configuration ---
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
enable_testing()

add_definitions(-DPROJ_NEW_API)

# --- Subdirectories ---
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
# For Windows: prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

FetchContent_Declare(
  pugixml
  URL https://github.com/zeux/pugixml/archive/refs/tags/v1.15.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(pugixml)

FetchContent_Declare(
  libOpenDRIVE
  URL https://github.com/pageldev/libOpenDRIVE/archive/refs/tags/0.5.0.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(libOpenDRIVE)

# Create a compatibility include directory for pugixml to match "pugixml/pugixml.hpp" structure
set(PUGIXML_COMPAT_DIR "${CMAKE_BINARY_DIR}/pugixml_compat")
file(MAKE_DIRECTORY "${PUGIXML_COMPAT_DIR}/pugixml")
configure_file("${pugixml_SOURCE_DIR}/src/pugixml.hpp" "${PUGIXML_COMPAT_DIR}/pugixml/pugixml.hpp" COPYONLY)
configure_file("${pugixml_SOURCE_DIR}/src/pugiconfig.hpp" "${PUGIXML_COMPAT_DIR}/pugixml/pugiconfig.hpp" COPYONLY)

# Fix for libOpenDRIVE 0.5.0
target_link_libraries(OpenDrive PUBLIC pugixml-static)
target_include_directories(OpenDrive SYSTEM BEFORE PUBLIC 
    $<BUILD_INTERFACE:${PUGIXML_COMPAT_DIR}>
)
target_include_directories(OpenDrive SYSTEM PUBLIC 
    $<BUILD_INTERFACE:${libopendrive_SOURCE_DIR}/include>
)
target_include_directories(OpenDrive SYSTEM PRIVATE 
    $<BUILD_INTERFACE:${libopendrive_SOURCE_DIR}/thirdparty>
)
target_compile_options(OpenDrive PRIVATE -w)
target_compile_options(pugixml-static PRIVATE -w)

add_subdirectory(src)

# --- Tests ---
macro(add_geoviewer_test name)
  add_executable(${name} ${ARGN})
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(${name} PRIVATE 
    GeoViewerCore 
    GeoViewerLogic
    GeoViewerUIWidgets
    gtest_main 
    Qt6::Gui
  )
  if(MSVC)
    target_compile_options(${name} PRIVATE /WX)
  else()
    target_compile_options(${name} PRIVATE -Werror)
  endif()
  target_compile_definitions(${name} PRIVATE
    GEOVIEWER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  add_test(NAME ${name} COMMAND ${name})
endmacro()

add_geoviewer_test(ProjectContextTests
  tests/project_context_test.cpp
)

add_geoviewer_test(ViewerCoreTests
  tests/scene_index_builder_test.cpp
  tests/spatial_grid_index_test.cpp
  tests/viewer_text_util_test.cpp
  tests/favorites_store_test.cpp
  tests/routing_logic_test.cpp
  tests/layer_tree_model_test.cpp
  tests/input_parsing_test.cpp
  tests/camera_controller_test.cpp
  tests/measure_tool_controller_test.cpp
  ${GEOVIEWER_APP_TEST_SOURCES}
)
