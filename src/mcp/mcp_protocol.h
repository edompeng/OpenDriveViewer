#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "src/geo_viewer_export.h"

namespace geoviewer::mcp {

enum class JsonRpcErrorCode {
  kParseError = -32700,
  kInvalidRequest = -32600,
  kMethodNotFound = -32601,
  kInvalidParams = -32602,
  kInternalError = -32603,
};

struct ToolDefinition {
  std::string name;
  std::string description;
  QJsonObject input_schema;
  std::function<QJsonObject(const QJsonObject&)> handler;
};

class GEOVIEWER_EXPORT McpProtocolHandler : public QObject {
  Q_OBJECT

 public:
  explicit McpProtocolHandler(QObject* parent = nullptr);
  ~McpProtocolHandler() override = default;

  void RegisterTool(const ToolDefinition& tool);
  QJsonObject HandleMessage(const QJsonObject& message);

  static QJsonObject CreateErrorResponse(const QJsonValue& id,
                                         JsonRpcErrorCode code,
                                         const QString& message);
  static QJsonObject CreateSuccessResponse(const QJsonValue& id,
                                           const QJsonObject& result);

 private:
  QJsonObject HandleInitialize(const QJsonValue& id, const QJsonObject& params);
  QJsonObject HandleToolsList(const QJsonValue& id);
  QJsonObject HandleToolsCall(const QJsonValue& id, const QJsonObject& params);
  QJsonObject HandlePing(const QJsonValue& id);

  std::map<std::string, ToolDefinition> tools_;
  bool initialized_ = false;
};

}  // namespace geoviewer::mcp
