#include "src/ui/widgets/async_map_loader.h"

#include <iostream>
#include <utility>
#include "src/core/thread_pool.h"

AsyncMapLoader::AsyncMapLoader(std::unique_ptr<IMapSceneLoader> loader,
                               QObject* parent)
    : QObject(parent), loader_(std::move(loader)) {}

void AsyncMapLoader::Start(const QString& path) {
  if (!loader_ || is_running_) return;

  is_running_ = true;
  last_result_ = MapSceneData();
  elapsed_.restart();
  emit ProgressTextChanged("Loading map and generating mesh... (0%)");

  const QPointer<AsyncMapLoader> self(this);
  const std::shared_ptr<IMapSceneLoader> loader = loader_;
  auto start_time = std::chrono::steady_clock::now();
  geoviewer::utility::ThreadPool::Instance().Enqueue([self, loader, path,
                                                      start_time]() {
    MapSceneData result;
    bool success = false;
    try {
      result = loader->Load(
          path.toStdString(),
          [self, start_time](float progress, const std::string& stage) {
            if (!self) return;
            double seconds = std::chrono::duration<double>(
                                 std::chrono::steady_clock::now() - start_time)
                                 .count();
            QString msg = QString::fromStdString(stage);
            QMetaObject::invokeMethod(
                self,
                [self, progress, msg, seconds]() {
                  if (!self) return;
                  emit self->ProgressTextChanged(
                      QString("%1 (%2%) - %3s")
                          .arg(msg)
                          .arg(static_cast<int>(progress * 100))
                          .arg(seconds, 0, 'f', 1));
                  emit self->ProgressChanged(progress, msg);
                },
                Qt::QueuedConnection);
          });
      success = result.IsValid();
    } catch (const std::exception& e) {
      std::cerr << "Exception loading map: " << e.what() << std::endl;
    } catch (...) {
      std::cerr << "Unknown exception loading map" << std::endl;
    }

    if (!self) return;
    QMetaObject::invokeMethod(
        self,
        [self, res = std::move(result), success]() mutable {
          if (!self) return;
          self->is_running_ = false;
          self->last_result_ = std::move(res);
          if (success) {
            emit self->Finalizing();
          }
          emit self->Finished(success);
        },
        Qt::QueuedConnection);
  });
}

bool AsyncMapLoader::IsRunning() const { return is_running_; }

MapSceneData AsyncMapLoader::TakeResult() {
  MapSceneData result = std::move(last_result_);
  last_result_ = MapSceneData();
  return result;
}
