#pragma once

#include <QMouseEvent>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>
#include "src/core/app_settings.h"
#include "src/core/scene_enums.h"
#include "src/logic/map_topology_validator.h"
#include "src/ui/widgets/floating_panel_widget.h"
#include "src/ui/widgets/geo_viewer.h"

class TopologyValidatorWidget : public FloatingPanelWidget {
  Q_OBJECT
 public:
  explicit TopologyValidatorWidget(GeoViewerWidget* viewer,
                                   const geoviewer::core::AppSettings& settings,
                                   QWidget* parent = nullptr);
  ~TopologyValidatorWidget() override = default;

 public slots:
  void Clear();
  void HandleRunValidation();

 protected:
  void RetranslateUi() override;
  void ToggleCollapse() override;

  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;

 private slots:
  void HandleItemDoubleClicked(QTreeWidgetItem* item, int column);

 private:
  void SetupUi();

  GeoViewerWidget* viewer_ = nullptr;
  geoviewer::core::AppSettings settings_;
  QWidget* content_area_ = nullptr;
  QTreeWidget* tree_widget_ = nullptr;
  QPushButton* run_btn_ = nullptr;

  std::vector<geoviewer::logic::TopologyIssue> current_issues_;
};
