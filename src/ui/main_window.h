#pragma once
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QProgressDialog>
#include <QPushButton>
#include <QResizeEvent>
#include <QStatusBar>
#include "src/core/app_settings.h"
#include "src/core/scene_enums.h"
#include "src/ui/widgets/async_map_loader.h"
#include "src/ui/widgets/coordinate_points_widget.h"
#include "src/ui/widgets/favorites_widget.h"
#include "src/ui/widgets/geo_viewer.h"
#include "src/ui/widgets/layer_control_widget.h"
#include "src/ui/widgets/loading_progress_widget.h"
#include "src/ui/widgets/routing_widget.h"

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
  void HandleCopyMapBaseName();
  void ToggleWidgetVisibility(QWidget *widget, bool visible);
  void ChangeLanguage(const QString &locale);
  void HandleSettingsChanged();
  void HandleViewModeToggle(bool is_2d);


 protected:
  void resizeEvent(QResizeEvent *event) override;
  void changeEvent(QEvent *event) override;
  void dragEnterEvent(QDragEnterEvent *event) override;
  void dropEvent(QDropEvent *event) override;

 private:
  void SetupPanels();
  void SetupToolbar();
  void RetranslateUi();
  QWidget *BuildCoordinateTools();
  void SetupConnections();
  void UpdateWindowTitle();
  void StartMapLoad(const QString &path);
  void ApplyCoordinateModePolicy(bool georeference_valid);
  void SaveSettingsToStruct();

  GeoViewerWidget *view_ = nullptr;
  QLineEdit *jump_to_coords_edit_ = nullptr;
  QStatusBar *status_ = nullptr;
  LayerControlWidget *layer_control_ = nullptr;
  QDockWidget *layer_control_dock_ = nullptr;
  RoutingWidget *routing_panel_ = nullptr;
  FavoritesWidget *favorites_panel_ = nullptr;
  CoordinatePointsWidget *coordinate_points_panel_ = nullptr;
  LoadingProgressWidget *load_progress_ = nullptr;
  QString current_map_path_;
  QString pending_map_path_;
  QAction *measure_action_;
  QAction *view_mode_action_ = nullptr;
  QAction *copy_map_name_action_ = nullptr;

  AsyncMapLoader *map_loader_;
  QTranslator *translator_;

  // UI Elements that need retranslation
  QMenu *panels_menu_ = nullptr;
  QToolButton *panels_btn_ = nullptr;
  QMenu *lang_menu_ = nullptr;
  QToolButton *lang_btn_ = nullptr;
  QAction *load_action_ = nullptr;
  QLabel *jump_label_ = nullptr;
  CoordinateMode coord_mode_ = CoordinateMode::kWGS84;
  QComboBox *coord_mode_combo_ = nullptr;
  bool wgs84_mode_allowed_ = true;
  geoviewer::core::AppSettings settings_;
};
