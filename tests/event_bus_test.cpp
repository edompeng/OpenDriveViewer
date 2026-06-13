#include <gtest/gtest.h>
#include <QCoreApplication>
#include "src/logic/event_bus.h"

TEST(EventBusTest, EmitAndReceive) {
  int argc = 0;
  char* argv[] = {nullptr};
  QCoreApplication app(argc, argv);

  using namespace geoviewer::logic;
  bool map_loaded_called = false;

  auto conn = QObject::connect(
      &EventBus::Instance(), &EventBus::MapLoaded,
      [&](const QString& path, bool success) {
        EXPECT_EQ(path, "test_path.xodr");
        EXPECT_TRUE(success);
        map_loaded_called = true;
      });

  emit EventBus::Instance().MapLoaded("test_path.xodr", true);
  EXPECT_TRUE(map_loaded_called);

  QObject::disconnect(conn);
}

TEST(EventBusTest, SingletonIdentity) {
  using namespace geoviewer::logic;
  EXPECT_EQ(&EventBus::Instance(), &EventBus::Instance());
}

