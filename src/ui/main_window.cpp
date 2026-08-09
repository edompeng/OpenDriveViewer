#include "src/ui/main_window.h"
#include <QtCore/qdebug.h>
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QInputDialog>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolBar>
#include <QTranslator>
#include <QWidget>
#include "src/mcp/mcp_server.h"
#include "OpenDriveMap.h"
#include "Road.h"
#include "src/core/app_settings.h"
#include "src/core/coordinate_mode_policy.h"
#include "src/core/coordinate_util.h"
#include "src/core/map_loader.h"
#include "src/core/settings_persistence.h"
#include "src/core/thread_pool.h"
#include "src/logic/event_bus.h"
#include "src/logic/input_parsing.h"
#include "src/ui/widgets/floating_panel_widget.h"
#include "src/ui/widgets/layer_control_widget.h"
#include "src/ui/widgets/map_statistics_dialog.h"
#include "src/ui/widgets/topology_validator_widget.h"
#include "src/ui/widgets/xml_editor_dialog.h"
#include "src/ui/widgets/xml_editor_types.h"

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  translator_ = new QTranslator(this);

  // Load Persistence Settings
  settings_ =
      geoviewer::core::SettingsPersistence::Load(qApp->applicationDirPath());

  // Language setup from settings
  if (!settings_.language.isEmpty()) {
    ChangeLanguage(settings_.language);
  } else {
    QString locale = QLocale::system().name();
    if (locale.isEmpty()) locale = "zh_CN";
    ChangeLanguage(locale);
  }

  view_ = new GeoViewerWidget(this);
  setCentralWidget(view_);
  map_loader_ =
      new AsyncMapLoader(std::make_unique<OpenDriveMapSceneLoader>(), this);

  status_ = statusBar();
  SetupPanels();
  SetupToolbar();

  // Apply coordinate mode from settings
  coord_mode_ = settings_.coordinate_mode;
  if (coord_mode_combo_) {
    coord_mode_combo_->setCurrentIndex(
        coord_mode_ == CoordinateMode::kWGS84 ? 0 : 1);
  }

  SetupConnections();

  setAcceptDrops(true);
  UpdateWindowTitle();
}

void MainWindow::HandleLoadMap() {
  QString path =
      QFileDialog::getOpenFileName(this, tr("Open OpenDRIVE file"), QString(),
                                   tr("OpenDRIVE Files (*.xodr *.xml)"));
  if (path.isEmpty()) return;
  StartMapLoad(path);
}

void MainWindow::HandleHoverInfo(double x, double y, double z, double lon,
                                 double lat, double alt,
                                 const QString& type_str, const QString& id_str,
                                 const QString& name_str, double hdg, double s,
                                 double t, bool has_lane_info) {
  QString status;
  if (coord_mode_ == CoordinateMode::kWGS84) {
    status = tr("Coords: %1, %2, %3")
                 .arg(lon, 0, 'f', 8)
                 .arg(lat, 0, 'f', 8)
                 .arg(alt, 0, 'f', 2);
  } else {
    status = tr("Coords: %1, %2, %3")
                 .arg(x, 0, 'f', 3)
                 .arg(y, 0, 'f', 3)
                 .arg(z, 0, 'f', 3);
  }

  if (!type_str.isEmpty()) {
    status += tr("Obj: %1 (ID: %2)").arg(type_str).arg(id_str);
    if (!name_str.isEmpty()) {
      status += tr(" [%1]").arg(name_str);
    }
  }

  if (has_lane_info) {
    status += tr(" | Hdg: %1 rad | s,t: (%2, %3)")
                  .arg(hdg, 0, 'f', 6)
                  .arg(s, 0, 'f', 3)
                  .arg(t, 0, 'f', 3);
  }

  status_->showMessage(status);
}

void MainWindow::HandleJumpToCoords() {
  const auto target = CoordinateInputParser::ParseJumpLocation(
      jump_to_coords_edit_->text().toStdString());
  if (!target.has_value()) {
    if (coord_mode_ == CoordinateMode::kWGS84) {
      status_->showMessage(
          tr("Please enter longitude, latitude (optional "
             "altitude), separated by comma or space"));
    } else {
      status_->showMessage(
          tr("Please enter x, y (optional z), separated by "
             "comma or space"));
    }
    return;
  }

  if (coord_mode_ == CoordinateMode::kWGS84) {
    view_->JumpToLocation(target->x, target->y, target->z);
    status_->showMessage(tr("Jumped to: %1, %2, %3")
                             .arg(target->x, 0, 'f', 8)
                             .arg(target->y, 0, 'f', 8)
                             .arg(target->z, 0, 'f', 2));
  } else {
    view_->JumpToLocalLocation(target->x, target->y, target->z);
    status_->showMessage(tr("Jumped to: %1, %2, %3")
                             .arg(target->x, 0, 'f', 3)
                             .arg(target->y, 0, 'f', 3)
                             .arg(target->z, 0, 'f', 3));
  }
}

void MainWindow::HandleCopyMapBaseName() {
  if (current_map_path_.isEmpty()) {
    status_->showMessage(tr("No map loaded."));
    return;
  }

  const QFileInfo file_info(current_map_path_);
  const QString base_name = file_info.completeBaseName();
  if (base_name.isEmpty()) {
    status_->showMessage(tr("Map filename is empty."));
    return;
  }

  QApplication::clipboard()->setText(base_name);
  status_->showMessage(tr("Copied map name: %1").arg(base_name));
}

void MainWindow::HandleScreenshot() {
  QImage img = view_->grabFramebuffer();
  QString path = QFileDialog::getSaveFileName(
      this, tr("Save Screenshot"), "",
      tr("PNG Images (*.png);;JPEG Images (*.jpg)"));
  if (!path.isEmpty()) {
    if (img.save(path)) {
      status_->showMessage(tr("Screenshot saved to: %1").arg(path), 3000);
    } else {
      status_->showMessage(tr("Failed to save screenshot"), 3000);
    }
  }
}

void MainWindow::HandleShowStats() {
  auto map = view_->GetMap();
  if (!map) return;
  geoviewer::ui::MapStatisticsDialog dialog(map, current_map_path_, this);
  dialog.exec();
}

void MainWindow::ChangeLanguage(const QString& locale) {
  // Explicitly include .qm extension for better robustness in resource loading
  QString path = ":/i18n/geoviewer_" + locale + ".qm";
  if (translator_->load(path)) {
    qDebug() << "Success loaded translation:" << path;
  } else {
    qDebug() << "Failed to load translation:" << path
             << ", falling back to zh_CN";
    if (translator_->load(":/i18n/geoviewer_zh_CN.qm")) {
      qDebug() << "Loaded fallback translation: :/i18n/geoviewer_zh_CN.qm";
    } else {
      qDebug() << "CRITICAL: Failed to load fallback translation!";
    }
  }
  // The LanguageChange event is triggered by qApp->installTranslator
  // but if it's already installed, we might need to manually trigger or
  // re-install
  qApp->removeTranslator(translator_);
  qApp->installTranslator(translator_);

  settings_.language = locale;
  HandleSettingsChanged();
}

void MainWindow::changeEvent(QEvent* event) {
  if (event->type() == QEvent::LanguageChange) {
    qDebug() << "LanguageChange event received";
    RetranslateUi();
  }
  QMainWindow::changeEvent(event);
}

void MainWindow::RetranslateUi() {
  // Update main window elements
  load_action_->setText(tr("Load .xodr"));
  load_action_->setToolTip(tr("Open an OpenDRIVE map file"));
  panels_btn_->setText(tr("Windows"));
  lang_btn_->setText(tr("Language"));
  measure_action_->setText(tr("Measure"));
  measure_action_->setToolTip(tr("Measure distance between points"));
  if (view_ && view_->GetViewMode() == CameraController::ViewMode::k2D) {
    view_mode_action_->setText(tr("2D"));
  } else {
    view_mode_action_->setText(tr("3D"));
  }
  view_mode_action_->setToolTip(
      tr("Toggle between 2D (Overhead) and 3D views"));

  if (copy_map_name_action_) {
    copy_map_name_action_->setText(tr("Copy Map Name"));
    copy_map_name_action_->setToolTip(
        tr("Copy current map file name without extension"));
  }
  UpdateWindowTitle();

  if (coord_mode_ == CoordinateMode::kWGS84) {
    jump_label_->setText(tr("Jump to (lon,lat,alt):"));
    jump_to_coords_edit_->setPlaceholderText(tr("lon,lat,alt"));
  } else {
    jump_label_->setText(tr("Jump to (x,y,z):"));
    jump_to_coords_edit_->setPlaceholderText(tr("x,y,z"));
  }

  if (coord_mode_combo_) {
    coord_mode_combo_->setItemText(0, tr("WGS84 (lon, lat)"));
    coord_mode_combo_->setItemText(1, tr("Local (x, y)"));
  }

  // Update Panel menu
  panels_menu_->setTitle(tr("Panels"));
  auto actions = panels_menu_->actions();
  if (actions.size() >= 4) {
    actions[0]->setText(tr("Layer Manager"));
    actions[1]->setText(tr("Routing"));
    actions[2]->setText(tr("Favorites"));
    actions[3]->setText(tr("Coordinate Inputs"));
  }

  // Update Language menu
  lang_menu_->setTitle(tr("Language"));

  if (layer_control_dock_) {
    layer_control_dock_->setWindowTitle(tr("Layer Manager"));
  }
}

void MainWindow::resizeEvent(QResizeEvent* event) {
  QMainWindow::resizeEvent(event);
  if (favorites_panel_ && view_) {
    favorites_panel_->move(view_->width() - favorites_panel_->width() - 20, 20);
  }
}

void MainWindow::SetupPanels() {
  layer_control_dock_ = new QDockWidget(tr("Layer Manager"), this);
  layer_control_dock_->setObjectName("LayerManagerDock");
  layer_control_dock_->setAllowedAreas(Qt::LeftDockWidgetArea |
                                       Qt::RightDockWidgetArea);

  layer_control_ =
      new LayerControlWidget(view_, settings_, layer_control_dock_);
  layer_control_dock_->setWidget(layer_control_);
  addDockWidget(Qt::LeftDockWidgetArea, layer_control_dock_);

  routing_panel_ = new RoutingWidget(view_, settings_, view_);
  routing_panel_->move(20, 530);

  favorites_panel_ = new FavoritesWidget(view_, settings_, view_);
  favorites_panel_->move(view_->width() - 270, 20);

  coordinate_points_panel_ =
      new CoordinatePointsWidget(view_, settings_, view_);
  coordinate_points_panel_->move(290, 330);

  load_progress_ = new LoadingProgressWidget(view_);
  load_progress_->move(view_->width() / 2 - 150, view_->height() / 2 - 50);

  topology_validator_panel_ =
      new TopologyValidatorWidget(view_, settings_, view_);
  topology_validator_panel_->move(20, 20);
  topology_validator_panel_->setVisible(false);

  // Apply visibility from settings
  layer_control_dock_->setVisible(settings_.layer_manager_visible);
  routing_panel_->setVisible(settings_.routing_visible);
  favorites_panel_->setVisible(settings_.favorites_visible);
  coordinate_points_panel_->setVisible(settings_.coordinate_points_visible);
}

void MainWindow::ToggleWidgetVisibility(QWidget* widget, bool visible) {
  if (widget) {
    widget->setVisible(visible);
  }
}

void MainWindow::SetupToolbar() {
  QToolBar* toolbar = addToolBar("Main");
  addToolBar(Qt::TopToolBarArea, toolbar);

  load_action_ = toolbar->addAction(tr("Load .xodr"));
  connect(load_action_, &QAction::triggered, this, &MainWindow::HandleLoadMap);

  compare_action_ = toolbar->addAction(tr("Compare .xodr"));
  compare_action_->setEnabled(false);
  connect(compare_action_, &QAction::triggered, this,
          &MainWindow::HandleCompareMap);

  save_as_action_ = toolbar->addAction(tr("Save As .xodr"));
  save_as_action_->setEnabled(false);
  connect(save_as_action_, &QAction::triggered, this,
          &MainWindow::HandleSaveMapAs);

  toolbar->addSeparator();

  // Widget visibility menu
  panels_menu_ = new QMenu(tr("Panels"), this);
  auto addToggle = [&](const QString& name, QWidget* w) {
    QAction* act = panels_menu_->addAction(name);
    act->setCheckable(true);
    act->setChecked(!w->isHidden());
    connect(act, &QAction::toggled, this,
            [this, w](bool checked) { ToggleWidgetVisibility(w, checked); });

    // Synchronize UI state when widget is shown/hidden externally
    if (auto* panel = qobject_cast<FloatingPanelWidget*>(w)) {
      connect(panel, &FloatingPanelWidget::VisibilityChanged, act,
              &QAction::setChecked);
    }
  };

  QAction* layerAct = layer_control_dock_->toggleViewAction();
  layerAct->setText(tr("Layer Manager"));
  panels_menu_->addAction(layerAct);

  addToggle(tr("Routing"), routing_panel_);
  addToggle(tr("Favorites"), favorites_panel_);
  addToggle(tr("Coordinate Inputs"), coordinate_points_panel_);
  addToggle(tr("Topology Validator"), topology_validator_panel_);

  panels_btn_ = new QToolButton(this);
  panels_btn_->setText(tr("Windows"));
  panels_btn_->setMenu(panels_menu_);
  panels_btn_->setPopupMode(QToolButton::InstantPopup);
  panels_btn_->setStyleSheet("padding: 2px 10px; font-weight: bold;");
  toolbar->addWidget(panels_btn_);

  toolbar->addSeparator();

  // Language selection menu
  lang_menu_ = new QMenu(tr("Language"), this);
  QAction* zhAction = lang_menu_->addAction("简体中文");
  QAction* enAction = lang_menu_->addAction("English");
  connect(zhAction, &QAction::triggered, this,
          [this]() { ChangeLanguage("zh_CN"); });
  connect(enAction, &QAction::triggered, this,
          [this]() { ChangeLanguage("en_US"); });

  lang_btn_ = new QToolButton(this);
  lang_btn_->setText(tr("Language"));
  lang_btn_->setMenu(lang_menu_);
  lang_btn_->setPopupMode(QToolButton::InstantPopup);
  lang_btn_->setStyleSheet("padding: 2px 10px; font-weight: bold;");
  toolbar->addWidget(lang_btn_);

  toolbar->addSeparator();

  measure_action_ = toolbar->addAction(tr("Measure"));
  measure_action_->setCheckable(true);

  view_mode_action_ = toolbar->addAction(tr("2D/3D"));
  view_mode_action_->setCheckable(true);
  view_mode_action_->setToolTip(
      tr("Toggle between 2D (Overhead) and 3D views"));
  connect(view_mode_action_, &QAction::toggled, this,
          &MainWindow::HandleViewModeToggle);

  copy_map_name_action_ = toolbar->addAction(tr("Copy Map Name"));
  copy_map_name_action_->setToolTip(
      tr("Copy current map file name without extension"));
  connect(copy_map_name_action_, &QAction::triggered, this,
          &MainWindow::HandleCopyMapBaseName);

  toolbar->addSeparator();

  screenshot_action_ = toolbar->addAction(tr("Screenshot"));
  screenshot_action_->setToolTip(tr("Save 3D view screenshot"));
  connect(screenshot_action_, &QAction::triggered, this,
          &MainWindow::HandleScreenshot);

  stats_action_ = toolbar->addAction(tr("Map Stats"));
  stats_action_->setToolTip(tr("Show Map Statistics"));
  stats_action_->setEnabled(false);
  connect(stats_action_, &QAction::triggered, this,
          &MainWindow::HandleShowStats);

  toolbar->addSeparator();

  mcp_action_ = toolbar->addAction(tr("MCP Server"));
  mcp_action_->setCheckable(true);
  mcp_action_->setToolTip(tr("Start/Stop HTTP MCP Server"));
  connect(mcp_action_, &QAction::triggered, this,
          &MainWindow::HandleToggleMcpServer);

  toolbar->addWidget(BuildCoordinateTools());
}

MainWindow::~MainWindow() {
  StopMcpServer();
}

void MainWindow::StartMcpStdio() {
  if (!mcp_server_) {
    mcp_server_ = new geoviewer::mcp::McpServer(
        view_, [this](const QString& path) { StartMapLoad(path); }, this);
  }
  mcp_server_->StartStdio();
  if (status_) {
    status_->showMessage(tr("MCP Server running in stdio mode"));
  }
}

bool MainWindow::StartMcpHttp(uint16_t port) {
  if (!mcp_server_) {
    mcp_server_ = new geoviewer::mcp::McpServer(
        view_, [this](const QString& path) { StartMapLoad(path); }, this);
  }
  bool ok = mcp_server_->StartHttp(port);
  if (ok) {
    if (mcp_action_) mcp_action_->setChecked(true);
    if (status_) {
      status_->showMessage(
          tr("MCP HTTP Server listening on port %1").arg(port));
    }
  } else {
    QMessageBox::warning(this, tr("MCP Server Error"),
                         tr("Failed to start HTTP server on port %1").arg(port));
  }
  return ok;
}

void MainWindow::StopMcpServer() {
  if (mcp_server_) {
    mcp_server_->StopAll();
  }
  if (mcp_action_) mcp_action_->setChecked(false);
  if (status_) {
    status_->showMessage(tr("MCP Server stopped"));
  }
}

void MainWindow::HandleToggleMcpServer() {
  if (mcp_server_ && mcp_server_->IsHttpRunning()) {
    StopMcpServer();
  } else {
    bool ok = false;
    int port = QInputDialog::getInt(this, tr("Start MCP Server"),
                                    tr("Port number:"), 8080, 1024, 65535, 1, &ok);
    if (ok) {
      StartMcpHttp(static_cast<uint16_t>(port));
    } else if (mcp_action_) {
      mcp_action_->setChecked(false);
    }
  }
}

QWidget* MainWindow::BuildCoordinateTools() {
  QWidget* container = new QWidget(this);
  QHBoxLayout* layout = new QHBoxLayout(container);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);

  coord_mode_combo_ = new QComboBox(container);
  coord_mode_combo_->addItem(tr("WGS84 (lon, lat)"));
  coord_mode_combo_->addItem(tr("Local (x, y)"));
  coord_mode_combo_->setFixedWidth(140);
  connect(coord_mode_combo_,
          QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this](int index) {
            if (!wgs84_mode_allowed_ && index == 0) {
              const QSignalBlocker blocker(coord_mode_combo_);
              coord_mode_combo_->setCurrentIndex(1);
              return;
            }
            coord_mode_ =
                (index == 0) ? CoordinateMode::kWGS84 : CoordinateMode::kLocal;
            coordinate_points_panel_->SetCoordinateMode(coord_mode_);
            view_->SetCoordinateMode(coord_mode_);
            RetranslateUi();
            HandleSettingsChanged();
          });

  layout->addWidget(coord_mode_combo_);

  jump_label_ = new QLabel(tr("Jump to (lon,lat,alt):"), container);
  layout->addWidget(jump_label_);
  jump_to_coords_edit_ = new QLineEdit(container);
  jump_to_coords_edit_->setPlaceholderText(tr("lon,lat,alt"));
  jump_to_coords_edit_->setFixedWidth(180);
  connect(jump_to_coords_edit_, &QLineEdit::returnPressed, this,
          &MainWindow::HandleJumpToCoords);
  layout->addWidget(jump_to_coords_edit_);

  layout->addStretch();
  return container;
}

void MainWindow::SetupConnections() {
  connect(view_, &GeoViewerWidget::ShowXmlRequested, this,
          &MainWindow::HandleShowXml);
  connect(view_, &GeoViewerWidget::ElementSelected, layer_control_,
          &LayerControlWidget::SelectElement);
  connect(layer_control_, &LayerControlWidget::ItemHovered, view_,
          &GeoViewerWidget::HighlightElement);
  connect(view_, &GeoViewerWidget::AddFavoriteRequested, favorites_panel_,
          &FavoritesWidget::AddFavorite);
  connect(view_, &GeoViewerWidget::RoutingStartRequested, routing_panel_,
          &RoutingWidget::SetStartLane);
  connect(view_, &GeoViewerWidget::RoutingEndRequested, routing_panel_,
          &RoutingWidget::SetEndLane);
  connect(view_, &GeoViewerWidget::HoverInfoChanged, this,
          &MainWindow::HandleHoverInfo);
  connect(view_, &GeoViewerWidget::MeasureModeChanged, measure_action_,
          &QAction::setChecked);
  connect(measure_action_, &QAction::toggled, view_,
          &GeoViewerWidget::SetMeasureMode);
  connect(view_, &GeoViewerWidget::TotalDistanceChanged, this,
          [this](double dist) {
            statusBar()->showMessage(
                tr("Total Distance: %1 m").arg(dist, 0, 'f', 2));
          });

  connect(view_, &GeoViewerWidget::MapDiffApplied, this, [this]() {
    status_->showMessage(
        tr("Map comparison complete. Differences highlighted."));
  });

  connect(
      view_, &GeoViewerWidget::ViewModeChanged, this,
      [this](CameraController::ViewMode mode) {
        const QSignalBlocker blocker(view_mode_action_);
        view_mode_action_->setChecked(mode == CameraController::ViewMode::k2D);
        view_mode_action_->setText(
            mode == CameraController::ViewMode::k2D ? tr("2D") : tr("3D"));
      });

  connect(map_loader_, &AsyncMapLoader::ProgressTextChanged, this,
          [this](const QString& text) {
            if (load_progress_) {
              load_progress_->SetText(text);
            }
            status_->showMessage(text);
          });

  connect(map_loader_, &AsyncMapLoader::Finalizing, this, [this]() {
    const QString text = tr("Finalizing map data and uploading to GPU...");
    if (load_progress_) {
      load_progress_->SetText(text);
    }
    status_->showMessage(text);
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  });

  connect(map_loader_, &AsyncMapLoader::Finished, this, [this](bool success) {
    MapSceneData data = map_loader_->TakeResult();
    if (!success || !data.IsValid()) {
      if (load_progress_) {
        load_progress_->HideLoading();
      }
      status_->showMessage(tr("Failed to load map."));
      return;
    }

    current_map_path_ = pending_map_path_;

    if (compare_action_) {
      compare_action_->setEnabled(true);
    }
    if (stats_action_) {
      stats_action_->setEnabled(true);
    }

    qDebug() << "Junction grouping:" << data.junction_grouping.groups.size()
             << "physical groups from"
             << data.junction_grouping.junctions.size()
             << "OpenDRIVE junctions.";

    road_id_to_mesh_cache_ = std::move(data.road_id_to_mesh);
    view_->ClearRefLineCache();
    view_->SetMapAndMesh(data.map, std::move(data.mesh),
                         &data.junction_grouping, data.routing_graph);
    view_->SetGeoreferenceAvailable(data.IsWgs84ModeAvailable());
    ApplyCoordinateModePolicy(data.IsWgs84ModeAvailable());

    if (load_progress_) {
      load_progress_->HideLoading();
    }

    status_->showMessage(data.IsWgs84ModeAvailable()
                             ? tr("Map ready. Building layer tree...")
                             : tr("Map ready in local coordinates mode. "
                                  "Building layer tree..."));

    UpdateWindowTitle();

    // Emit map loaded signal to EventBus to notify decoupled listeners (e.g.
    // LayerControlWidget)
    emit geoviewer::logic::EventBus::Instance().MapLoaded(current_map_path_,
                                                          true);

    QTimer::singleShot(0, this, [this]() {
      if (wgs84_mode_allowed_) {
        status_->showMessage(tr("Map loaded successfully."));
      } else {
        status_->showMessage(
            tr("Map loaded successfully. Invalid georeference: local "
               "coordinate mode only."));
      }
    });
  });

  connect(view_, &GeoViewerWidget::SceneReset, routing_panel_,
          &RoutingWidget::Clear);
  connect(view_, &GeoViewerWidget::SceneReset, coordinate_points_panel_,
          &CoordinatePointsWidget::Clear);
  connect(view_, &GeoViewerWidget::SceneReset, favorites_panel_,
          &FavoritesWidget::Clear);
  connect(view_, &GeoViewerWidget::SceneReset, topology_validator_panel_,
          &TopologyValidatorWidget::Clear);

  connect(view_, &GeoViewerWidget::ViewResized, this, [this]() {
    if (favorites_panel_) {
      favorites_panel_->move(view_->width() - favorites_panel_->width() - 20,
                             20);
    }
    if (routing_panel_) {
      routing_panel_->move(20, view_->height() - routing_panel_->height() - 20);
    }
  });

  // Settings persistence connections
  connect(layer_control_, &LayerControlWidget::SettingsChanged, this,
          &MainWindow::HandleSettingsChanged);
  connect(routing_panel_, &RoutingWidget::SettingsChanged, this,
          &MainWindow::HandleSettingsChanged);
  connect(favorites_panel_, &FavoritesWidget::SettingsChanged, this,
          &MainWindow::HandleSettingsChanged);
  connect(coordinate_points_panel_, &CoordinatePointsWidget::SettingsChanged,
          this, &MainWindow::HandleSettingsChanged);
  connect(layer_control_dock_, &QDockWidget::visibilityChanged, this,
          &MainWindow::HandleSettingsChanged);
}

void MainWindow::StartMapLoad(const QString& path) {
  if (map_loader_->IsRunning()) return;

  is_modified_ = false;
  if (save_as_action_) {
    save_as_action_->setEnabled(false);
  }

  if (compare_action_) {
    compare_action_->setEnabled(false);
  }

  if (load_progress_) {
    load_progress_->move(view_->width() / 2 - load_progress_->width() / 2,
                         view_->height() / 2 - load_progress_->height() / 2);
    load_progress_->ShowLoading();
  }

  status_->showMessage(tr("Loading map and generating mesh..."));
  pending_map_path_ = path;
  map_loader_->Start(path);
}

void MainWindow::UpdateWindowTitle() {
  if (current_map_path_.isEmpty()) {
    setWindowTitle("OpenDriveViewer");
  } else {
    setWindowTitle(QString("OpenDriveViewer - %1")
                       .arg(QFileInfo(current_map_path_).fileName()));
  }
}

void MainWindow::ApplyCoordinateModePolicy(bool georeference_valid) {
  wgs84_mode_allowed_ = IsWgs84ModeAllowed(georeference_valid);
  coord_mode_ = ResolveDefaultCoordinateMode(georeference_valid);

  if (coord_mode_combo_) {
    const QSignalBlocker blocker(coord_mode_combo_);
    coord_mode_combo_->setItemData(0, wgs84_mode_allowed_ ? QVariant() : 0,
                                   Qt::UserRole - 1);
    coord_mode_combo_->setEnabled(wgs84_mode_allowed_);
    coord_mode_combo_->setCurrentIndex(
        coord_mode_ == CoordinateMode::kWGS84 ? 0 : 1);
  }

  coordinate_points_panel_->SetCoordinateMode(coord_mode_);
  view_->SetCoordinateMode(coord_mode_);
  RetranslateUi();
}
void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
  if (event->mimeData()->hasUrls()) {
    QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl& url : urls) {
      if (url.isLocalFile()) {
        QString path = url.toLocalFile();
        if (path.endsWith(".xodr", Qt::CaseInsensitive) ||
            path.endsWith(".xml", Qt::CaseInsensitive)) {
          event->acceptProposedAction();
          return;
        }
      }
    }
  }
}

void MainWindow::dropEvent(QDropEvent* event) {
  if (event->mimeData()->hasUrls()) {
    QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl& url : urls) {
      if (url.isLocalFile()) {
        QString path = url.toLocalFile();
        if (path.endsWith(".xodr", Qt::CaseInsensitive) ||
            path.endsWith(".xml", Qt::CaseInsensitive)) {
          StartMapLoad(path);
          event->acceptProposedAction();
          return;
        }
      }
    }
  }
}

void MainWindow::HandleSettingsChanged() {
  SaveSettingsToStruct();
  geoviewer::core::SettingsPersistence::Save(settings_,
                                             qApp->applicationDirPath());
}

void MainWindow::SaveSettingsToStruct() {
  if (layer_control_dock_)
    settings_.layer_manager_visible = !layer_control_dock_->isHidden();
  if (routing_panel_) settings_.routing_visible = !routing_panel_->isHidden();
  if (favorites_panel_)
    settings_.favorites_visible = !favorites_panel_->isHidden();
  if (coordinate_points_panel_)
    settings_.coordinate_points_visible = !coordinate_points_panel_->isHidden();

  if (view_) {
    for (auto& [layer, visibility] : settings_.global_layer_visibility) {
      visibility = view_->IsLayerVisible(layer);
    }
  }
  settings_.coordinate_mode = coord_mode_;
}

void MainWindow::HandleViewModeToggle(bool is_2d) {
  if (view_) {
    view_->SetViewMode(is_2d ? CameraController::ViewMode::k2D
                             : CameraController::ViewMode::k3D);
  }
}

void MainWindow::HandleCompareMap() {
  QString path =
      QFileDialog::getOpenFileName(this, tr("Compare with OpenDRIVE Map"),
                                   QString(), tr("OpenDrive Maps (*.xodr)"));
  if (path.isEmpty()) return;

  status_->showMessage(tr("Comparing maps..."));
  view_->CompareWithMap(path);
}

void MainWindow::HandleShowXml(const geoviewer::ui::XmlTarget& target,
                               const QString& xml_text) {
  if (!xml_editor_) {
    xml_editor_ = new geoviewer::ui::XmlEditorDialog(this);
    connect(xml_editor_, &geoviewer::ui::XmlEditorDialog::XmlSaved, this,
            &MainWindow::HandleXmlSaved);
  }
  xml_editor_->SetXml(xml_text, target);
  xml_editor_->show();
  xml_editor_->raise();
  xml_editor_->activateWindow();
}

void MainWindow::HandleXmlSaved(const geoviewer::ui::XmlTarget& target,
                                const QString& xml_text) {
  auto map = view_->GetMap();
  if (!map) return;

  pugi::xml_document parsed_doc;
  pugi::xml_parse_result result =
      parsed_doc.load_string(xml_text.toStdString().c_str());
  if (!result) return;  // already validated by dialog

  pugi::xml_node new_node = parsed_doc.first_child();
  pugi::xml_node old_node;

  if (target.type == geoviewer::ui::XmlTargetType::kRoad) {
    for (pugi::xml_node node :
         map->xml_doc.child("OpenDRIVE").children("road")) {
      if (node.attribute("id").value() == target.road_id) {
        old_node = node;
        break;
      }
    }
  } else if (target.type == geoviewer::ui::XmlTargetType::kJunction) {
    for (pugi::xml_node node :
         map->xml_doc.child("OpenDRIVE").children("junction")) {
      if (node.attribute("id").value() == target.element_id) {
        old_node = node;
        break;
      }
    }
  } else if (target.type == geoviewer::ui::XmlTargetType::kLane) {
    pugi::xml_node road_node;
    for (pugi::xml_node node :
         map->xml_doc.child("OpenDRIVE").children("road")) {
      if (node.attribute("id").value() == target.road_id) {
        road_node = node;
        break;
      }
    }
    if (road_node) {
      pugi::xml_node lanes_node = road_node.child("lanes");
      pugi::xml_node sec_node;
      for (pugi::xml_node s_node : lanes_node.children("laneSection")) {
        if (std::abs(s_node.attribute("s").as_double() - target.lane_s0) <
            1e-3) {
          sec_node = s_node;
          break;
        }
      }
      if (sec_node) {
        for (pugi::xml_node side_node :
             {sec_node.child("left"), sec_node.child("center"),
              sec_node.child("right")}) {
          if (!side_node) continue;
          for (pugi::xml_node lane_node : side_node.children("lane")) {
            if (lane_node.attribute("id").as_int() == target.lane_id) {
              old_node = lane_node;
              break;
            }
          }
          if (old_node) break;
        }
      }
    }
  } else if (target.type == geoviewer::ui::XmlTargetType::kObject) {
    pugi::xml_node road_node;
    for (pugi::xml_node node :
         map->xml_doc.child("OpenDRIVE").children("road")) {
      if (node.attribute("id").value() == target.road_id) {
        road_node = node;
        break;
      }
    }
    if (road_node) {
      pugi::xml_node objs_node = road_node.child("objects");
      for (pugi::xml_node obj_node : objs_node.children("object")) {
        if (obj_node.attribute("id").value() == target.element_id) {
          old_node = obj_node;
          break;
        }
      }
    }
  } else if (target.type == geoviewer::ui::XmlTargetType::kSignal) {
    pugi::xml_node road_node;
    for (pugi::xml_node node :
         map->xml_doc.child("OpenDRIVE").children("road")) {
      if (node.attribute("id").value() == target.road_id) {
        road_node = node;
        break;
      }
    }
    if (road_node) {
      pugi::xml_node sigs_node = road_node.child("signals");
      for (pugi::xml_node sig_node : sigs_node.children("signal")) {
        if (sig_node.attribute("id").value() == target.element_id) {
          old_node = sig_node;
          break;
        }
      }
    }
  }

  if (old_node) {
    pugi::xml_node parent = old_node.parent();
    parent.insert_copy_before(new_node, old_node);
    parent.remove_child(old_node);

    is_modified_ = true;
    if (save_as_action_) {
      save_as_action_->setEnabled(true);
    }

    TriggerMeshUpdate(target.road_id);
  }
}

void MainWindow::TriggerMeshUpdate(const std::string& target_road_id) {
  auto map = view_->GetMap();
  if (!map) return;

  status_->showMessage(tr("Rebuilding road mesh in background..."));

  QString temp_path = QDir::tempPath() + "/geoviewer_temp_update.xodr";
  if (!map->xml_doc.save_file(temp_path.toStdString().c_str())) {
    status_->showMessage(
        tr("Failed to save temporary map file for recomputation."));
    return;
  }

  if (xml_editor_) {
    xml_editor_->setEnabled(false);
  }

  std::string std_temp_path = temp_path.toStdString();

  // Move the mesh cache into the background thread to avoid an expensive copy.
  // The merge of all per-road meshes is O(total_vertices) and must not run on
  // the GUI thread.
  auto mesh_cache = std::move(road_id_to_mesh_cache_);

  geoviewer::utility::ThreadPool::Instance().Enqueue(
      [this, std_temp_path, target_road_id,
       mesh_cache = std::move(mesh_cache)]() mutable {
        MapSceneData data;

        try {
          data.map = std::make_shared<odr::OpenDriveMap>(std_temp_path);
          data.georeference_valid = true;
        } catch (...) {
          data.georeference_valid = false;
        }

        if (data.map) {
          data.junction_grouping = JunctionClusterUtil::Analyze(*data.map);
          data.routing_graph = std::make_shared<odr::RoutingGraph>(
              data.map->get_routing_graph());

          if (!target_road_id.empty() &&
              data.map->id_to_road.find(target_road_id) !=
                  data.map->id_to_road.end()) {
            // Single-road update: rebuild only the target road mesh, then merge
            // all roads in the background.
            mesh_cache[target_road_id] =
                geoviewer::core::GenerateSingleRoadMesh(
                    data.map->id_to_road.at(target_road_id), 0.75);
            data.mesh = geoviewer::core::MergeRoadMeshes(mesh_cache);
          } else if (!target_road_id.empty()) {
            // Fallback: target road was not found, rebuild all roads
            // sequentially to avoid ThreadPool starvation.
            for (const auto& [r_id, road] : data.map->id_to_road) {
              mesh_cache[r_id] =
                  geoviewer::core::GenerateSingleRoadMesh(road, 0.75);
            }
            data.mesh = geoviewer::core::MergeRoadMeshes(mesh_cache);
          } else {
            // Junction edit: no road meshes changed. Merge existing cache.
            data.mesh = geoviewer::core::MergeRoadMeshes(mesh_cache);
          }
        }

        QMetaObject::invokeMethod(
            this,
            [this, data = std::move(data), mesh_cache = std::move(mesh_cache),
             target_road_id, std_temp_path]() mutable {
              QFile::remove(QString::fromStdString(std_temp_path));

              if (xml_editor_) {
                xml_editor_->setEnabled(true);
              }

              if (!data.IsValid()) {
                status_->showMessage(tr("Rebuild failed: Invalid map data."));
                // Restore the cache even on failure so it is not lost.
                road_id_to_mesh_cache_ = std::move(mesh_cache);
                return;
              }

              // Restore the (possibly updated) cache back to the member.
              road_id_to_mesh_cache_ = std::move(mesh_cache);

              if (!target_road_id.empty()) {
                view_->ClearRefLineCacheForRoad(target_road_id);
              }

              view_->SetMapAndMesh(data.map, std::move(data.mesh),
                                   &data.junction_grouping, data.routing_graph);
              view_->SetGeoreferenceAvailable(data.IsWgs84ModeAvailable());
              ApplyCoordinateModePolicy(data.IsWgs84ModeAvailable());

              emit geoviewer::logic::EventBus::Instance().MapLoaded(
                  current_map_path_, true);

              status_->showMessage(
                  tr("Map updated and mesh rebuilt successfully."));
            },
            Qt::QueuedConnection);
      });
}

void MainWindow::HandleSaveMapAs() {
  auto map = view_->GetMap();
  if (!map) return;

  QString file_path =
      QFileDialog::getSaveFileName(this, tr("Save Map As"), current_map_path_,
                                   tr("OpenDRIVE Files (*.xodr *.xml)"));

  if (file_path.isEmpty()) return;

  if (map->xml_doc.save_file(file_path.toStdString().c_str())) {
    is_modified_ = false;
    if (save_as_action_) {
      save_as_action_->setEnabled(false);
    }
    current_map_path_ = file_path;
    UpdateWindowTitle();
    status_->showMessage(tr("Map saved successfully to %1").arg(file_path));
  } else {
    QMessageBox::critical(this, tr("Save Error"),
                          tr("Failed to save map file."));
  }
}
