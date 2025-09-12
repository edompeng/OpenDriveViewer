#pragma once

#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPoint>
#include <QPushButton>
#include <QToolButton>
#include <QTreeWidget>
#include <QWidget>
#include "src/app/floating_panel_widget.h"
#include "src/app/geo_viewer.h"

class OpenScenarioWidget : public FloatingPanelWidget {
  Q_OBJECT

 public:
  explicit OpenScenarioWidget(GeoViewerWidget* viewer,
                              QWidget* parent = nullptr);

 protected:
  void RetranslateUi() override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void leaveEvent(QEvent* event) override;

 private slots:
  void ToggleCollapse();
  void HandleAddFiles();
  void HandleRemoveSelected();
  void HandleItemChanged(QTreeWidgetItem* item, int column);
  void HandleItemEntered(QTreeWidgetItem* item, int column);
  void HandleItemDoubleClicked(QTreeWidgetItem* item, int column);
  void HandleCustomContextMenu(const QPoint& pos);
  void RebuildTree();

 private:
  void UpdateParentCheckState(QTreeWidgetItem* item);
  void SetSubtreeCheckState(QTreeWidgetItem* item, Qt::CheckState state);

  GeoViewerWidget* viewer_ = nullptr;
  QWidget* content_area_ = nullptr;
  QTreeWidget* tree_ = nullptr;
  QToolButton* collapse_button_ = nullptr;
  QLabel* title_label_ = nullptr;
  QPushButton* add_files_btn_ = nullptr;
  QPushButton* remove_btn_ = nullptr;
  bool is_collapsed_ = false;
  bool is_syncing_ = false;
};
