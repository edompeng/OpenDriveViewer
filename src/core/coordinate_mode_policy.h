#pragma once

#include "src/core/scene_enums.h"

inline bool IsWgs84ModeAllowed(bool georeference_valid) {
  return georeference_valid;
}

inline CoordinateMode ResolveDefaultCoordinateMode(bool georeference_valid) {
  return georeference_valid ? CoordinateMode::kWGS84 : CoordinateMode::kLocal;
}

