#pragma once

#include <QString>

#include "src/geo_viewer_export.h"

namespace geoviewer::core {

class GEOVIEWER_EXPORT AppVersion {
 public:
  static QString Current();
};

}  // namespace geoviewer::core
