cmake_minimum_required(VERSION 3.16)
project(OpenDriveViewer LANGUAGES CXX)

# --- Platform & Toolchain ---
if(MSVC)
  add_compile_options(/Zc:__cplusplus /utf-8 /W3)
  add_definitions(-D_CRT_SECURE_NO_WARNINGS)
else()
  add_compile_options(-Wall -Wextra)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# --- Find Dependencies ---
# Attempt to use VCPKG if present
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif()

find_package(Qt6 COMPONENTS Widgets Gui OpenGL OpenGLWidgets Concurrent LinguistTools REQUIRED)
find_package(OpenGL REQUIRED)

# Proj finding with fallback
find_package(PROJ CONFIG QUIET)
if(NOT PROJ_FOUND)
  find_package(PROJ QUIET)
endif()

# Fallback to PROJ_ROOT if provided
if(NOT PROJ_FOUND AND PROJ_ROOT)
  set(PROJ_INCLUDE_DIR "${PROJ_ROOT}/include")
  set(PROJ_LIBRARY "${PROJ_ROOT}/lib/proj.lib")
  set(PROJ_FOUND INTERNAL)
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
)
# For Windows: prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

FetchContent_Declare(
  pugixml
  URL https://github.com/zeux/pugixml/archive/refs/tags/v1.15.tar.gz
)
FetchContent_MakeAvailable(pugixml)

FetchContent_Declare(
  libOpenDRIVE
  URL https://github.com/pageldev/libOpenDRIVE/archive/refs/tags/0.5.0.tar.gz
  PATCH_COMMAND patch -p1 < ${CMAKE_CURRENT_SOURCE_DIR}/third_party/patches/libOpenDRIVE.patch
)
FetchContent_MakeAvailable(libOpenDRIVE)

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
