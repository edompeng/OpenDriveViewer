#include <gtest/gtest.h>

#include <QByteArray>

#include "src/core/app_version.h"

namespace geoviewer::core {

TEST(AppVersionTest, VersionIsAvailable) {
  EXPECT_FALSE(AppVersion::Current().isEmpty());
}

TEST(AppVersionTest, MatchesCiVersionWhenProvided) {
  const QByteArray expected_version = qgetenv("GEOVIEWER_VERSION");
  if (expected_version.isEmpty()) GTEST_SKIP();

  EXPECT_EQ(AppVersion::Current(), QString::fromUtf8(expected_version));
}

}  // namespace geoviewer::core
