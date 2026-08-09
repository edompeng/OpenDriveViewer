#pragma once

#include <QJsonObject>
#include <QObject>
#include <functional>
#include <string>

#include "src/geo_viewer_export.h"

class GeoViewerWidget;

namespace geoviewer::mcp {

class GEOVIEWER_EXPORT McpBridge : public QObject {
  Q_OBJECT

 public:
  explicit McpBridge(
      GeoViewerWidget* viewer_widget,
      std::function<void(const QString&)> load_map_fn = nullptr,
      QObject* parent = nullptr);
  ~McpBridge() override = default;

  // --- UI Operations (Main Thread Dispatched) ---
  QJsonObject SetCamera(const QJsonObject& args);
  QJsonObject JumpToLocation(const QJsonObject& args);
  QJsonObject SetViewMode(const QJsonObject& args);
  QJsonObject AddUserPoints(const QJsonObject& args);
  QJsonObject ClearUserPoints(const QJsonObject& args);
  QJsonObject AddRoutingPath(const QJsonObject& args);
  QJsonObject ClearRoutingPaths(const QJsonObject& args);
  QJsonObject HighlightElement(const QJsonObject& args);
  QJsonObject SetLayerVisibility(const QJsonObject& args);
  QJsonObject LoadMap(const QJsonObject& args);
  QJsonObject TakeScreenshot(const QJsonObject& args);

  // --- Data Query Operations ---
  QJsonObject GetMapInfo(const QJsonObject& args);
  QJsonObject GetRoads(const QJsonObject& args);
  QJsonObject GetRoadDetail(const QJsonObject& args);
  QJsonObject GetLaneGeometry(const QJsonObject& args);
  QJsonObject GetJunctions(const QJsonObject& args);
  QJsonObject GetSignals(const QJsonObject& args);
  QJsonObject GetObjects(const QJsonObject& args);
  QJsonObject QueryPoint(const QJsonObject& args);
  QJsonObject CoordinateTransform(const QJsonObject& args);

 private:
  template <typename Func>
  QJsonObject RunOnMainThread(Func&& func);

  GeoViewerWidget* viewer_widget_ = nullptr;
  std::function<void(const QString&)> load_map_fn_;
};

}  // namespace geoviewer::mcp
