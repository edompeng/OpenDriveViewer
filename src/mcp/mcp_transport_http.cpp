#include "src/mcp/mcp_transport_http.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

#include "src/core/app_version.h"
#include "src/mcp/mcp_protocol.h"
#include "src/mcp/mcp_protocol_version.h"

namespace geoviewer::mcp {

namespace {

QMap<QByteArray, QByteArray> ParseHeaders(const QByteArray& header_part) {
  QMap<QByteArray, QByteArray> headers;
  const QList<QByteArray> lines = header_part.split('\n');
  for (int i = 1; i < lines.size(); ++i) {
    const QByteArray line = lines.at(i).trimmed();
    const int separator = line.indexOf(':');
    if (separator <= 0) continue;
    headers.insert(line.left(separator).trimmed().toLower(),
                   line.mid(separator + 1).trimmed());
  }
  return headers;
}

bool IsAllowedOrigin(const QByteArray& origin_header) {
  if (origin_header.isEmpty()) return true;
  const QUrl origin(QString::fromUtf8(origin_header));
  const QString host = origin.host().toLower();
  return origin.isValid() &&
         (origin.scheme() == "http" || origin.scheme() == "https") &&
         (host == "localhost" || host == "127.0.0.1" || host == "::1");
}

}  // namespace

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

  if (!server_->listen(QHostAddress::LocalHost, port_)) {
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
  QString path = tokens[1].section('?', 0, 0);
  const QMap<QByteArray, QByteArray> headers = ParseHeaders(header_part);

  if (!IsAllowedOrigin(headers.value("origin"))) {
    socket->write(
        "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n"
        "Connection: close\r\n\r\n");
    socket->disconnectFromHost();
    return;
  }

  const QByteArray origin = headers.value("origin");
  const QByteArray cors_header =
      origin.isEmpty() ? QByteArray()
                       : "Access-Control-Allow-Origin: " + origin + "\r\n";

  // CORS Options Preflight
  if (method == "OPTIONS") {
    QByteArray response = "HTTP/1.1 204 No Content\r\n" + cors_header +
                          "Access-Control-Allow-Methods: POST, GET, OPTIONS\r\n"
                          "Access-Control-Allow-Headers: Content-Type, Accept, "
                          "MCP-Protocol-Version, Mcp-Session-Id, Mcp-Method, "
                          "Mcp-Name\r\n"
                          "Connection: close\r\n\r\n";
    socket->write(response);
    socket->disconnectFromHost();
    return;
  }

  if (method == "GET" && (path == "/" || path == "/health")) {
    QJsonObject info;
    info["status"] = "ok";
    info["server"] = "GeoViewer MCP Server";
    info["version"] = geoviewer::core::AppVersion::Current();
    info["transport"] = "streamable-http";
    info["endpoint"] = "/mcp";
    QJsonArray protocol_versions;
    for (const QString& version :
         McpProtocolVersionPolicy::SupportedVersions()) {
      protocol_versions.append(version);
    }
    info["protocolVersions"] = protocol_versions;
    QByteArray body = QJsonDocument(info).toJson(QJsonDocument::Compact);

    QByteArray response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: " +
        QByteArray::number(body.size()) + "\r\n" + cors_header +
        "Connection: close\r\n\r\n" + body;
    socket->write(response);
    socket->disconnectFromHost();
    return;
  }

  if (method == "POST" && path == "/mcp") {
    QJsonParseError parse_error;
    QJsonDocument doc = QJsonDocument::fromJson(body_part, &parse_error);

    QJsonObject json_response;
    bool is_notification = false;
    if (parse_error.error != QJsonParseError::NoError || !doc.isObject()) {
      json_response = McpProtocolHandler::CreateErrorResponse(
          QJsonValue(), JsonRpcErrorCode::kParseError, "Parse error");
    } else {
      const QJsonObject request = doc.object();
      is_notification = !request.contains("id") ||
                        request.value("id").isNull() ||
                        request.value("id").isUndefined();
      json_response = handler_->HandleMessage(request);
    }

    if (is_notification) {
      QByteArray http_response =
          "HTTP/1.1 202 Accepted\r\n"
          "Content-Length: 0\r\n" +
          cors_header + "Connection: close\r\n\r\n";
      socket->write(http_response);
      socket->disconnectFromHost();
      return;
    }

    QByteArray response_body =
        QJsonDocument(json_response).toJson(QJsonDocument::Compact);
    QByteArray protocol_header;
    if (json_response.value("result").isObject()) {
      const QString negotiated_version = json_response.value("result")
                                             .toObject()
                                             .value("protocolVersion")
                                             .toString();
      if (!negotiated_version.isEmpty()) {
        protocol_header =
            "MCP-Protocol-Version: " + negotiated_version.toUtf8() + "\r\n";
      }
    }

    QByteArray http_response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: " +
        QByteArray::number(response_body.size()) + "\r\n" + cors_header +
        protocol_header + "Connection: close\r\n\r\n" + response_body;

    socket->write(http_response);
    socket->disconnectFromHost();
  } else {
    QByteArray status =
        path == "/mcp" ? "405 Method Not Allowed" : "404 Not Found";
    QByteArray allow_header = path == "/mcp" ? "Allow: POST, OPTIONS\r\n" : "";
    QByteArray response = "HTTP/1.1 " + status + "\r\n" + allow_header +
                          "Connection: close\r\n\r\n";
    socket->write(response);
    socket->disconnectFromHost();
  }
}

}  // namespace geoviewer::mcp
