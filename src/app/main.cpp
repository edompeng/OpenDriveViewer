#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDebug>
#include <QDir>
#include <QLocale>

#include <iostream>

#include "src/core/crash_handler.h"
#include "src/core/thread_pool.h"
#include "src/mcp/mcp_transport_stdio.h"
#include "src/ui/main_window.h"

// Redirect Qt logs to stderr to preserve clean stdout for stdio MCP
void CustomStderrMessageHandler(QtMsgType type,
                                const QMessageLogContext& context,
                                const QString& msg) {
  Q_UNUSED(type);
  Q_UNUSED(context);
  std::cerr << "[QtLog] " << msg.toStdString() << "\n" << std::flush;
}

int main(int argc, char** argv) {
  // Immediately redirect stdout to stderr if --mcp-stdio is set in command
  // line, ensuring stdout is reserved strictly for clean MCP JSON-RPC messages.
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--mcp-stdio") {
      geoviewer::mcp::StdioTransport::RedirectStdoutToStderr();
      break;
    }
  }

  Q_INIT_RESOURCE(OpenDriveViewer_translations);
  QApplication app(argc, argv);

  // Parse command line arguments
  QCommandLineParser parser;
  parser.setApplicationDescription(
      "GeoViewer - 3D Geospatial OpenDRIVE Viewer");
  parser.addHelpOption();

  QCommandLineOption mcp_stdio_opt("mcp-stdio",
                                   "Enable MCP Server in stdio transport mode");
  QCommandLineOption mcp_http_opt(
      "mcp-http",
      "Enable MCP Server in HTTP transport mode (default port 8080)", "port",
      "8080");

  parser.addOption(mcp_stdio_opt);
  parser.addOption(mcp_http_opt);
  parser.process(app);

  if (parser.isSet(mcp_stdio_opt)) {
    qInstallMessageHandler(CustomStderrMessageHandler);
  }

  // Initialize crash handler
  QString crash_dir =
      app.applicationDirPath() + QDir::separator() + "crash_reports";
  geoviewer::core::CrashHandler::Initialize(crash_dir.toStdString());

  QSurfaceFormat format;
  format.setDepthBufferSize(24);
  format.setStencilBufferSize(8);
  format.setVersion(3, 3);  // Requires OpenGL 3.3
  format.setProfile(QSurfaceFormat::CoreProfile);
  QSurfaceFormat::setDefaultFormat(format);

  MainWindow w;
  w.resize(1465, 880);
  w.show();

  if (parser.isSet(mcp_stdio_opt)) {
    w.StartMcpStdio();
  } else if (parser.isSet(mcp_http_opt)) {
    bool ok = false;
    uint16_t port = parser.value(mcp_http_opt).toUShort(&ok);
    if (!ok || port == 0) port = 8080;
    w.StartMcpHttp(port);
  }

  int exit_code = app.exec();
  geoviewer::utility::ThreadPool::Instance().Shutdown();
  return exit_code;
}
