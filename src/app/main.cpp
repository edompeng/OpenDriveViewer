#include <QApplication>
#include <QDebug>
#include <QLocale>
#include <QTranslator>
#include "src/ui/main_window.h"

int main(int argc, char **argv) {
  Q_INIT_RESOURCE(OpenDriveViewer_translations);
  QApplication app(argc, argv);

  // Set default language to Simplified Chinese if not specified
  QString locale_name = QLocale::system().name();
  if (locale_name.isEmpty()) {
    locale_name = "zh_CN";
  }

  QTranslator translator;
  QString system_locale = ":/i18n/geoviewer_" + locale_name;
  if (translator.load(system_locale)) {
    app.installTranslator(&translator);
    qDebug() << "Initial translation loaded:" << system_locale;
  } else {
    qDebug() << "Failed to load initial translation:" << system_locale;
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
  w.resize(1465, 880);
  w.show();
  return app.exec();
}
