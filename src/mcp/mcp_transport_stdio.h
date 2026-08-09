#pragma once

#include <QObject>
#include <QSocketNotifier>
#include <memory>

#include "src/geo_viewer_export.h"

namespace geoviewer::mcp {

class McpProtocolHandler;

class GEOVIEWER_EXPORT StdioTransport : public QObject {
  Q_OBJECT

 public:
  explicit StdioTransport(McpProtocolHandler* handler, QObject* parent = nullptr);
  ~StdioTransport() override;

  void Start();
  void Stop();
  bool IsRunning() const { return is_running_; }

 private slots:
  void OnStdinReady();

 private:
  McpProtocolHandler* handler_ = nullptr;
  QSocketNotifier* notifier_ = nullptr;
  bool is_running_ = false;
};

}  // namespace geoviewer::mcp
