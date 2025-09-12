#pragma once

#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QObject>
#include <QTimer>
#include <memory>
#include "src/utility/map_loader.h"

class AsyncMapLoader : public QObject {
  Q_OBJECT

 public:
  explicit AsyncMapLoader(std::unique_ptr<IMapSceneLoader> loader,
                          QObject* parent = nullptr);

  void Start(const QString& path);
  bool IsRunning() const;
  MapSceneData TakeResult();

 signals:
  void ProgressTextChanged(const QString& text);
  void Finalizing();
  void Finished(bool success);

 private:
  void StopProgressUpdates();

  std::unique_ptr<IMapSceneLoader> loader_;
  QFutureWatcher<MapSceneData> watcher_;
  QTimer progress_timer_;
  QElapsedTimer elapsed_;
  MapSceneData last_result_;
};
