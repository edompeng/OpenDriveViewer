#pragma once

#include <QObject>
#include <QThread>
#include <atomic>

#include "src/geo_viewer_export.h"

namespace geoviewer::mcp {

class McpProtocolHandler;

class GEOVIEWER_EXPORT StdioThread : public QThread {
  Q_OBJECT

 public:
  explicit StdioThread(QObject* parent = nullptr) : QThread(parent) {}

  void Stop() {
    running_ = false;
    requestInterruption();
  }

 signals:
  void LineReceived(const QString& line);

 protected:
  void run() override;

 private:
  std::atomic<bool> running_{false};
};

class GEOVIEWER_EXPORT StdioTransport : public QObject {
  Q_OBJECT

 public:
  explicit StdioTransport(McpProtocolHandler* handler,
                          QObject* parent = nullptr);
  ~StdioTransport() override;

  void Start();
  void Stop();
  bool IsRunning() const { return is_running_; }

  static void RedirectStdoutToStderr();
  static void WriteJsonRpcMessage(const std::string& message);

 private slots:
  void OnLineReceived(const QString& line);

 private:
  McpProtocolHandler* handler_ = nullptr;
  StdioThread* reader_thread_ = nullptr;
  bool is_running_ = false;
};

}  // namespace geoviewer::mcp
