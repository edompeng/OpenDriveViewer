#include <QApplication>
#include <QDebug>
#include <QLocale>
#include <QTranslator>
#include "src/ui/main_window.h"

int main(int argc, char **argv) {
  Q_INIT_RESOURCE(OpenDriveViewer_translations);
  QApplication app(argc, argv);

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
