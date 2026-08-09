#pragma once

#include <QByteArray>
#include <QMap>
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <cstdint>
#include <memory>

#include "src/geo_viewer_export.h"

namespace geoviewer::mcp {

class McpProtocolHandler;

class GEOVIEWER_EXPORT HttpTransport : public QObject {
  Q_OBJECT

 public:
  explicit HttpTransport(McpProtocolHandler* handler, QObject* parent = nullptr);
  ~HttpTransport() override;

  bool Start(uint16_t port = 8080);
  void Stop();
  bool IsRunning() const { return is_running_; }
  uint16_t Port() const { return port_; }

 private slots:
  void OnNewConnection();
  void OnReadyRead();
  void OnSocketDisconnected();

 private:
  void ProcessSocketBuffer(QTcpSocket* socket);
  void HandleHttpRequest(QTcpSocket* socket, const QByteArray& request_data);

  McpProtocolHandler* handler_ = nullptr;
  QTcpServer* server_ = nullptr;
  bool is_running_ = false;
  uint16_t port_ = 8080;
  QMap<QTcpSocket*, QByteArray> socket_buffers_;
};

}  // namespace geoviewer::mcp
