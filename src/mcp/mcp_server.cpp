#include "src/mcp/mcp_server.h"

#include "src/mcp/mcp_bridge.h"
#include "src/mcp/mcp_protocol.h"
#include "src/mcp/mcp_tools_data.h"
#include "src/mcp/mcp_tools_ui.h"
#include "src/mcp/mcp_transport_http.h"
#include "src/mcp/mcp_transport_stdio.h"

namespace geoviewer::mcp {

McpServer::McpServer(GeoViewerWidget* viewer_widget,
                     std::function<void(const QString&)> load_map_fn,
                     QObject* parent)
    : QObject(parent),
      viewer_widget_(viewer_widget),
      load_map_fn_(load_map_fn) {
  protocol_handler_ = std::make_unique<McpProtocolHandler>(this);
  bridge_ = std::make_unique<McpBridge>(viewer_widget, load_map_fn, this);
  stdio_transport_ =
      std::make_unique<StdioTransport>(protocol_handler_.get(), this);
  http_transport_ =
      std::make_unique<HttpTransport>(protocol_handler_.get(), this);

  RegisterAllTools();
}

McpServer::~McpServer() { StopAll(); }

void McpServer::RegisterAllTools() {
  for (const auto& tool : CreateDataTools(bridge_.get())) {
    protocol_handler_->RegisterTool(tool);
  }
  for (const auto& tool : CreateUiTools(bridge_.get())) {
    protocol_handler_->RegisterTool(tool);
  }
}

void McpServer::StartStdio() { stdio_transport_->Start(); }

bool McpServer::StartHttp(uint16_t port) {
  return http_transport_->Start(port);
}

void McpServer::StopHttp() { http_transport_->Stop(); }

void McpServer::StopAll() {
  if (stdio_transport_) stdio_transport_->Stop();
  if (http_transport_) http_transport_->Stop();
}

bool McpServer::IsStdioRunning() const {
  return stdio_transport_ && stdio_transport_->IsRunning();
}

bool McpServer::IsHttpRunning() const {
  return http_transport_ && http_transport_->IsRunning();
}

uint16_t McpServer::HttpPort() const {
  return http_transport_ ? http_transport_->Port() : 0;
}

}  // namespace geoviewer::mcp
