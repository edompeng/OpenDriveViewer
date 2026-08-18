#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "src/mcp/mcp_protocol.h"
#include "src/mcp/mcp_protocol_version.h"

namespace geoviewer::mcp {

TEST(McpProtocolTest, InitializeAndPing) {
  int argc = 1;
  char app_name[] = "mcp_test";
  char* argv[] = {app_name, nullptr};
  QCoreApplication app(argc, argv);

  McpProtocolHandler handler;

  // Test initialize
  QJsonObject init_req;
  init_req["jsonrpc"] = "2.0";
  init_req["id"] = 1;
  init_req["method"] = "initialize";
  init_req["params"] = QJsonObject{{"protocolVersion", "2025-06-18"}};

  QJsonObject init_res = handler.HandleMessage(init_req);
  EXPECT_EQ(init_res["jsonrpc"].toString(), "2.0");
  EXPECT_EQ(init_res["id"].toInt(), 1);
  EXPECT_TRUE(init_res.contains("result"));

  QJsonObject result = init_res["result"].toObject();
  EXPECT_TRUE(result.contains("protocolVersion"));
  EXPECT_EQ(result["protocolVersion"].toString(), "2025-06-18");
  EXPECT_TRUE(result.contains("serverInfo"));
  EXPECT_FALSE(result["instructions"].toString().isEmpty());

  // Test ping
  QJsonObject ping_req;
  ping_req["jsonrpc"] = "2.0";
  ping_req["id"] = 2;
  ping_req["method"] = "ping";

  QJsonObject ping_res = handler.HandleMessage(ping_req);
  EXPECT_EQ(ping_res["id"].toInt(), 2);
  EXPECT_TRUE(ping_res.contains("result"));
}

TEST(McpProtocolTest, NegotiatesLegacyVersionsAndSupportsModernDiscovery) {
  McpProtocolHandler handler;

  QJsonObject init_req{
      {"jsonrpc", "2.0"},
      {"id", 1},
      {"method", "initialize"},
      {"params", QJsonObject{{"protocolVersion", "unsupported"}}}};
  const QJsonObject init_result =
      handler.HandleMessage(init_req)["result"].toObject();
  EXPECT_EQ(init_result["protocolVersion"].toString(), "2025-11-25");

  QJsonObject discover_req{{"jsonrpc", "2.0"},
                           {"id", "discover-1"},
                           {"method", "server/discover"},
                           {"params", QJsonObject()}};
  const QJsonObject discover_result =
      handler.HandleMessage(discover_req)["result"].toObject();
  EXPECT_EQ(discover_result["resultType"].toString(), "complete");
  EXPECT_TRUE(
      discover_result["supportedVersions"].toArray().contains("2026-07-28"));
  EXPECT_TRUE(discover_result["capabilities"].toObject().contains("tools"));
  EXPECT_TRUE(McpProtocolVersionPolicy::IsSupported("2025-03-26"));
}

TEST(McpProtocolTest, ToolRegistrationAndCall) {
  McpProtocolHandler handler;

  ToolDefinition tool;
  tool.name = "echo_tool";
  tool.description = "Echo back input";
  tool.input_schema = QJsonObject{{"type", "object"}};
  tool.handler = [](const QJsonObject& args) -> QJsonObject {
    QJsonObject content_item;
    content_item["type"] = "text";
    content_item["text"] = args.value("message").toString();

    QJsonObject res;
    res["content"] = QJsonArray{content_item};
    return res;
  };

  handler.RegisterTool(tool);

  // List tools
  QJsonObject list_req;
  list_req["jsonrpc"] = "2.0";
  list_req["id"] = 1;
  list_req["method"] = "tools/list";

  QJsonObject list_res = handler.HandleMessage(list_req);
  QJsonArray tools = list_res["result"].toObject()["tools"].toArray();
  EXPECT_EQ(tools.size(), 1);
  EXPECT_EQ(tools[0].toObject()["name"].toString(), "echo_tool");

  // Call tool
  QJsonObject call_req;
  call_req["jsonrpc"] = "2.0";
  call_req["id"] = 2;
  call_req["method"] = "tools/call";
  QJsonObject params;
  params["name"] = "echo_tool";
  params["arguments"] = QJsonObject{{"message", "hello world"}};
  call_req["params"] = params;

  QJsonObject call_res = handler.HandleMessage(call_req);
  EXPECT_TRUE(call_res.contains("result"));
  QJsonArray content = call_res["result"].toObject()["content"].toArray();
  EXPECT_EQ(content[0].toObject()["text"].toString(), "hello world");
}

TEST(McpProtocolTest, InvalidMethodAndErrorHandling) {
  McpProtocolHandler handler;

  QJsonObject req;
  req["jsonrpc"] = "2.0";
  req["id"] = 1;
  req["method"] = "non_existent_method";

  QJsonObject res = handler.HandleMessage(req);
  EXPECT_TRUE(res.contains("error"));
  EXPECT_EQ(res["error"].toObject()["code"].toInt(),
            static_cast<int>(JsonRpcErrorCode::kMethodNotFound));
}

}  // namespace geoviewer::mcp
