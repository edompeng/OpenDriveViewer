#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QLocale>
#include <QTranslator>
#include "src/core/crash_handler.h"
#include "src/ui/main_window.h"

int main(int argc, char **argv) {
  Q_INIT_RESOURCE(OpenDriveViewer_translations);
  QApplication app(argc, argv);

  // Initialize crash handler
  QString crash_dir = app.applicationDirPath() + QDir::separator() + "crash_reports";
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
  return app.exec();
}
