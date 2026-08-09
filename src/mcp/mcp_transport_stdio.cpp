#include "src/mcp/mcp_transport_stdio.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <cstdio>
#include <iostream>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include "src/mcp/mcp_protocol.h"

namespace geoviewer::mcp {

StdioTransport::StdioTransport(McpProtocolHandler* handler, QObject* parent)
    : QObject(parent), handler_(handler) {}

StdioTransport::~StdioTransport() { Stop(); }

void StdioTransport::Start() {
  if (is_running_) return;

#ifndef _WIN32
  notifier_ = new QSocketNotifier(STDIN_FILENO, QSocketNotifier::Read, this);
  connect(notifier_, &QSocketNotifier::activated, this,
          &StdioTransport::OnStdinReady);
  notifier_->setEnabled(true);
#endif

  is_running_ = true;
}

void StdioTransport::Stop() {
  if (!is_running_) return;
  if (notifier_) {
    notifier_->setEnabled(false);
    notifier_->deleteLater();
    notifier_ = nullptr;
  }
  is_running_ = false;
}

void StdioTransport::OnStdinReady() {
  std::string line;
  if (std::getline(std::cin, line)) {
    if (line.empty()) return;

    QJsonParseError parse_error;
    QJsonDocument doc = QJsonDocument::fromJson(
        QByteArray::fromStdString(line), &parse_error);

    if (parse_error.error != QJsonParseError::NoError || !doc.isObject()) {
      QJsonObject err = McpProtocolHandler::CreateErrorResponse(
          QJsonValue(), JsonRpcErrorCode::kParseError, "Parse error");
      std::cout << QJsonDocument(err).toJson(QJsonDocument::Compact).toStdString()
                << "\n"
                << std::flush;
      return;
    }

    QJsonObject response = handler_->HandleMessage(doc.object());
    if (!response.isEmpty()) {
      std::cout << QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString()
                << "\n"
                << std::flush;
    }
  }
}

}  // namespace geoviewer::mcp
