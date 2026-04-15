#include "src/app/coordinate_points_widget.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QToolButton>

#include "src/utility/input_parsing.h"

CoordinatePointsWidget::CoordinatePointsWidget(GeoViewerWidget* viewer,
                                               QWidget* parent)
    : FloatingPanelWidget(parent), viewer_(viewer) {
  setMinimumSize(280, 50);
  resize(320, 380);

  auto* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(2, 2, 2, 2);
  mainLayout->setSpacing(0);

  // Title Bar
  auto* titleBar = new QWidget(this);
  titleBar->setFixedHeight(30);
  titleBar->setStyleSheet(
      "background-color: #445544; border-top-left-radius: 8px; "
      "border-top-right-radius: 8px;");
  auto* titleLayout = new QHBoxLayout(titleBar);
  titleLayout->setContentsMargins(10, 5, 5, 5);

  title_label_ = new QLabel(tr("<b>Coordinate Points</b>"), titleBar);
  title_label_->setStyleSheet("color: white;");
  title_label_->setAttribute(Qt::WA_TransparentForMouseEvents);
  titleLayout->addWidget(title_label_);
  titleLayout->addStretch();

  collapse_button_ = new QToolButton(titleBar);
  collapse_button_->setText("−");
  collapse_button_->setStyleSheet(
      "color: white; border: none; font-weight: bold;");
  connect(collapse_button_, &QToolButton::clicked, this,
          &CoordinatePointsWidget::ToggleCollapse);
  titleLayout->addWidget(collapse_button_);
  mainLayout->addWidget(titleBar);

  // Content
  content_area_ = new QWidget(this);
  auto* contentLayout = new QVBoxLayout(content_area_);
  contentLayout->setContentsMargins(5, 5, 5, 5);
  contentLayout->setSpacing(4);

  // Input area
  input_label_ = new QLabel(tr("(lon,lat[,alt]); ...:"), content_area_);
  input_label_->setStyleSheet("color: #ccc; font-size: 11px;");
  contentLayout->addWidget(input_label_);

  input_points_edit_ = new QLineEdit(content_area_);
  input_points_edit_->setPlaceholderText(
      tr("(lon,lat) or (lon,lat,alt) semicolon separated"));
  input_points_edit_->setStyleSheet(
      "background: rgba(255,255,255,0.1); color: white; border: 1px solid "
      "rgba(255,255,255,0.2); border-radius: 4px; padding: 5px;");
  contentLayout->addWidget(input_points_edit_);

  auto* btnLayout = new QHBoxLayout();
  btnLayout->setSpacing(4);
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

  btnLayout->addWidget(add_btn_);
  btnLayout->addWidget(clear_btn_);
  contentLayout->addLayout(btnLayout);

  // Separator
  auto* sep = new QWidget(content_area_);
  sep->setFixedHeight(1);
  sep->setStyleSheet("background-color: rgba(255,255,255,0.15);");
  contentLayout->addWidget(sep);

  // Points list
  list_label_ = new QLabel(tr("Added points:"), content_area_);
  list_label_->setStyleSheet("color: #aaa; font-size: 11px;");
  contentLayout->addWidget(list_label_);

  points_list_ = new QListWidget(content_area_);
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
  contentLayout->addWidget(points_list_, 1);

  connect(points_list_, &QListWidget::itemDoubleClicked, this,
          &CoordinatePointsWidget::HandleItemDoubleClicked);
  connect(points_list_, &QListWidget::customContextMenuRequested, this,
          &CoordinatePointsWidget::HandleCustomContextMenu);

  mainLayout->addWidget(content_area_);

  setStyleSheet(
      "CoordinatePointsWidget { background-color: rgba(50, 60, 50, 230); "
      "border-radius: 8px; border: 1px solid #556655; } ");

  // Listen for viewer point changes
  connect(viewer_, &GeoViewerWidget::userPointsChanged, this,
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
  RefreshPointsList();
}


void CoordinatePointsWidget::HandleAddPoint() {
  const auto points = CoordinateInputParser::ParseUserPoints(
      input_points_edit_->text().toStdString());
  for (const auto& point : points) {
    if (coord_mode_ == CoordinateMode::kWGS84) {
      viewer_->AddUserPoint(point.x, point.y, point.z);
    } else {
      viewer_->AddUserPointLocal(point.x, point.y, point.z);
    }
  }

  // List is refreshed via the userPointsChanged signal
}

void CoordinatePointsWidget::HandleClearPoints() {
  viewer_->ClearUserPoints();
  // List is refreshed via the userPointsChanged signal
}

void CoordinatePointsWidget::HandlePointsChanged() { RefreshPointsList(); }

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
    // List is refreshed via the userPointsChanged signal
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
  auto* colorBtn = new QToolButton(widget);
  colorBtn->setFixedSize(16, 16);
  QColor point_color =
      QColor::fromRgbF(snap.color.x(), snap.color.y(), snap.color.z());
  colorBtn->setStyleSheet(
      QString("background-color: %1; border: 1px solid rgba(255,255,255,0.3); "
              "border-radius: 3px;")
          .arg(point_color.name()));
  connect(colorBtn, &QToolButton::clicked, this, [this, index]() {
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
  layout->addWidget(colorBtn);

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
  auto* deleteBtn = new QToolButton(widget);
  deleteBtn->setText("✕");
  deleteBtn->setFixedSize(18, 18);
  deleteBtn->setStyleSheet(
      "color: #f66; border: none; font-weight: bold; font-size: 12px;");
  connect(deleteBtn, &QToolButton::clicked, this, [this, index]() {
    viewer_->RemoveUserPoint(index);
    // List is refreshed via the userPointsChanged signal
  });
  layout->addWidget(deleteBtn);

  return widget;
}

void CoordinatePointsWidget::RefreshPointsList() {
  points_list_->clear();
  const int count = viewer_->UserPointCount();
  for (int i = 0; i < count; ++i) {
    auto* item = new QListWidgetItem(points_list_);
    item->setData(Qt::UserRole, i);
    item->setSizeHint(QSize(0, 24));
    points_list_->addItem(item);
    points_list_->setItemWidget(item, BuildPointItemWidget(i));
  }
}
