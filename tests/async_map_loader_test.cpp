#include "src/ui/widgets/async_map_loader.h"
#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QThread>
#include "tests/test_helpers.h"

TEST(AsyncMapLoaderTest, LoadMapAsync) {
  int argc = 0;
  char* argv[] = {nullptr};
  QCoreApplication app(argc, argv);

  auto loader_impl = std::make_unique<OpenDriveMapSceneLoader>();
  AsyncMapLoader map_loader(std::move(loader_impl));

  bool finished_called = false;
  bool success_status = false;
  float max_progress = 0.0f;

  QObject::connect(&map_loader, &AsyncMapLoader::Finished, [&](bool success) {
    finished_called = true;
    success_status = success;
  });

  QObject::connect(&map_loader, &AsyncMapLoader::ProgressChanged,
                   [&](float progress, const QString& text) {
                     Q_UNUSED(text);
                     if (progress > max_progress) {
                       max_progress = progress;
                     }
                   });

  std::string map_path = geoviewer::test::FindTestData("data/test2.xodr");
  map_loader.Start(QString::fromStdString(map_path));

  EXPECT_TRUE(map_loader.IsRunning());

  // Spin event loop until finished
  QDeadlineTimer deadline(5000);  // 5s timeout
  while (!deadline.hasExpired() && !finished_called) {
    QCoreApplication::processEvents();
    QThread::msleep(10);
  }

  EXPECT_TRUE(finished_called);
  EXPECT_TRUE(success_status);
  EXPECT_FALSE(map_loader.IsRunning());

  MapSceneData result = map_loader.TakeResult();
  EXPECT_TRUE(result.IsValid());
}
