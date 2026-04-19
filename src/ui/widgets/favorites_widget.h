#pragma once

#include <QAction>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include "src/ui/widgets/floating_panel_widget.h"
#include "src/ui/widgets/geo_viewer.h"
#include "src/logic/favorites_store.h"

class FavoritesWidget : public FloatingPanelWidget {
  Q_OBJECT
 public:
  explicit FavoritesWidget(GeoViewerWidget* viewer, QWidget* parent = nullptr);

 protected:
  void RetranslateUi() override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;

 public slots:
  void AddFavorite(const QString& road_id, TreeNodeType type,
                   const QString& element_id, const QString& name = "");
  void ToggleCollapse();

 private slots:
  void HandleCustomContextMenu(const QPoint& pos);
  void HandleItemEntered(QListWidgetItem* item);
  void HandleItemClicked(QListWidgetItem* item);
  void HandleItemDoubleClicked(QListWidgetItem* item);

 private:
  void RefreshListIndices();

  GeoViewerWidget* viewer_;
  QListWidget* list_;
  QWidget* content_area_;
  QLabel* title_label_;
  QToolButton* collapse_button_;
  bool is_collapsed_ = false;
  FavoritesStore favorites_;
};
