#include "src/app/async_map_loader.h"

#include <thread>
#include <utility>
#include "src/utility/thread_pool.h"

AsyncMapLoader::AsyncMapLoader(std::unique_ptr<IMapSceneLoader> loader,
                               QObject* parent)
    : QObject(parent), loader_(std::move(loader)) {
  progress_timer_.setInterval(1000);

  connect(&progress_timer_, &QTimer::timeout, this, [this]() {
    const int seconds = static_cast<int>(elapsed_.elapsed() / 1000);
    emit ProgressTextChanged(
        QString("Loading map and generating mesh... (%1s)").arg(seconds));
  });
}

void AsyncMapLoader::Start(const QString& path) {
  if (!loader_ || is_running_) return;

  is_running_ = true;
  last_result_ = MapSceneData();
  elapsed_.restart();
  progress_timer_.start();
  emit ProgressTextChanged("Loading map and generating mesh...");

  geoviewer::utility::ThreadPool::Instance().Enqueue([this, path]() {
    auto result = loader_->Load(path.toStdString());
    QMetaObject::invokeMethod(this, [this, res = std::move(result)]() mutable {
      is_running_ = false;
      StopProgressUpdates();
      last_result_ = std::move(res);
      if (last_result_.IsValid()) {
        emit Finalizing();
      }
      emit Finished(last_result_.IsValid());
    });
  });
}

bool AsyncMapLoader::IsRunning() const { return is_running_; }

MapSceneData AsyncMapLoader::TakeResult() {
  MapSceneData result = std::move(last_result_);
  last_result_ = MapSceneData();
  return result;
}

void AsyncMapLoader::StopProgressUpdates() {
  if (progress_timer_.isActive()) {
    progress_timer_.stop();
  }
}
