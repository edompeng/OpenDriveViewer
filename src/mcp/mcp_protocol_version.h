#pragma once

#include <QString>
#include <QStringList>

#include "src/geo_viewer_export.h"

namespace geoviewer::mcp {

class GEOVIEWER_EXPORT McpProtocolVersionPolicy {
 public:
  static QStringList SupportedVersions();
  static QString NegotiateLegacyVersion(const QString& requested_version);
  static bool IsSupported(const QString& version);
  static bool IsModern(const QString& version);
};

}  // namespace geoviewer::mcp
