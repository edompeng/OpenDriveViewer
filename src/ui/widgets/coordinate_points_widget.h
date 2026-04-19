#pragma once

#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include "src/ui/widgets/floating_panel_widget.h"
#include "src/ui/widgets/geo_viewer.h"
#include "src/core/scene_enums.h"

/// @brief Coordinate points management panel
///
/// Provides coordinate input and point list management (show/hide, color
/// modification, deletion, double-click to jump). Follows SRP: Coordinate input
/// and list management are combined in the same widget, but rendering logic is
/// delegated to GeoViewerWidget.
class CoordinatePointsWidget : public FloatingPanelWidget {
  Q_OBJECT
 public:
  explicit CoordinatePointsWidget(GeoViewerWidget* viewer,
                                  QWidget* parent = nullptr);
  void SetCoordinateMode(CoordinateMode mode);

 protected:
  void RetranslateUi() override;
  void showEvent(QShowEvent* event) override;

 private slots:
  void HandleAddPoint();
  void HandleClearPoints();
  void HandlePointsChanged();
  void HandleItemDoubleClicked(QListWidgetItem* item);
  void HandleCustomContextMenu(const QPoint& pos);
  void ToggleCollapse();

 private:
  /// @brief Build a single list item widget with checkbox, color button, and
  /// coordinates label
  QWidget* BuildPointItemWidget(int index);
  void RefreshPointsList();

  GeoViewerWidget* viewer_;
  QLineEdit* input_points_edit_;
  QListWidget* points_list_;
  QWidget* content_area_;
  QToolButton* collapse_button_;
  QLabel* title_label_ = nullptr;
  QLabel* input_label_ = nullptr;
  QPushButton* add_btn_ = nullptr;
  QPushButton* clear_btn_ = nullptr;
  QLabel* list_label_ = nullptr;
  bool is_collapsed_ = false;
  bool points_list_dirty_ = true;
  CoordinateMode coord_mode_ = CoordinateMode::kWGS84;
};
