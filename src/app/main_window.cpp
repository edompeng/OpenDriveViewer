#include "src/app/main_window.h"
#include <QApplication>
#include <QCheckBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QToolBar>
#include <QTranslator>
#include <QWidget>
#include "src/app/layer_control_widget.h"
#include "src/utility/input_parsing.h"

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  translator_ = new QTranslator(this);

  view_ = new GeoViewerWidget(this);
  setCentralWidget(view_);
  map_loader_ =
      new AsyncMapLoader(std::make_unique<OpenDriveMapSceneLoader>(), this);

  status_ = statusBar();
  SetupPanels();
  SetupToolbar();
  SetupConnections();

  // Set window title
  setWindowTitle("OpenDriveViewer");
}

void MainWindow::HandleLoadMap() {
  QString path =
      QFileDialog::getOpenFileName(this, tr("Open OpenDRIVE file"), QString(),
                                   tr("OpenDRIVE Files (*.xodr *.xml)"));
  if (path.isEmpty()) return;
  StartMapLoad(path);
}

void MainWindow::HandleHoverInfo(double lon, double lat, double alt,
                                 const QString& typeStr, const QString& idStr,
                                 const QString& nameStr) {
  QString statusText = tr("Coords: %1,%2,%3")
                           .arg(lon, 0, 'f', 8)
                           .arg(lat, 0, 'f', 8)
                           .arg(alt, 0, 'f', 2);

  if (!typeStr.isEmpty()) {
    statusText += tr(" | Type: %1 | ID: %2").arg(typeStr).arg(idStr);
    if (!nameStr.isEmpty()) {
      statusText += QString(" | %1").arg(nameStr);
    }
  }

  status_->showMessage(statusText);
}

void MainWindow::HandleJumpToCoords() {
  const auto target = CoordinateInputParser::ParseJumpLocation(
      jump_to_coords_edit_->text().toStdString());
  if (!target.has_value()) {
    status_->showMessage(
        tr("Please enter longitude, latitude (optional altitude), separated by "
           "comma or space"));
    return;
  }

  view_->JumpToLocation(target->lon, target->lat, target->alt);
  status_->showMessage(tr("Jumped to: %1, %2, %3")
                           .arg(target->lon, 0, 'f', 8)
                           .arg(target->lat, 0, 'f', 8)
                           .arg(target->alt, 0, 'f', 2));
}

void MainWindow::ChangeLanguage(const QString& locale) {
  qApp->removeTranslator(translator_);
  QString path = ":/i18n/geoviewer_" + locale;
  if (translator_->load(path)) {
    qApp->installTranslator(translator_);
    qDebug() << "Loaded translation:" << path;
  } else {
    qDebug() << "Failed to load translation:" << path
             << ", falling back to zh_CN";
    if (translator_->load(":/i18n/geoviewer_zh_CN")) {
      qApp->installTranslator(translator_);
      qDebug() << "Loaded fallback translation: :/i18n/geoviewer_zh_CN";
    } else {
      qDebug() << "CRITICAL: Failed to load fallback translation!";
    }
  }
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
  panels_btn_->setText(tr("Windows"));
  lang_btn_->setText(tr("Language"));
  measure_action_->setText(tr("Measure"));
  jump_label_->setText(tr("Jump to (lon,lat,alt):"));
  jump_to_coords_edit_->setPlaceholderText(tr("lon,lat,alt"));

  // Update Panel menu
  panels_menu_->setTitle(tr("Panels"));
  auto actions = panels_menu_->actions();
  if (actions.size() >= 5) {
    actions[0]->setText(tr("Layer Control"));
    actions[1]->setText(tr("OpenSCENARIO"));
    actions[2]->setText(tr("Routing"));
    actions[3]->setText(tr("Favorites"));
    actions[4]->setText(tr("Coordinate Inputs"));
  }

  // Update Layer checkboxes
  const char* layer_labels[] = {"Lanes",     "Lines",         "Marks",
                                "Objects",   "Signal Lights", "Signals",
                                "Ref Lines", "Junctions"};
  for (size_t i = 0; i < layer_checkboxes_.size() && i < 8; ++i) {
    layer_checkboxes_[i]->setText(tr(layer_labels[i]));
  }

  // Update Language menu
  lang_menu_->setTitle(tr("Language"));
}

void MainWindow::resizeEvent(QResizeEvent* event) {
  QMainWindow::resizeEvent(event);
  if (favorites_panel_) {
    favorites_panel_->move(width() - favorites_panel_->width() - 20, 20);
  }
}

void MainWindow::SetupPanels() {
  layer_control_ = new LayerControlWidget(view_, view_);
  layer_control_->move(20, 20);
  layer_control_->show();

  open_scenario_panel_ = new OpenScenarioWidget(view_, view_);
  open_scenario_panel_->move(290, 20);
  open_scenario_panel_->hide();

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
  addToggle(tr("Layer Control"), layer_control_);
  addToggle(tr("OpenSCENARIO"), open_scenario_panel_);
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
  toolbar->addWidget(BuildCoordinateTools());
}

QWidget* MainWindow::BuildCoordinateTools() {
  QWidget* container = new QWidget(this);
  QHBoxLayout* layout = new QHBoxLayout(container);

  layout->addSpacing(20);
  jump_label_ = new QLabel(tr("Jump to (lon,lat,alt):"), container);
  layout->addWidget(jump_label_);
  jump_to_coords_edit_ = new QLineEdit(container);
  jump_to_coords_edit_->setPlaceholderText(tr("lon,lat,alt"));
  jump_to_coords_edit_->setFixedWidth(180);
  connect(jump_to_coords_edit_, &QLineEdit::returnPressed, this,
          &MainWindow::HandleJumpToCoords);
  layout->addWidget(jump_to_coords_edit_);

  QWidget* layers_widget = new QWidget(container);
  QHBoxLayout* layers_layout = new QHBoxLayout(layers_widget);
  layers_layout->setContentsMargins(0, 0, 0, 0);

  struct LayerToggleSpec {
    const char* label;
    LayerType type;
    bool default_visible;
  };

  const LayerToggleSpec layer_specs[] = {
      {"Lanes", LayerType::kLanes, true},
      {"Lines", LayerType::kLaneLines, true},
      {"Marks", LayerType::kRoadmarks, true},
      {"Objects", LayerType::kObjects, false},
      {"Signal Lights", LayerType::kSignalLights, true},
      {"Signals", LayerType::kSignalSigns, false},
      {"Ref Lines", LayerType::kReferenceLines, true},
      {"Junctions", LayerType::kJunctions, false},
  };

  layer_checkboxes_.clear();
  for (const auto& spec : layer_specs) {
    auto* checkbox = new QCheckBox(tr(spec.label), layers_widget);
    checkbox->setChecked(spec.default_visible);
    view_->SetLayerVisible(spec.type, spec.default_visible);
    connect(checkbox, &QCheckBox::toggled, this, [this, spec](bool checked) {
      view_->SetLayerVisible(spec.type, checked);
    });
    layers_layout->addWidget(checkbox);
    layer_checkboxes_.push_back(checkbox);
  }

  layout->addWidget(layers_widget);
  return container;
}

void MainWindow::SetupConnections() {
  connect(view_, &GeoViewerWidget::elementSelected, layer_control_,
          &LayerControlWidget::SelectElement);
  connect(layer_control_, &LayerControlWidget::itemHovered, view_,
          &GeoViewerWidget::HighlightElement);
  connect(view_, &GeoViewerWidget::addFavoriteRequested, favorites_panel_,
          &FavoritesWidget::AddFavorite);
  connect(view_, &GeoViewerWidget::routingStartRequested, routing_panel_,
          &RoutingWidget::SetStartLane);
  connect(view_, &GeoViewerWidget::routingEndRequested, routing_panel_,
          &RoutingWidget::SetEndLane);
  connect(view_, &GeoViewerWidget::hoverInfoChanged, this,
          &MainWindow::HandleHoverInfo);
  connect(view_, &GeoViewerWidget::measureModeChanged, measure_action_,
          &QAction::setChecked);
  connect(measure_action_, &QAction::toggled, view_,
          &GeoViewerWidget::SetMeasureMode);
  connect(view_, &GeoViewerWidget::totalDistanceChanged, this,
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

    qDebug() << "Junction grouping:" << data.junction_grouping.groups.size()
             << "physical groups from"
             << data.junction_grouping.junctions.size()
             << "OpenDRIVE junctions.";

    view_->SetMapAndMesh(data.map, std::move(data.mesh),
                         &data.junction_grouping);

    if (load_progress_) {
      load_progress_->HideLoading();
    }

    status_->showMessage(tr("Map ready. Building layer tree..."));
    QTimer::singleShot(0, this, [this]() {
      layer_control_->UpdateTree();
      status_->showMessage(tr("Map loaded successfully."));
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
  map_loader_->Start(path);
}
