#include "src/app/async_map_loader.h"

#include <QtConcurrent/QtConcurrent>
#include <utility>

AsyncMapLoader::AsyncMapLoader(std::unique_ptr<IMapSceneLoader> loader,
                               QObject* parent)
    : QObject(parent), loader_(std::move(loader)) {
  progress_timer_.setInterval(1000);

  connect(&progress_timer_, &QTimer::timeout, this, [this]() {
    const int seconds = static_cast<int>(elapsed_.elapsed() / 1000);
    emit ProgressTextChanged(
        QString("Loading map and generating mesh... (%1s)").arg(seconds));
  });

  connect(&watcher_, &QFutureWatcher<MapSceneData>::finished, this, [this]() {
    StopProgressUpdates();
    last_result_ = watcher_.result();
    if (last_result_.IsValid()) {
      emit Finalizing();
    }
    emit Finished(last_result_.IsValid());
  });
}

void AsyncMapLoader::Start(const QString& path) {
  if (!loader_ || watcher_.isRunning()) return;

  last_result_ = MapSceneData();
  elapsed_.restart();
  progress_timer_.start();
  emit ProgressTextChanged("Loading map and generating mesh...");

  watcher_.setFuture(QtConcurrent::run([loader = loader_.get(), path]() {
    return loader->Load(path.toStdString());
  }));
}

bool AsyncMapLoader::IsRunning() const { return watcher_.isRunning(); }

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
