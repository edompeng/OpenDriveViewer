#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>

#include "src/mcp/mcp_bridge.h"
#include "src/mcp/mcp_protocol.h"
#include "src/mcp/mcp_tools_data.h"
#include "src/mcp/mcp_tools_ui.h"

namespace geoviewer::mcp {

TEST(McpToolsTest, DataAndUiToolsRegistration) {
  int argc = 1;
  char app_name[] = "mcp_test";
  char* argv[] = {app_name, nullptr};
  QCoreApplication app(argc, argv);

  McpBridge bridge(nullptr, nullptr);
  McpProtocolHandler handler;

  for (const auto& tool : CreateDataTools(&bridge)) {
    handler.RegisterTool(tool);
  }
  for (const auto& tool : CreateUiTools(&bridge)) {
    handler.RegisterTool(tool);
  }

  QJsonObject list_req;
  list_req["jsonrpc"] = "2.0";
  list_req["id"] = 1;
  list_req["method"] = "tools/list";

  QJsonObject res = handler.HandleMessage(list_req);
  EXPECT_TRUE(res.contains("result"));

  QJsonArray tools = res["result"].toObject()["tools"].toArray();
  EXPECT_GE(tools.size(), 20);

  bool has_get_map_info = false;
  bool has_set_camera = false;
  bool has_load_map = false;

  for (const auto& val : tools) {
    QString name = val.toObject()["name"].toString();
    if (name == "get_map_info") has_get_map_info = true;
    if (name == "set_camera") has_set_camera = true;
    if (name == "load_map") has_load_map = true;
  }

  EXPECT_TRUE(has_get_map_info);
  EXPECT_TRUE(has_set_camera);
  EXPECT_TRUE(has_load_map);
}

TEST(McpToolsTest, ToolExecutionCoordinateTransform) {
  int argc = 1;
  char app_name[] = "mcp_test";
  char* argv[] = {app_name, nullptr};
  QCoreApplication app(argc, argv);

  McpBridge bridge(nullptr, nullptr);
  McpProtocolHandler handler;

  for (const auto& tool : CreateDataTools(&bridge)) {
    handler.RegisterTool(tool);
  }

  QJsonObject call_req;
  call_req["jsonrpc"] = "2.0";
  call_req["id"] = 100;
  call_req["method"] = "tools/call";
  QJsonObject params;
  params["name"] = "coordinate_transform";
  params["arguments"] = QJsonObject{
      {"from", "local"},
      {"to", "wgs84"},
      {"x", 0.0},
      {"y", 0.0},
      {"z", 0.0},
  };
  call_req["params"] = params;

  QJsonObject call_res = handler.HandleMessage(call_req);
  EXPECT_EQ(call_res["id"].toInt(), 100);
  EXPECT_TRUE(call_res.contains("result"));
  QJsonObject result = call_res["result"].toObject();
  EXPECT_TRUE(result.contains("lon") || result.contains("x") ||
              result.contains("error"));
}

}  // namespace geoviewer::mcp
