#pragma once
#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QProgressDialog>
#include <QPushButton>
#include <QResizeEvent>
#include <QStatusBar>
#include <vector>
#include "src/app/async_map_loader.h"
#include "src/ui/widgets/coordinate_points_widget.h"
#include "src/ui/widgets/favorites_widget.h"
#include "src/ui/widgets/geo_viewer.h"
#include "src/ui/widgets/layer_control_widget.h"
#include "src/ui/widgets/loading_progress_widget.h"
#include "src/ui/widgets/routing_widget.h"
#include "src/ui/widgets/open_scenario_widget.h"
#include "src/utility/scene_enums.h"

class MainWindow : public QMainWindow {
  Q_OBJECT
 public:
  MainWindow(QWidget *parent = nullptr);

 private slots:
  void HandleLoadMap();
  void HandleHoverInfo(double x, double y, double z, double lon, double lat,
                       double alt, const QString &type_str,
                       const QString &id_str, const QString &name_str);

  void HandleJumpToCoords();
  void ToggleWidgetVisibility(QWidget *widget, bool visible);
  void ChangeLanguage(const QString &locale);

 protected:
  void resizeEvent(QResizeEvent *event) override;
  void changeEvent(QEvent *event) override;

 private:
  void SetupPanels();
  void SetupToolbar();
  void RetranslateUi();
  QWidget *BuildCoordinateTools();
  void SetupConnections();
  void UpdateWindowTitle();
  void StartMapLoad(const QString &path);
  void ApplyCoordinateModePolicy(bool georeference_valid);

  GeoViewerWidget *view_;
  QLineEdit *jump_to_coords_edit_;
  QStatusBar *status_;
  LayerControlWidget *layer_control_;
  OpenScenarioWidget *open_scenario_panel_;
  RoutingWidget *routing_panel_;
  FavoritesWidget *favorites_panel_;
  CoordinatePointsWidget *coordinate_points_panel_;
  LoadingProgressWidget *load_progress_ = nullptr;
  QString current_map_path_;
  QString pending_map_path_;
  QAction *measure_action_;
  AsyncMapLoader *map_loader_;
  QTranslator *translator_;

  // UI Elements that need retranslation
  QMenu *panels_menu_;
  QToolButton *panels_btn_;
  QMenu *lang_menu_;
  QToolButton *lang_btn_;
  QAction *load_action_;
  QLabel *jump_label_;
  std::vector<QCheckBox *> layer_checkboxes_;
  CoordinateMode coord_mode_ = CoordinateMode::kWGS84;
  QComboBox *coord_mode_combo_ = nullptr;
  bool wgs84_mode_allowed_ = true;
};
