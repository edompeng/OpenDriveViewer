#include "src/mcp/mcp_tools_data.h"

#include <QJsonObject>
#include "src/mcp/mcp_bridge.h"

namespace geoviewer::mcp {

std::vector<ToolDefinition> CreateDataTools(McpBridge* bridge) {
  std::vector<ToolDefinition> tools;

  // 1. get_map_info
  tools.push_back({
      "get_map_info",
      "Get overview information about currently loaded OpenDRIVE map",
      QJsonObject{
          {"type", "object"},
          {"properties", QJsonObject{}},
      },
      [bridge](const QJsonObject& args) { return bridge->GetMapInfo(args); },
  });

  // 2. get_roads
  tools.push_back({
      "get_roads",
      "Get list of roads with summary attributes (id, length, junction, etc.)",
      QJsonObject{
          {"type", "object"},
          {"properties",
           QJsonObject{
               {"offset",
                QJsonObject{{"type", "integer"},
                            {"description", "Start index for pagination"}}},
               {"limit",
                QJsonObject{{"type", "integer"},
                            {"description", "Max items to return"}}},
           }},
      },
      [bridge](const QJsonObject& args) { return bridge->GetRoads(args); },
  });

  // 3. get_road_detail
  tools.push_back({
      "get_road_detail",
      "Get detailed structure of a specific road including lane sections, "
      "signals, and objects",
      QJsonObject{
          {"type", "object"},
          {"properties",
           QJsonObject{
               {"road_id",
                QJsonObject{{"type", "string"},
                            {"description", "Target road ID"}}},
           }},
          {"required", QJsonArray{"road_id"}},
      },
      [bridge](const QJsonObject& args) { return bridge->GetRoadDetail(args); },
  });

  // 4. get_lane_geometry
  tools.push_back({
      "get_lane_geometry",
      "Sample discrete centerline vertices of a specified lane",
      QJsonObject{
          {"type", "object"},
          {"properties",
           QJsonObject{
               {"road_id", QJsonObject{{"type", "string"}}},
               {"lane_id", QJsonObject{{"type", "integer"}}},
               {"s_start", QJsonObject{{"type", "number"}}},
               {"s_end", QJsonObject{{"type", "number"}}},
               {"eps",
                QJsonObject{{"type", "number"},
                            {"description", "Sampling resolution step"}}},
           }},
          {"required", QJsonArray{"road_id", "lane_id"}},
      },
      [bridge](const QJsonObject& args) { return bridge->GetLaneGeometry(args); },
  });

  // 5. get_junctions
  tools.push_back({
      "get_junctions",
      "Get list of all junctions and connection topologies",
      QJsonObject{
          {"type", "object"},
          {"properties", QJsonObject{}},
      },
      [bridge](const QJsonObject& args) { return bridge->GetJunctions(args); },
  });

  // 6. get_signals
  tools.push_back({
      "get_signals",
      "Get all traffic signals, optionally filtered by road_id",
      QJsonObject{
          {"type", "object"},
          {"properties",
           QJsonObject{
               {"road_id",
                QJsonObject{{"type", "string"},
                            {"description", "Optional road ID filter"}}},
           }},
      },
      [bridge](const QJsonObject& args) { return bridge->GetSignals(args); },
  });

  // 7. get_objects
  tools.push_back({
      "get_objects",
      "Get all road objects (signs, obstacles, facilities), optionally "
      "filtered by road_id",
      QJsonObject{
          {"type", "object"},
          {"properties",
           QJsonObject{
               {"road_id",
                QJsonObject{{"type", "string"},
                            {"description", "Optional road ID filter"}}},
           }},
      },
      [bridge](const QJsonObject& args) { return bridge->GetObjects(args); },
  });

  // 8. query_point
  tools.push_back({
      "query_point",
      "Reverse map match: find closest road, s/t offset, and heading for a "
      "given 3D point",
      QJsonObject{
          {"type", "object"},
          {"properties",
           QJsonObject{
               {"x", QJsonObject{{"type", "number"}}},
               {"y", QJsonObject{{"type", "number"}}},
               {"z", QJsonObject{{"type", "number"}}},
               {"lon", QJsonObject{{"type", "number"}}},
               {"lat", QJsonObject{{"type", "number"}}},
               {"alt", QJsonObject{{"type", "number"}}},
           }},
      },
      [bridge](const QJsonObject& args) { return bridge->QueryPoint(args); },
  });

  // 9. coordinate_transform
  tools.push_back({
      "coordinate_transform",
      "Convert coordinates between local projection and WGS84 (lon/lat)",
      QJsonObject{
          {"type", "object"},
          {"properties",
           QJsonObject{
               {"from",
                QJsonObject{{"type", "string"},
                            {"description", "'wgs84' or 'local'"}}},
               {"to",
                QJsonObject{{"type", "string"},
                            {"description", "'local' or 'wgs84'"}}},
               {"x", QJsonObject{{"type", "number"}}},
               {"y", QJsonObject{{"type", "number"}}},
               {"z", QJsonObject{{"type", "number"}}},
               {"lon", QJsonObject{{"type", "number"}}},
               {"lat", QJsonObject{{"type", "number"}}},
               {"alt", QJsonObject{{"type", "number"}}},
           }},
          {"required", QJsonArray{"from", "to"}},
      },
      [bridge](const QJsonObject& args) {
        return bridge->CoordinateTransform(args);
      },
  });

  return tools;
}

}  // namespace geoviewer::mcp
