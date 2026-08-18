#pragma once
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QProgressDialog>
#include <QPushButton>
#include <QResizeEvent>
#include <QStatusBar>
#include <map>
#include "RoadNetworkMesh.h"
#include "src/core/app_settings.h"
#include "src/core/scene_enums.h"
#include "src/ui/widgets/async_map_loader.h"
#include "src/ui/widgets/coordinate_points_widget.h"
#include "src/ui/widgets/favorites_widget.h"
#include "src/ui/widgets/geo_viewer.h"
#include "src/ui/widgets/layer_control_widget.h"
#include "src/ui/widgets/loading_progress_widget.h"
#include "src/ui/widgets/routing_widget.h"
#include "src/ui/widgets/shortcut_manager.h"

class TopologyValidatorWidget;

namespace geoviewer::ui {
class XmlEditorDialog;
struct XmlTarget;
}  // namespace geoviewer::ui

namespace geoviewer::mcp {
class McpServer;
}

class MainWindow : public QMainWindow {
  Q_OBJECT
 public:
  MainWindow(QWidget *parent = nullptr);
  ~MainWindow() override;
  void StartMapLoad(const QString &path);
  GeoViewerWidget *GetViewerWidget() const { return view_; }

  void StartMcpStdio();
  bool StartMcpHttp(uint16_t port = 8080);
  void StopMcpServer();
  geoviewer::mcp::McpServer *GetMcpServer() const { return mcp_server_; }

 private slots:
  void HandleLoadMap();
  void HandleCompareMap();
  void HandleHoverInfo(double x, double y, double z, double lon, double lat,
                       double alt, const QString &type_str,
                       const QString &id_str, const QString &name_str,
                       double hdg = 0.0, double s = 0.0, double t = 0.0,
                       bool has_lane_info = false);

  void HandleJumpToCoords();
  void HandleCopyMapBaseName();
  void HandleScreenshot();
  void HandleShowStats();
  void ToggleWidgetVisibility(QWidget *widget, bool visible);
  void ChangeLanguage(const QString &locale);
  void HandleSettingsChanged();
  void HandleViewModeToggle(bool is_2d);
  void HandleShowXml(const geoviewer::ui::XmlTarget &target,
                     const QString &xml_text);
  void HandleXmlSaved(const geoviewer::ui::XmlTarget &target,
                      const QString &xml_text);
  void HandleSaveMapAs();
  void TriggerMeshUpdate(const std::string &target_road_id = "");
  void HandleToggleMcpServer();
  void HandleToggleCoordinateMode();
  void HandleShowShortcutSettings();
  void HandleShowAbout();
  void HandleResetLayout();
  void HandleCycleLanguage();

 protected:
  void resizeEvent(QResizeEvent *event) override;
  void changeEvent(QEvent *event) override;
  void dragEnterEvent(QDragEnterEvent *event) override;
  void dropEvent(QDropEvent *event) override;
  void closeEvent(QCloseEvent *event) override;

 private:
  void SetupPanels();
  void SetupMenus();
  void SetupToolbar();
  void SetupShortcuts();
  void RetranslateUi();
  QWidget *BuildCoordinateTools();
  void SetupConnections();
  void UpdateWindowTitle();
  void ApplyCoordinateModePolicy(bool georeference_valid);
  void SaveSettingsToStruct();
  QDockWidget *CreateToolDock(const QString &object_name, const QString &title,
                              QWidget *content, Qt::DockWidgetArea area);
  void SetDefaultDockLayout();
  void ToggleDock(QDockWidget *dock, bool focus_coordinate_input = false);

  GeoViewerWidget *view_ = nullptr;
  QLineEdit *jump_to_coords_edit_ = nullptr;
  QStatusBar *status_ = nullptr;
  LayerControlWidget *layer_control_ = nullptr;
  QDockWidget *layer_control_dock_ = nullptr;
  QDockWidget *routing_dock_ = nullptr;
  QDockWidget *favorites_dock_ = nullptr;
  QDockWidget *coordinate_points_dock_ = nullptr;
  QDockWidget *topology_validator_dock_ = nullptr;
  RoutingWidget *routing_panel_ = nullptr;
  FavoritesWidget *favorites_panel_ = nullptr;
  CoordinatePointsWidget *coordinate_points_panel_ = nullptr;
  TopologyValidatorWidget *topology_validator_panel_ = nullptr;
  LoadingProgressWidget *load_progress_ = nullptr;
  QString current_map_path_;
  QString pending_map_path_;
  QAction *measure_action_;
  QAction *view_mode_action_ = nullptr;
  QAction *copy_map_name_action_ = nullptr;
  QAction *compare_action_ = nullptr;
  QAction *screenshot_action_ = nullptr;
  QAction *stats_action_ = nullptr;
  QAction *save_as_action_ = nullptr;
  QAction *mcp_action_ = nullptr;
  QAction *exit_action_ = nullptr;
  QAction *coordinate_mode_action_ = nullptr;
  QAction *shortcut_settings_action_ = nullptr;
  QAction *about_action_ = nullptr;
  QAction *reset_layout_action_ = nullptr;
  QAction *cycle_language_action_ = nullptr;
  geoviewer::ui::XmlEditorDialog *xml_editor_ = nullptr;
  geoviewer::mcp::McpServer *mcp_server_ = nullptr;
  bool is_modified_ = false;

  AsyncMapLoader *map_loader_;
  QTranslator *translator_;

  // UI Elements that need retranslation
  QMenu *panels_menu_ = nullptr;
  QMenu *lang_menu_ = nullptr;
  QMenu *file_menu_ = nullptr;
  QMenu *view_menu_ = nullptr;
  QMenu *tools_menu_ = nullptr;
  QMenu *settings_menu_ = nullptr;
  QMenu *help_menu_ = nullptr;
  QAction *load_action_ = nullptr;
  QLabel *jump_label_ = nullptr;
  CoordinateMode coord_mode_ = CoordinateMode::kWGS84;
  QComboBox *coord_mode_combo_ = nullptr;
  bool wgs84_mode_allowed_ = true;
  geoviewer::core::AppSettings settings_;
  geoviewer::ui::ShortcutManager shortcut_manager_;
  QLabel *version_label_ = nullptr;
  std::map<std::string, odr::RoadNetworkMesh> road_id_to_mesh_cache_;
};
