#include <gtest/gtest.h>

#include "src/core/app_version.h"

namespace geoviewer::core {

TEST(AppVersionTest, VersionIsAvailable) {
  EXPECT_FALSE(AppVersion::Current().isEmpty());
}

}  // namespace geoviewer::core
