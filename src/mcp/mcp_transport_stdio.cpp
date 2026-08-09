#include "src/mcp/mcp_transport_stdio.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QThread>
#include <atomic>
#include <iostream>

#ifdef _WIN32
#  include <io.h>
#else
#  include <unistd.h>
#endif

#include "src/mcp/mcp_protocol.h"

namespace geoviewer::mcp {

void StdioThread::run() {
  running_ = true;
  std::string line;
  while (running_ && !isInterruptionRequested() &&
         std::getline(std::cin, line)) {
    if (!line.empty()) {
      emit LineReceived(QString::fromStdString(line));
    }
  }
}

static int g_real_stdout_fd = -1;

void StdioTransport::RedirectStdoutToStderr() {
  if (g_real_stdout_fd != -1) return;  // Already redirected

#ifdef _WIN32
  int orig_fd = _dup(_fileno(stdout));
  if (orig_fd != -1) {
    _dup2(_fileno(stderr), _fileno(stdout));
    g_real_stdout_fd = orig_fd;
  }
#else
  int orig_fd = dup(STDOUT_FILENO);
  if (orig_fd != -1) {
    dup2(STDERR_FILENO, STDOUT_FILENO);
    g_real_stdout_fd = orig_fd;
  }
#endif
}

void StdioTransport::WriteJsonRpcMessage(const std::string& message) {
  std::string output = message + "\n";
  if (g_real_stdout_fd != -1) {
#ifdef _WIN32
    _write(g_real_stdout_fd, output.c_str(),
           static_cast<unsigned int>(output.size()));
#else
    ssize_t ret = ::write(g_real_stdout_fd, output.c_str(), output.size());
    (void)ret;
#endif
  } else {
    std::cout << output << std::flush;
  }
}

StdioTransport::StdioTransport(McpProtocolHandler* handler, QObject* parent)
    : QObject(parent), handler_(handler) {}

StdioTransport::~StdioTransport() { Stop(); }

void StdioTransport::Start() {
  if (is_running_) return;

  reader_thread_ = new StdioThread();
  connect(reader_thread_, &StdioThread::LineReceived, this,
          &StdioTransport::OnLineReceived, Qt::QueuedConnection);
  reader_thread_->start();

  is_running_ = true;
}

void StdioTransport::Stop() {
  if (!is_running_) return;
  if (reader_thread_) {
    reader_thread_->Stop();
    reader_thread_->quit();
    if (!reader_thread_->wait(300)) {
      reader_thread_->terminate();
      reader_thread_->wait(300);
    }
    delete reader_thread_;
    reader_thread_ = nullptr;
  }
  is_running_ = false;
}

void StdioTransport::OnLineReceived(const QString& line) {
  QByteArray line_bytes = line.toUtf8();
  QJsonParseError parse_error;
  QJsonDocument doc = QJsonDocument::fromJson(line_bytes, &parse_error);

  if (parse_error.error != QJsonParseError::NoError || !doc.isObject()) {
    QJsonObject err = McpProtocolHandler::CreateErrorResponse(
        QJsonValue(), JsonRpcErrorCode::kParseError, "Parse error");
    WriteJsonRpcMessage(
        QJsonDocument(err).toJson(QJsonDocument::Compact).toStdString());
    return;
  }

  QJsonObject response = handler_->HandleMessage(doc.object());
  if (!response.isEmpty()) {
    WriteJsonRpcMessage(
        QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString());
  }
}

}  // namespace geoviewer::mcp
