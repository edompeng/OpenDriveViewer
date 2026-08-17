#include "src/mcp/mcp_protocol.h"

#include <QJsonDocument>
#include <QString>

namespace geoviewer::mcp {

McpProtocolHandler::McpProtocolHandler(QObject* parent) : QObject(parent) {}

void McpProtocolHandler::RegisterTool(const ToolDefinition& tool) {
  tools_[tool.name] = tool;
}

QJsonObject McpProtocolHandler::CreateErrorResponse(const QJsonValue& id,
                                                    JsonRpcErrorCode code,
                                                    const QString& message) {
  QJsonObject error_obj;
  error_obj["code"] = static_cast<int>(code);
  error_obj["message"] = message;

  QJsonObject response;
  response["jsonrpc"] = "2.0";
  response["id"] = id;
  response["error"] = error_obj;
  return response;
}

QJsonObject McpProtocolHandler::CreateSuccessResponse(
    const QJsonValue& id, const QJsonObject& result) {
  QJsonObject response;
  response["jsonrpc"] = "2.0";
  response["id"] = id;
  response["result"] = result;
  return response;
}

QJsonObject McpProtocolHandler::HandleMessage(const QJsonObject& message) {
  if (message.value("jsonrpc").toString() != "2.0") {
    return CreateErrorResponse(message.value("id"),
                               JsonRpcErrorCode::kInvalidRequest,
                               "Invalid JSON-RPC version");
  }

  const QString method = message.value("method").toString();
  const QJsonValue id = message.value("id");
  const QJsonObject params = message.value("params").toObject();

  // Notification handling (requests without id)
  if (id.isUndefined() || id.isNull()) {
    if (method == "notifications/initialized") {
      initialized_ = true;
    }
    return QJsonObject();  // No response for notifications
  }

  if (method == "initialize") {
    return HandleInitialize(id, params);
  } else if (method == "ping") {
    return HandlePing(id);
  } else if (method == "tools/list") {
    return HandleToolsList(id);
  } else if (method == "tools/call") {
    return HandleToolsCall(id, params);
  }

  return CreateErrorResponse(id, JsonRpcErrorCode::kMethodNotFound,
                             "Method not found: " + method);
}

QJsonObject McpProtocolHandler::HandleInitialize(const QJsonValue& id,
                                                 const QJsonObject& params) {
  Q_UNUSED(params);
  QJsonObject capabilities;
  QJsonObject tools_cap;
  tools_cap["listChanged"] = false;
  capabilities["tools"] = tools_cap;

  QJsonObject server_info;
  server_info["name"] = "GeoViewer MCP Server";
  server_info["version"] = "1.0.0";

  QJsonObject result;
  result["protocolVersion"] = "2024-11-05";
  result["capabilities"] = capabilities;
  result["serverInfo"] = server_info;
  result["instructions"] =
      "Use GeoViewer tools to inspect OpenDRIVE map data and control the "
      "running viewer. Call get_map_info before map-dependent tools when the "
      "current map state is unknown.";

  initialized_ = true;
  return CreateSuccessResponse(id, result);
}

QJsonObject McpProtocolHandler::HandlePing(const QJsonValue& id) {
  return CreateSuccessResponse(id, QJsonObject());
}

QJsonObject McpProtocolHandler::HandleToolsList(const QJsonValue& id) {
  QJsonArray tools_array;
  for (const auto& [name, tool] : tools_) {
    QJsonObject tool_obj;
    tool_obj["name"] = QString::fromStdString(tool.name);
    tool_obj["description"] = QString::fromStdString(tool.description);
    tool_obj["inputSchema"] = tool.input_schema;
    tools_array.append(tool_obj);
  }

  QJsonObject result;
  result["tools"] = tools_array;
  return CreateSuccessResponse(id, result);
}

QJsonObject McpProtocolHandler::HandleToolsCall(const QJsonValue& id,
                                                const QJsonObject& params) {
  const std::string name = params.value("name").toString().toStdString();
  const QJsonObject arguments = params.value("arguments").toObject();

  auto it = tools_.find(name);
  if (it == tools_.end()) {
    return CreateErrorResponse(id, JsonRpcErrorCode::kInvalidParams,
                               "Unknown tool: " + QString::fromStdString(name));
  }

  try {
    QJsonObject raw_result = it->second.handler(arguments);
    QJsonObject mcp_result;

    if (raw_result.contains("content") &&
        raw_result.value("content").isArray()) {
      mcp_result = raw_result;
    } else {
      bool is_error = raw_result.contains("error");
      QJsonObject content_item;
      if (is_error) {
        content_item["type"] = "text";
        content_item["text"] = raw_result.value("error").toString();
      } else {
        content_item["type"] = "text";
        content_item["text"] = QString::fromUtf8(
            QJsonDocument(raw_result).toJson(QJsonDocument::Compact));
      }

      QJsonArray content_array;
      content_array.append(content_item);

      mcp_result["content"] = content_array;
      mcp_result["isError"] = is_error;
    }

    return CreateSuccessResponse(id, mcp_result);
  } catch (const std::exception& e) {
    return CreateErrorResponse(
        id, JsonRpcErrorCode::kInternalError,
        QString("Tool execution error: %1").arg(e.what()));
  } catch (...) {
    return CreateErrorResponse(id, JsonRpcErrorCode::kInternalError,
                               "Tool execution error: unknown exception");
  }
}

}  // namespace geoviewer::mcp
