#include <QApplication>
#include <QDebug>
#include <QLocale>
#include <QTranslator>
#include "src/app/main_window.h"

int main(int argc, char **argv) {
  QApplication app(argc, argv);

  // Set default language to Simplified Chinese if not specified
  QString localeName = QLocale::system().name();
  if (localeName.isEmpty()) {
    localeName = "zh_CN";
  }

  QTranslator translator;
  QString systemLocale = ":/i18n/geoviewer_" + localeName;
  if (translator.load(systemLocale)) {
    app.installTranslator(&translator);
    qDebug() << "Initial translation loaded:" << systemLocale;
  } else {
    qDebug() << "Failed to load initial translation:" << systemLocale;
    if (translator.load(":/i18n/geoviewer_zh_CN")) {
      app.installTranslator(&translator);
      qDebug() << "Loaded fallback translation: :/i18n/geoviewer_zh_CN";
    } else {
      qDebug() << "Failed to load fallback translation!";
    }
  }

  QSurfaceFormat format;
  format.setDepthBufferSize(24);
  format.setStencilBufferSize(8);
  format.setVersion(3, 3);  // Requires OpenGL 3.3
  format.setProfile(QSurfaceFormat::CoreProfile);
  QSurfaceFormat::setDefaultFormat(format);

  MainWindow w;
  w.resize(1280, 800);
  w.show();
  return app.exec();
}
