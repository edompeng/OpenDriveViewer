#include <gtest/gtest.h>
#include "src/core/coordinate_mode_policy.h"

TEST(CoordinateModePolicyTest, Wgs84AllowedOnlyWithValidGeoreference) {
  EXPECT_TRUE(IsWgs84ModeAllowed(true));
  EXPECT_FALSE(IsWgs84ModeAllowed(false));
}

TEST(CoordinateModePolicyTest, DefaultModeFollowsGeoreferenceValidity) {
  EXPECT_EQ(ResolveDefaultCoordinateMode(true), CoordinateMode::kWGS84);
  EXPECT_EQ(ResolveDefaultCoordinateMode(false), CoordinateMode::kLocal);
}

