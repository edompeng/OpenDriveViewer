#pragma once

#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include "src/core/app_settings.h"
#include "src/core/scene_enums.h"
#include "src/ui/widgets/floating_panel_widget.h"
#include "src/ui/widgets/geo_viewer.h"

class CoordinatePointsWidget : public FloatingPanelWidget {
  Q_OBJECT
 public:
  CoordinatePointsWidget(GeoViewerWidget* viewer,
                         const geoviewer::core::AppSettings& settings,
                         QWidget* parent = nullptr);

  void SetCoordinateMode(CoordinateMode mode);
  void Clear();

 protected:
  void RetranslateUi() override;
  void ToggleCollapse() override;

 protected:
  void showEvent(QShowEvent* event) override;

 private slots:
  void HandleAddPoint();
  void HandleClearPoints();
  void HandlePointsChanged();
  void HandleItemDoubleClicked(QTreeWidgetItem* item, int column);
  void HandleCustomContextMenu(const QPoint& pos);
  void HandlePickColor();

 private:
  void RefreshPointsList();
  void UpdatePointsListState();
  QWidget* BuildPointItemWidget(int index);

  GeoViewerWidget* viewer_;
  CoordinateMode coord_mode_ = CoordinateMode::kWGS84;
  QVector3D next_point_color_;

  QLineEdit* input_points_edit_;
  QTreeWidget* points_tree_;
  QWidget* content_area_;
  QLabel* input_label_ = nullptr;
  QToolButton* color_btn_ = nullptr;
  QPushButton* add_btn_ = nullptr;
  QPushButton* clear_btn_ = nullptr;
  QLabel* list_label_ = nullptr;

  bool points_tree_dirty_ = false;
};
