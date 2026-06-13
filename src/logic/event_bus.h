#pragma once

#include <QObject>
#include <QString>
#include <QVector3D>
#include "src/geo_viewer_export.h"

namespace geoviewer::logic {

class GEOVIEWER_EXPORT EventBus : public QObject {
  Q_OBJECT
 public:
  static EventBus& Instance() {
    static EventBus instance;
    return instance;
  }

 signals:
  void MapLoaded(const QString& path, bool success);
  void LayerVisibilityChanged(int layer_type, bool visible);
  void HighlightChanged();
  void RouteGenerated(bool success);
  void SimulationStateChanged(bool active);
  void SimulationPoseUpdated(const QVector3D& position, float heading);

 private:
  EventBus() = default;
  ~EventBus() override = default;
  EventBus(const EventBus&) = delete;
  EventBus& operator=(const EventBus&) = delete;
};

}  // namespace geoviewer::logic
