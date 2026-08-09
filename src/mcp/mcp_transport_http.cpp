#include "src/mcp/mcp_transport_http.h"

#include <QJsonDocument>
#include <QJsonObject>
#include "src/mcp/mcp_protocol.h"

namespace geoviewer::mcp {

HttpTransport::HttpTransport(McpProtocolHandler* handler, QObject* parent)
    : QObject(parent), handler_(handler) {
  server_ = new QTcpServer(this);
  connect(server_, &QTcpServer::newConnection, this,
          &HttpTransport::OnNewConnection);
}

HttpTransport::~HttpTransport() { Stop(); }

bool HttpTransport::Start(uint16_t port) {
  if (is_running_) return true;
  port_ = port;

  if (!server_->listen(QHostAddress::Any, port_)) {
    return false;
  }

  is_running_ = true;
  return true;
}

void HttpTransport::Stop() {
  if (!is_running_) return;
  if (server_ && server_->isListening()) {
    server_->close();
  }
  socket_buffers_.clear();
  is_running_ = false;
}

void HttpTransport::OnNewConnection() {
  while (server_->hasPendingConnections()) {
    QTcpSocket* socket = server_->nextPendingConnection();
    socket_buffers_[socket] = QByteArray();
    connect(socket, &QTcpSocket::readyRead, this, &HttpTransport::OnReadyRead);
    connect(socket, &QTcpSocket::disconnected, this,
            &HttpTransport::OnSocketDisconnected);
  }
}

void HttpTransport::OnSocketDisconnected() {
  auto* socket = qobject_cast<QTcpSocket*>(sender());
  if (socket) {
    socket_buffers_.remove(socket);
    socket->deleteLater();
  }
}

void HttpTransport::OnReadyRead() {
  auto* socket = qobject_cast<QTcpSocket*>(sender());
  if (!socket) return;

  socket_buffers_[socket].append(socket->readAll());
  ProcessSocketBuffer(socket);
}

void HttpTransport::ProcessSocketBuffer(QTcpSocket* socket) {
  QByteArray& buffer = socket_buffers_[socket];

  while (!buffer.isEmpty()) {
    int header_end = buffer.indexOf("\r\n\r\n");
    if (header_end == -1) {
      break;  // Headers not fully received yet
    }

    QByteArray header_part = buffer.left(header_end);

    // Parse Content-Length header if present for POST requests
    int content_length = 0;
    for (const QByteArray& line : header_part.split('\n')) {
      QString line_str = QString::fromUtf8(line).trimmed();
      if (line_str.startsWith("Content-Length:", Qt::CaseInsensitive)) {
        content_length = line_str.section(':', 1).trimmed().toInt();
      }
    }

    int total_expected_len = header_end + 4 + content_length;
    if (buffer.size() < total_expected_len) {
      break;  // Full body not received yet
    }

    QByteArray full_request = buffer.left(total_expected_len);
    buffer.remove(0, total_expected_len);

    HandleHttpRequest(socket, full_request);
  }
}

void HttpTransport::HandleHttpRequest(QTcpSocket* socket,
                                       const QByteArray& request_data) {
  int header_end = request_data.indexOf("\r\n\r\n");
  if (header_end == -1) return;

  QByteArray header_part = request_data.left(header_end);
  QByteArray body_part = request_data.mid(header_end + 4);

  QString first_line =
      QString::fromUtf8(header_part.split('\n').first()).trimmed();
  QStringList tokens = first_line.split(' ');

  if (tokens.size() < 2) return;

  QString method = tokens[0];

  // CORS Options Preflight
  if (method == "OPTIONS") {
    QByteArray response =
        "HTTP/1.1 204 No Content\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: POST, GET, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Connection: close\r\n\r\n";
    socket->write(response);
    socket->disconnectFromHost();
    return;
  }

  if (method == "GET") {
    QJsonObject info;
    info["status"] = "ok";
    info["server"] = "GeoViewer MCP Server";
    info["version"] = "1.0.0";
    QByteArray body = QJsonDocument(info).toJson(QJsonDocument::Compact);

    QByteArray response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Content-Length: " +
        QByteArray::number(body.size()) +
        "\r\n"
        "Connection: close\r\n\r\n" +
        body;
    socket->write(response);
    socket->disconnectFromHost();
    return;
  }

  if (method == "POST") {
    QJsonParseError parse_error;
    QJsonDocument doc = QJsonDocument::fromJson(body_part, &parse_error);

    QJsonObject json_response;
    if (parse_error.error != QJsonParseError::NoError || !doc.isObject()) {
      json_response = McpProtocolHandler::CreateErrorResponse(
          QJsonValue(), JsonRpcErrorCode::kParseError, "Parse error");
    } else {
      json_response = handler_->HandleMessage(doc.object());
    }

    QByteArray response_body =
        QJsonDocument(json_response).toJson(QJsonDocument::Compact);

    QByteArray http_response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Content-Length: " +
        QByteArray::number(response_body.size()) +
        "\r\n"
        "Connection: close\r\n\r\n" +
        response_body;

    socket->write(http_response);
    socket->disconnectFromHost();
  } else {
    QByteArray response =
        "HTTP/1.1 405 Method Not Allowed\r\n"
        "Connection: close\r\n\r\n";
    socket->write(response);
    socket->disconnectFromHost();
  }
}

}  // namespace geoviewer::mcp
