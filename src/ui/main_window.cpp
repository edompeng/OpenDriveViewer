#include "src/ui/main_window.h"
#include <QApplication>
#include <QCheckBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMimeData>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolBar>
#include <QTranslator>
#include <QWidget>
#include "src/core/coordinate_mode_policy.h"
#include "src/logic/input_parsing.h"
#include "src/ui/widgets/layer_control_widget.h"

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  translator_ = new QTranslator(this);
  // Initial language setup based on system locale
  QString locale = QLocale::system().name();
  if (locale.isEmpty()) locale = "zh_CN";
  if (!translator_->load(":/i18n/geoviewer_" + locale + ".qm")) {
    if (!translator_->load(":/i18n/geoviewer_zh_CN.qm")) {
      qWarning() << "Failed to load default translation file";
    }
  }
  qApp->installTranslator(translator_);

  view_ = new GeoViewerWidget(this);
  setCentralWidget(view_);
  map_loader_ =
      new AsyncMapLoader(std::make_unique<OpenDriveMapSceneLoader>(), this);

  status_ = statusBar();
  SetupPanels();
  SetupToolbar();
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
                                 const QString& name_str) {
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

  layer_control_ = new LayerControlWidget(view_, layer_control_dock_);
  layer_control_dock_->setWidget(layer_control_);
  addDockWidget(Qt::LeftDockWidgetArea, layer_control_dock_);
  layer_control_dock_->show();

  routing_panel_ = new RoutingWidget(view_, view_);
  routing_panel_->move(20, 530);
  routing_panel_->hide();

  favorites_panel_ = new FavoritesWidget(view_, view_);
  favorites_panel_->move(view_->width() - 270, 20);
  favorites_panel_->hide();

  coordinate_points_panel_ = new CoordinatePointsWidget(view_, view_);
  coordinate_points_panel_->move(290, 330);
  coordinate_points_panel_->hide();

  load_progress_ = new LoadingProgressWidget(view_);
  load_progress_->move(view_->width() / 2 - 150, view_->height() / 2 - 50);
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

  toolbar->addSeparator();

  // Widget visibility menu
  panels_menu_ = new QMenu(tr("Panels"), this);
  auto addToggle = [&](const QString& name, QWidget* w) {
    QAction* act = panels_menu_->addAction(name);
    act->setCheckable(true);
    act->setChecked(!w->isHidden());
    connect(act, &QAction::toggled, this,
            [this, w](bool checked) { ToggleWidgetVisibility(w, checked); });
  };
  
  QAction* layerAct = layer_control_dock_->toggleViewAction();
  layerAct->setText(tr("Layer Manager"));
  panels_menu_->addAction(layerAct);

  addToggle(tr("Routing"), routing_panel_);
  addToggle(tr("Favorites"), favorites_panel_);
  addToggle(tr("Coordinate Inputs"), coordinate_points_panel_);

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
  copy_map_name_action_ = toolbar->addAction(tr("Copy Map Name"));
  copy_map_name_action_->setToolTip(
      tr("Copy current map file name without extension"));
  connect(copy_map_name_action_, &QAction::triggered, this,
          &MainWindow::HandleCopyMapBaseName);
  toolbar->addWidget(BuildCoordinateTools());
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

    qDebug() << "Junction grouping:" << data.junction_grouping.groups.size()
             << "physical groups from"
             << data.junction_grouping.junctions.size()
             << "OpenDRIVE junctions.";

    view_->SetMapAndMesh(data.map, std::move(data.mesh),
                         &data.junction_grouping);
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

    QTimer::singleShot(0, this, [this]() {
      layer_control_->UpdateTree();
      if (wgs84_mode_allowed_) {
        status_->showMessage(tr("Map loaded successfully."));
      } else {
        status_->showMessage(
            tr("Map loaded successfully. Invalid georeference: local "
               "coordinate mode only."));
      }
    });
  });
}

void MainWindow::StartMapLoad(const QString& path) {
  if (map_loader_->IsRunning()) return;

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
