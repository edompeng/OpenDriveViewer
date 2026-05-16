#pragma once

#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPoint>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include "src/core/app_settings.h"
#include "src/core/scene_enums.h"
#include "src/logic/favorites_store.h"
#include "src/ui/widgets/floating_panel_widget.h"
#include "src/ui/widgets/geo_viewer.h"

class FavoritesWidget : public FloatingPanelWidget {
  Q_OBJECT
 public:
  explicit FavoritesWidget(GeoViewerWidget* viewer,
                           const geoviewer::core::AppSettings& settings,
                           QWidget* parent = nullptr);

 public slots:
  void AddFavorite(const QString& road_id, TreeNodeType type,
                   const QString& element_id, const QString& name);
  void Clear();

 protected:
  void RetranslateUi() override;
  void ToggleCollapse() override;

 protected:
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;

 private slots:
  void HandleCustomContextMenu(const QPoint& pos);
  void HandleItemEntered(QListWidgetItem* item);
  void HandleItemDoubleClicked(QListWidgetItem* item);
  void HandleItemClicked(QListWidgetItem* item);

 private:
  void RefreshListIndices();

  GeoViewerWidget* viewer_;
  QListWidget* list_;
  QWidget* content_area_;
  FavoritesStore favorites_;
};
