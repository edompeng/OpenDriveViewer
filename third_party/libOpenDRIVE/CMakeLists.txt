cmake_minimum_required(VERSION 3.13)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

project(libOpenDrive VERSION 0.6.0 DESCRIPTION ".xodr library")
set(CMAKE_CXX_STANDARD 17)
if(CMAKE_SYSTEM_NAME MATCHES "Windows")
set(CMAKE_CXX_STANDARD_REQUIRED True)
endif()
add_compile_definitions(_USE_MATH_DEFINES)
if(MSVC)
    set(CMAKE_CXX_FLAGS "/EHsc /W3")
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS 1)
    set(CMAKE_CXX_FLAGS_RELEASE "/O2")
else()
    set(CMAKE_CXX_FLAGS "-Wall")
    set(CMAKE_CXX_FLAGS_DEBUG "-g")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3")
endif()

include(GNUInstallDirs)

set(SOURCES
    src/Geometries/Arc.cpp
    src/Geometries/CubicSpline.cpp
    src/Geometries/Line.cpp
    src/Geometries/ParamPoly3.cpp
    src/Geometries/RoadGeometry.cpp
    src/Geometries/Spiral.cpp
    src/Geometries/Spiral/odrSpiral.cpp
    src/Junction.cpp
    src/Lane.cpp
    src/LaneSection.cpp
    src/Log.cpp
    src/Mesh.cpp
    src/OpenDriveMap.cpp
    src/RefLine.cpp
    src/Road.cpp
    src/RoadMark.cpp
    src/RoadNetworkMesh.cpp
    src/RoadObject.cpp
    src/RoadSignal.cpp
    src/RoutingGraph.cpp
    ${pugixml_SOURCE_DIR}/src/pugixml.cpp
)

add_library(OpenDrive ${SOURCES})
target_include_directories(
    OpenDrive PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    $<BUILD_INTERFACE:${pugixml_SOURCE_DIR}/src>
)

install(
    TARGETS OpenDrive
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(
    FILES
    ${pugixml_SOURCE_DIR}/src/pugixml.hpp
    ${pugixml_SOURCE_DIR}/src/pugiconfig.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(TARGETS OpenDrive EXPORT OpenDriveConfig)
install(EXPORT OpenDriveConfig NAMESPACE OpenDrive:: DESTINATION cmake)
