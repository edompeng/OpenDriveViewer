#include "src/ui/widgets/coordinate_points_widget.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QShowEvent>
#include <QToolButton>

#include "src/logic/input_parsing.h"

CoordinatePointsWidget::CoordinatePointsWidget(GeoViewerWidget* viewer,
                                               QWidget* parent)
    : FloatingPanelWidget(parent), viewer_(viewer) {
  setMinimumSize(280, 50);
  resize(320, 380);

  auto* main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(2, 2, 2, 2);
  main_layout->setSpacing(0);

  // Title Bar
  auto* title_bar = new QWidget(this);
  title_bar->setFixedHeight(30);
  title_bar->setStyleSheet(
      "background-color: #445544; border-top-left-radius: 8px; "
      "border-top-right-radius: 8px;");
  auto* title_layout = new QHBoxLayout(title_bar);
  title_layout->setContentsMargins(10, 5, 5, 5);

  title_label_ = new QLabel(tr("<b>Coordinate Points</b>"), title_bar);
  title_label_->setStyleSheet("color: white;");
  title_label_->setAttribute(Qt::WA_TransparentForMouseEvents);
  title_layout->addWidget(title_label_);
  title_layout->addStretch();

  collapse_button_ = new QToolButton(title_bar);
  collapse_button_->setText("−");
  collapse_button_->setStyleSheet(
      "color: white; border: none; font-weight: bold;");
  connect(collapse_button_, &QToolButton::clicked, this,
          &CoordinatePointsWidget::ToggleCollapse);
  title_layout->addWidget(collapse_button_);
  main_layout->addWidget(title_bar);

  // Content
  content_area_ = new QWidget(this);
  auto* content_layout = new QVBoxLayout(content_area_);
  content_layout->setContentsMargins(5, 5, 5, 5);
  content_layout->setSpacing(4);

  // Input area
  input_label_ = new QLabel(tr("(lon,lat[,alt]); ...:"), content_area_);
  input_label_->setStyleSheet("color: #ccc; font-size: 11px;");
  content_layout->addWidget(input_label_);

  input_points_edit_ = new QLineEdit(content_area_);
  input_points_edit_->setPlaceholderText(
      tr("(lon,lat) or (lon,lat,alt) semicolon separated"));
  input_points_edit_->setStyleSheet(
      "background: rgba(255,255,255,0.1); color: white; border: 1px solid "
      "rgba(255,255,255,0.2); border-radius: 4px; padding: 5px;");
  content_layout->addWidget(input_points_edit_);

  auto* btn_layout = new QHBoxLayout();
  btn_layout->setSpacing(4);
  add_btn_ = new QPushButton(tr("Add"), content_area_);
  add_btn_->setStyleSheet(
      "background-color: #007bff; color: white; border-radius: 4px; "
      "padding: 6px 12px;");
  connect(add_btn_, &QPushButton::clicked, this,
          &CoordinatePointsWidget::HandleAddPoint);

  clear_btn_ = new QPushButton(tr("Clear All"), content_area_);
  clear_btn_->setStyleSheet(
      "background-color: #6c757d; color: white; border-radius: 4px; "
      "padding: 6px 12px;");
  connect(clear_btn_, &QPushButton::clicked, this,
          &CoordinatePointsWidget::HandleClearPoints);

  btn_layout->addWidget(add_btn_);
  btn_layout->addWidget(clear_btn_);
  content_layout->addLayout(btn_layout);

  // Separator
  auto* sep = new QWidget(content_area_);
  sep->setFixedHeight(1);
  sep->setStyleSheet("background-color: rgba(255,255,255,0.15);");
  content_layout->addWidget(sep);

  // Points list
  list_label_ = new QLabel(tr("Added points:"), content_area_);
  list_label_->setStyleSheet("color: #aaa; font-size: 11px;");
  content_layout->addWidget(list_label_);

  points_list_ = new QListWidget(content_area_);
  points_list_->setUniformItemSizes(true);
  points_list_->setContextMenuPolicy(Qt::CustomContextMenu);
  points_list_->setStyleSheet(
      "QListWidget { background-color: rgba(0,0,0,0.2); color: #eee; "
      "border: 1px solid rgba(255,255,255,0.1); border-radius: 4px; "
      "padding: 2px; } "
      "QListWidget::item { padding: 2px 4px; border-bottom: 1px solid "
      "rgba(255,255,255,0.05); } "
      "QListWidget::item:hover { background-color: rgba(255,255,255,0.08); } "
      "QListWidget::item:selected { background-color: rgba(255,255,255,0.15); "
      "}");
  content_layout->addWidget(points_list_, 1);

  connect(points_list_, &QListWidget::itemDoubleClicked, this,
          &CoordinatePointsWidget::HandleItemDoubleClicked);
  connect(points_list_, &QListWidget::customContextMenuRequested, this,
          &CoordinatePointsWidget::HandleCustomContextMenu);

  main_layout->addWidget(content_area_);

  setStyleSheet(
      "CoordinatePointsWidget { background-color: rgba(50, 60, 50, 230); "
      "border-radius: 8px; border: 1px solid #556655; } ");

  // Listen for viewer point changes
  connect(viewer_, &GeoViewerWidget::UserPointsChanged, this,
          &CoordinatePointsWidget::HandlePointsChanged);

  hide();  // Default hidden
}

void CoordinatePointsWidget::SetCoordinateMode(CoordinateMode mode) {
  if (coord_mode_ == mode) return;
  coord_mode_ = mode;
  RetranslateUi();
}

void CoordinatePointsWidget::RetranslateUi() {
  title_label_->setText(tr("<b>Coordinate Points</b>"));
  if (coord_mode_ == CoordinateMode::kWGS84) {
    input_label_->setText(tr("(lon,lat[,alt]); ...:"));
    input_points_edit_->setPlaceholderText(
        tr("(lon,lat) or (lon,lat,alt) semicolon separated"));
  } else {
    input_label_->setText(tr("(x,y[,z]); ...:"));
    input_points_edit_->setPlaceholderText(
        tr("(x,y) or (x,y,z) semicolon separated"));
  }
  add_btn_->setText(tr("Add"));
  clear_btn_->setText(tr("Clear All"));
  list_label_->setText(tr("Added points:"));
  HandlePointsChanged();
}

void CoordinatePointsWidget::HandleAddPoint() {
  const auto points = CoordinateInputParser::ParseUserPoints(
      input_points_edit_->text().toStdString());
  if (points.empty()) return;

  viewer_->BeginUserPointsBatch();
  for (const auto& point : points) {
    if (coord_mode_ == CoordinateMode::kWGS84) {
      viewer_->AddUserPoint(point.x, point.y, point.z);
    } else {
      viewer_->AddUserPointLocal(point.x, point.y, point.z);
    }
  }
  viewer_->EndUserPointsBatch();

  // List is refreshed via the UserPointsChanged signal
}

void CoordinatePointsWidget::HandleClearPoints() {
  viewer_->ClearUserPoints();
  // List is refreshed via the UserPointsChanged signal
}

void CoordinatePointsWidget::HandlePointsChanged() {
  points_list_dirty_ = true;
  if (!isVisible()) return;
  RefreshPointsList();
  points_list_dirty_ = false;
}

void CoordinatePointsWidget::showEvent(QShowEvent* event) {
  FloatingPanelWidget::showEvent(event);
  if (!points_list_dirty_) return;
  RefreshPointsList();
  points_list_dirty_ = false;
}

void CoordinatePointsWidget::HandleItemDoubleClicked(QListWidgetItem* item) {
  if (!item) return;
  int index = item->data(Qt::UserRole).toInt();
  auto snap = viewer_->GetUserPointSnapshot(index);
  if (coord_mode_ == CoordinateMode::kWGS84) {
    viewer_->JumpToLocation(snap.lon, snap.lat, snap.alt);
  } else {
    viewer_->JumpToLocalLocation(snap.x, snap.y, snap.z);
  }
}

void CoordinatePointsWidget::HandleCustomContextMenu(const QPoint& pos) {
  QListWidgetItem* item = points_list_->itemAt(pos);
  if (!item) return;

  int index = item->data(Qt::UserRole).toInt();
  auto snap = viewer_->GetUserPointSnapshot(index);

  QMenu menu(this);
  QAction* toggle_vis =
      menu.addAction(snap.visible ? tr("👁 Hide point") : tr("👁 Show point"));
  QAction* change_color = menu.addAction(tr("🎨 Change color"));
  QAction* jump_to = menu.addAction(tr("🎯 Jump to point"));
  menu.addSeparator();
  QAction* remove = menu.addAction(tr("❌ Delete point"));

  QAction* selected = menu.exec(points_list_->viewport()->mapToGlobal(pos));
  if (!selected) return;

  if (selected == toggle_vis) {
    viewer_->SetUserPointVisible(index, !snap.visible);
    RefreshPointsList();
  } else if (selected == change_color) {
    QColor initial =
        QColor::fromRgbF(snap.color.x(), snap.color.y(), snap.color.z());
    QColor chosen =
        QColorDialog::getColor(initial, this, tr("Select point color"));
    if (chosen.isValid()) {
      viewer_->SetUserPointColor(
          index, QVector3D(chosen.redF(), chosen.greenF(), chosen.blueF()));
      RefreshPointsList();
    }
  } else if (selected == jump_to) {
    if (coord_mode_ == CoordinateMode::kWGS84) {
      viewer_->JumpToLocation(snap.lon, snap.lat, snap.alt);
    } else {
      viewer_->JumpToLocalLocation(snap.x, snap.y, snap.z);
    }
  } else if (selected == remove) {
    viewer_->RemoveUserPoint(index);
    // List is refreshed via the UserPointsChanged signal
  }
}

void CoordinatePointsWidget::ToggleCollapse() {
  TogglePanelCollapse(content_area_, is_collapsed_, collapse_button_, 30, 380);
}

QWidget* CoordinatePointsWidget::BuildPointItemWidget(int index) {
  auto snap = viewer_->GetUserPointSnapshot(index);

  auto* widget = new QWidget();
  auto* layout = new QHBoxLayout(widget);
  layout->setContentsMargins(2, 1, 2, 1);
  layout->setSpacing(4);

  // Checkbox for visibility
  auto* checkbox = new QCheckBox(widget);
  checkbox->setChecked(snap.visible);
  checkbox->setFixedSize(16, 16);
  checkbox->setStyleSheet(
      "QCheckBox::indicator { width: 14px; height: 14px; }");
  connect(checkbox, &QCheckBox::toggled, this, [this, index](bool checked) {
    viewer_->SetUserPointVisible(index, checked);
    // Update the color swatch opacity to reflect visibility
    RefreshPointsList();
  });
  layout->addWidget(checkbox);

  // Color swatch button
  auto* color_btn = new QToolButton(widget);
  color_btn->setFixedSize(16, 16);
  QColor point_color =
      QColor::fromRgbF(snap.color.x(), snap.color.y(), snap.color.z());
  color_btn->setStyleSheet(
      QString("background-color: %1; border: 1px solid rgba(255,255,255,0.3); "
              "border-radius: 3px;")
          .arg(point_color.name()));
  connect(color_btn, &QToolButton::clicked, this, [this, index]() {
    auto s = viewer_->GetUserPointSnapshot(index);
    QColor initial = QColor::fromRgbF(s.color.x(), s.color.y(), s.color.z());
    QColor chosen =
        QColorDialog::getColor(initial, this, tr("Select point color"));
    if (chosen.isValid()) {
      viewer_->SetUserPointColor(
          index, QVector3D(chosen.redF(), chosen.greenF(), chosen.blueF()));
      RefreshPointsList();
    }
  });
  layout->addWidget(color_btn);

  // Coordinate label
  QString coordText;
  if (coord_mode_ == CoordinateMode::kWGS84) {
    coordText = QString("%1, %2, %3")
                    .arg(snap.lon, 0, 'f', 7)
                    .arg(snap.lat, 0, 'f', 7)
                    .arg(snap.alt, 0, 'f', 2);
  } else {
    coordText = QString("%1, %2, %3")
                    .arg(snap.x, 0, 'f', 3)
                    .arg(snap.y, 0, 'f', 3)
                    .arg(snap.z, 0, 'f', 3);
  }
  auto* label = new QLabel(coordText, widget);

  label->setStyleSheet(QString("color: %1; font-size: 11px;")
                           .arg(snap.visible ? "#eee" : "#777"));
  layout->addWidget(label, 1);

  // Delete button
  auto* delete_btn = new QToolButton(widget);
  delete_btn->setText("✕");
  delete_btn->setFixedSize(18, 18);
  delete_btn->setStyleSheet(
      "color: #f66; border: none; font-weight: bold; font-size: 12px;");
  connect(delete_btn, &QToolButton::clicked, this, [this, index]() {
    viewer_->RemoveUserPoint(index);
    // List is refreshed via the UserPointsChanged signal
  });
  layout->addWidget(delete_btn);

  return widget;
}

void CoordinatePointsWidget::RefreshPointsList() {
  points_list_->setUpdatesEnabled(false);
  points_list_->clear();
  const int count = viewer_->UserPointCount();
  for (int i = 0; i < count; ++i) {
    auto* item = new QListWidgetItem(points_list_);
    item->setData(Qt::UserRole, i);
    item->setSizeHint(QSize(0, 24));
    points_list_->addItem(item);
    points_list_->setItemWidget(item, BuildPointItemWidget(i));
  }
  points_list_->setUpdatesEnabled(true);
}
