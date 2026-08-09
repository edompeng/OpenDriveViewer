#pragma once

#include <QObject>
#include <cstdint>
#include <functional>
#include <memory>

#include "src/geo_viewer_export.h"

class GeoViewerWidget;

namespace geoviewer::mcp {

class McpProtocolHandler;
class McpBridge;
class StdioTransport;
class HttpTransport;

class GEOVIEWER_EXPORT McpServer : public QObject {
  Q_OBJECT

 public:
  explicit McpServer(
      GeoViewerWidget* viewer_widget,
      std::function<void(const QString&)> load_map_fn = nullptr,
      QObject* parent = nullptr);
  ~McpServer() override;

  void StartStdio();
  bool StartHttp(uint16_t port = 8080);
  void StopHttp();
  void StopAll();

  bool IsStdioRunning() const;
  bool IsHttpRunning() const;
  uint16_t HttpPort() const;

 private:
  void RegisterAllTools();

  GeoViewerWidget* viewer_widget_ = nullptr;
  std::function<void(const QString&)> load_map_fn_;

  std::unique_ptr<McpProtocolHandler> protocol_handler_;
  std::unique_ptr<McpBridge> bridge_;
  std::unique_ptr<StdioTransport> stdio_transport_;
  std::unique_ptr<HttpTransport> http_transport_;
};

}  // namespace geoviewer::mcp
