#include "src/core/coordinate_util.h"
#include <gtest/gtest.h>

TEST(CoordinateUtilTest, ConversionCycle) {
  // Typical georeference string
  std::string proj_str =
      "+proj=utm +zone=32 +ellps=WGS84 +datum=WGS84 +units=m +no_defs";
  double offset_x = 1000.0;
  double offset_y = 2000.0;

  CoordinateUtil::Instance().Init(proj_str, offset_x, offset_y);

  // WGS84 coordinates: lon=9.0, lat=48.0, alt=100.0
  double x = 9.0;
  double y = 48.0;
  double z = 100.0;

  double original_lon = x;
  double original_lat = y;
  double original_alt = z;

  // Convert to local Cartesian
  CoordinateUtil::Instance().WGS84ToLocal(&x, &y, &z);

  // Convert back to WGS84
  CoordinateUtil::Instance().LocalToWGS84(&x, &y, &z);

  // Verify that the conversion cycle returns coordinates very close to original
  EXPECT_NEAR(x, original_lon, 1e-7);
  EXPECT_NEAR(y, original_lat, 1e-7);
  EXPECT_NEAR(z, original_alt, 1e-7);
}
