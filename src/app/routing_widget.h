#pragma once

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include "src/app/geo_viewer.h"

#include <QLabel>
#include <QMouseEvent>
#include <QPoint>
#include <QPushButton>
#include <QTreeWidget>
#include "src/app/floating_panel_widget.h"

class RoutingWidget : public FloatingPanelWidget {
  Q_OBJECT
 public:
  explicit RoutingWidget(GeoViewerWidget* viewer, QWidget* parent = nullptr);

 public slots:
  void SetStartLane(const QString& lane_pos);
  void SetEndLane(const QString& lane_pos);

 protected:
  void RetranslateUi() override;

 protected:
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void leaveEvent(QEvent* event) override;

 private slots:
  void HandleCalculate();
  void ToggleCollapse();
  void HandleHistoryItemChanged(QTreeWidgetItem* item, int column);
  void HandleHistoryContextMenu(const QPoint& pos);
  void HandleItemEntered(QTreeWidgetItem* item, int column);
  void HandleItemDoubleClicked(QTreeWidgetItem* item, int column);

 private:
  GeoViewerWidget* viewer_;
  QLineEdit* start_input_;
  QLineEdit* end_input_;
  QTreeWidget* history_tree_;
  QWidget* content_area_;
  QToolButton* collapse_button_;
  QLabel* title_label_ = nullptr;
  QLabel* start_label_ = nullptr;
  QLabel* end_label_ = nullptr;
  QPushButton* calc_btn_ = nullptr;
  QLabel* history_label_ = nullptr;
  bool is_collapsed_ = false;
};
