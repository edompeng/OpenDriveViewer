#include "src/ui/widgets/async_map_loader.h"

#include <utility>
#include "src/core/thread_pool.h"

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

  const QPointer<AsyncMapLoader> self(this);
  const std::shared_ptr<IMapSceneLoader> loader = loader_;
  geoviewer::utility::ThreadPool::Instance().Enqueue([self, loader, path]() {
    auto result = loader->Load(path.toStdString());
    if (!self) return;
    QMetaObject::invokeMethod(
        self,
        [self, res = std::move(result)]() mutable {
          if (!self) return;
          self->is_running_ = false;
          self->StopProgressUpdates();
          self->last_result_ = std::move(res);
          if (self->last_result_.IsValid()) {
            emit self->Finalizing();
          }
          emit self->Finished(self->last_result_.IsValid());
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

void AsyncMapLoader::StopProgressUpdates() {
  if (progress_timer_.isActive()) {
    progress_timer_.stop();
  }
}
