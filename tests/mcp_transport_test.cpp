#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <QTimer>

#include "src/mcp/mcp_protocol.h"
#include "src/mcp/mcp_transport_http.h"

namespace geoviewer::mcp {

TEST(McpTransportTest, HttpTransportPostRequest) {
  int argc = 1;
  char app_name[] = "mcp_test";
  char* argv[] = {app_name, nullptr};
  QCoreApplication app(argc, argv);

  McpProtocolHandler handler;
  HttpTransport transport(&handler);

  uint16_t test_port = 18080;
  ASSERT_TRUE(transport.Start(test_port));
  EXPECT_TRUE(transport.IsRunning());

  QTcpSocket socket;
  socket.connectToHost("127.0.0.1", test_port);
  ASSERT_TRUE(socket.waitForConnected(2000));

  QJsonObject req;
  req["jsonrpc"] = "2.0";
  req["id"] = 10;
  req["method"] = "ping";

  QByteArray body = QJsonDocument(req).toJson(QJsonDocument::Compact);
  QByteArray http_req =
      "POST /mcp HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Content-Type: application/json\r\n"
      "Content-Length: " +
      QByteArray::number(body.size()) +
      "\r\n"
      "\r\n" +
      body;

  socket.write(http_req);
  socket.flush();

  QEventLoop loop;
  QObject::connect(&socket, &QTcpSocket::readyRead, &loop, &QEventLoop::quit);
  QTimer::singleShot(2000, &loop, &QEventLoop::quit);
  loop.exec();

  QByteArray response_data = socket.readAll();
  EXPECT_TRUE(response_data.contains("HTTP/1.1 200 OK"));
  EXPECT_TRUE(response_data.contains("\"id\":10"));

  transport.Stop();
  EXPECT_FALSE(transport.IsRunning());
}

TEST(McpTransportTest, HttpTransportOptionsPreflight) {
  int argc = 1;
  char app_name[] = "mcp_test";
  char* argv[] = {app_name, nullptr};
  QCoreApplication app(argc, argv);

  McpProtocolHandler handler;
  HttpTransport transport(&handler);

  uint16_t test_port = 18081;
  ASSERT_TRUE(transport.Start(test_port));

  QTcpSocket socket;
  socket.connectToHost("127.0.0.1", test_port);
  ASSERT_TRUE(socket.waitForConnected(2000));

  QByteArray http_req =
      "OPTIONS /mcp HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Access-Control-Request-Method: POST\r\n"
      "\r\n";

  socket.write(http_req);
  socket.flush();

  QEventLoop loop;
  QObject::connect(&socket, &QTcpSocket::readyRead, &loop, &QEventLoop::quit);
  QTimer::singleShot(2000, &loop, &QEventLoop::quit);
  loop.exec();

  QByteArray response_data = socket.readAll();
  EXPECT_TRUE(response_data.contains("HTTP/1.1 204 No Content"));
  EXPECT_TRUE(response_data.contains("Access-Control-Allow-Origin: *"));

  transport.Stop();
}

}  // namespace geoviewer::mcp
