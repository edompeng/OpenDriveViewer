add_library(
  GeoViewerMcp ${LIB_TYPE}
  mcp_bridge.cpp
  mcp_protocol.cpp
  mcp_protocol_version.cpp
  mcp_server.cpp
  mcp_tools_data.cpp
  mcp_tools_ui.cpp
  mcp_transport_http.cpp
  mcp_transport_stdio.cpp)

target_include_directories(GeoViewerMcp PUBLIC ${CMAKE_SOURCE_DIR})

target_link_libraries(
  GeoViewerMcp
  PUBLIC GeoViewerCore GeoViewerLogic GeoViewerUIWidgets Qt6::Core Qt6::Gui
         Qt6::Network Qt6::Widgets OpenDrive)
