#include "src/mcp/mcp_tools_ui.h"

#include <QJsonObject>
#include "src/mcp/mcp_bridge.h"

namespace geoviewer::mcp {

std::vector<ToolDefinition> CreateUiTools(McpBridge* bridge) {
  std::vector<ToolDefinition> tools;

  // 1. set_camera
  tools.push_back({
      "set_camera",
      "Set viewer camera position, target look-at point, distance, yaw, and "
      "pitch angles",
      QJsonObject{
          {"type", "object"},
          {"properties",
           QJsonObject{
               {"target_x", QJsonObject{{"type", "number"}}},
               {"target_y", QJsonObject{{"type", "number"}}},
               {"target_z", QJsonObject{{"type", "number"}}},
               {"yaw",
                QJsonObject{
                    {"type", "number"},
                    {"description", "Yaw angle in degrees (e.g. 45.0)"}}},
               {"pitch",
                QJsonObject{
                    {"type", "number"},
                    {"description", "Pitch angle in degrees (e.g. -30.0)"}}},
               {"distance",
                QJsonObject{
                    {"type", "number"},
                    {"description", "Camera distance to target point"}}},
           }},
      },
      [bridge](const QJsonObject& args) { return bridge->SetCamera(args); },
  });

  // 2. jump_to_location
  tools.push_back({
      "jump_to_location",
      "Jump camera center to specified WGS84 (lon, lat, alt) or Local (x, y, z) "
      "location",
      QJsonObject{
          {"type", "object"},
          {"properties",
           QJsonObject{
               {"lon", QJsonObject{{"type", "number"}}},
               {"lat", QJsonObject{{"type", "number"}}},
               {"alt", QJsonObject{{"type", "number"}}},
               {"x", QJsonObject{{"type", "number"}}},
               {"y", QJsonObject{{"type", "number"}}},
               {"z", QJsonObject{{"type", "number"}}},
           }},
      },
      [bridge](const QJsonObject& args) {
        return bridge->JumpToLocation(args);
      },
  });

  // 3. set_view_mode
  tools.push_back({
      "set_view_mode",
      "Switch viewport projection between 2D top-down view and 3D perspective "
      "view",
      QJsonObject{
          {"type", "object"},
          {"properties",
           QJsonObject{
               {"mode",
                QJsonObject{
                    {"type", "string"},
                    {"description", "View mode: '2d' or '3d'"}}},
           }},
          {"required", QJsonArray{"mode"}},
      },
      [bridge](const QJsonObject& args) { return bridge->SetViewMode(args); },
  });

  // 4. add_user_points
  tools.push_back({
      "add_user_points",
      "Add custom 3D annotation points/markers with optional RGB colors",
      QJsonObject{
          {"type", "object"},
          {"properties",
           QJsonObject{
               {"points",
                QJsonObject{
                    {"type", "array"},
                    {"description",
                     "List of points with (lon, lat, alt) or (x, y, z) and "
                     "optional color [r, g, b]"}}},
           }},
          {"required", QJsonArray{"points"}},
      },
      [bridge](const QJsonObject& args) { return bridge->AddUserPoints(args); },
  });

  // 5. clear_user_points
  tools.push_back({
      "clear_user_points",
      "Clear all custom user annotation points from the scene",
      QJsonObject{
          {"type", "object"},
          {"properties", QJsonObject{}},
      },
      [bridge](const QJsonObject& args) {
        return bridge->ClearUserPoints(args);
      },
  });

  // 6. add_routing_path
  tools.push_back({
      "add_routing_path",
      "Calculate and render a path route between start and destination coordinates",
      QJsonObject{
          {"type", "object"},
          {"properties",
           QJsonObject{
               {"start_lon", QJsonObject{{"type", "number"}}},
               {"start_lat", QJsonObject{{"type", "number"}}},
               {"end_lon", QJsonObject{{"type", "number"}}},
               {"end_lat", QJsonObject{{"type", "number"}}},
               {"start_x", QJsonObject{{"type", "number"}}},
               {"start_y", QJsonObject{{"type", "number"}}},
               {"end_x", QJsonObject{{"type", "number"}}},
               {"end_y", QJsonObject{{"type", "number"}}},
               {"name",
                QJsonObject{{"type", "string"},
                            {"description", "Name label for route"}}},
           }},
      },
      [bridge](const QJsonObject& args) {
        return bridge->AddRoutingPath(args);
      },
  });

  // 7. clear_routing_paths
  tools.push_back({
      "clear_routing_paths",
      "Clear all rendered route paths from the scene",
      QJsonObject{
          {"type", "object"},
          {"properties", QJsonObject{}},
      },
      [bridge](const QJsonObject& args) {
        return bridge->ClearRoutingPaths(args);
      },
  });

  // 8. highlight_element
  tools.push_back({
      "highlight_element",
      "Highlight a specific map element (lane, roadmark, object, signal, or junction)",
      QJsonObject{
          {"type", "object"},
          {"properties",
           QJsonObject{
               {"road_id", QJsonObject{{"type", "string"}}},
               {"type",
                QJsonObject{
                    {"type", "string"},
                    {"description",
                     "Element type: 'lane', 'roadmark', 'object', 'signal', or "
                     "'junction'"}}},
               {"element_id", QJsonObject{{"type", "string"}}},
           }},
          {"required", QJsonArray{"type", "element_id"}},
      },
      [bridge](const QJsonObject& args) {
        return bridge->HighlightElement(args);
      },
  });

  // 9. set_layer_visibility
  tools.push_back({
      "set_layer_visibility",
      "Toggle visibility of rendering layers (lanes, roadmarks, objects, signals, junctions)",
      QJsonObject{
          {"type", "object"},
          {"properties",
           QJsonObject{
               {"layer",
                QJsonObject{
                    {"type", "string"},
                    {"description",
                     "Layer name: 'lanes', 'roadmarks', 'objects', 'signals', "
                     "or 'junctions'"}}},
               {"visible", QJsonObject{{"type", "boolean"}}},
           }},
          {"required", QJsonArray{"layer", "visible"}},
      },
      [bridge](const QJsonObject& args) {
        return bridge->SetLayerVisibility(args);
      },
  });

  // 10. load_map
  tools.push_back({
      "load_map",
      "Load a new OpenDRIVE map file (.xodr / .xml)",
      QJsonObject{
          {"type", "object"},
          {"properties",
           QJsonObject{
               {"path",
                QJsonObject{
                    {"type", "string"},
                    {"description", "Absolute path to .xodr or .xml file"}}},
           }},
          {"required", QJsonArray{"path"}},
      },
      [bridge](const QJsonObject& args) { return bridge->LoadMap(args); },
  });

  // 11. take_screenshot
  tools.push_back({
      "take_screenshot",
      "Capture current 3D viewport rendering as PNG image",
      QJsonObject{
          {"type", "object"},
          {"properties",
           QJsonObject{
               {"output_path",
                QJsonObject{
                    {"type", "string"},
                    {"description",
                     "Optional file path to save screenshot PNG"}}},
           }},
      },
      [bridge](const QJsonObject& args) {
        return bridge->TakeScreenshot(args);
      },
  });

  return tools;
}

}  // namespace geoviewer::mcp
