#include <catch2/catch_test_macros.hpp>

#include "src/utility/coordinate_mode_policy.h"

TEST_CASE("CoordinateModePolicy - WGS84 allowed only with valid georeference",
          "[coordinate-mode]") {
  CHECK(IsWgs84ModeAllowed(true));
  CHECK_FALSE(IsWgs84ModeAllowed(false));
}

TEST_CASE("CoordinateModePolicy - default mode follows georeference validity",
          "[coordinate-mode]") {
  CHECK(ResolveDefaultCoordinateMode(true) == CoordinateMode::kWGS84);
  CHECK(ResolveDefaultCoordinateMode(false) == CoordinateMode::kLocal);
}

