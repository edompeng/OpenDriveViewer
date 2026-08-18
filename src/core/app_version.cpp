#include "src/core/app_version.h"

#ifndef GEOVIEWER_VERSION
#  define GEOVIEWER_VERSION "0.0.0-dev"
#endif

namespace geoviewer::core {

QString AppVersion::Current() { return QStringLiteral(GEOVIEWER_VERSION); }

}  // namespace geoviewer::core
