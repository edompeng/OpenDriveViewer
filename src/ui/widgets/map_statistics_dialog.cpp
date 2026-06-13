#include "src/ui/widgets/map_statistics_dialog.h"

#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include "Lane.h"
#include "OpenDriveMap.h"
#include "Road.h"

namespace geoviewer::ui {

MapStatisticsDialog::MapStatisticsDialog(std::shared_ptr<odr::OpenDriveMap> map,
                                         const QString& map_path,
                                         QWidget* parent)
    : QDialog(parent), map_(map), map_path_(map_path) {
  setWindowTitle(tr("Map Statistics"));
  resize(500, 500);

  CalculateStats();
  SetupUi();
}

void MapStatisticsDialog::CalculateStats() {
  if (!map_) return;

  road_count_ = map_->id_to_road.size();
  junction_count_ = map_->id_to_junction.size();
  total_road_length_ = 0.0;
  total_lanes_count_ = 0;
  lane_type_distribution_.clear();

  min_x_ = 1e9;
  max_x_ = -1e9;
  min_y_ = 1e9;
  max_y_ = -1e9;

  for (const auto& [road_id, road] : map_->id_to_road) {
    total_road_length_ += road.length;

    // Sample reference line coordinates for bounding box
    if (road.length > 0.0) {
      double s_steps[] = {0.0, road.length * 0.5, road.length};
      for (double s : s_steps) {
        odr::Vec3D pt = road.get_xyz(s, 0.0, 0.0);
        min_x_ = std::min(min_x_, pt[0]);
        max_x_ = std::max(max_x_, pt[0]);
        min_y_ = std::min(min_y_, pt[1]);
        max_y_ = std::max(max_y_, pt[1]);
      }
    }

    for (const auto& [s0, section] : road.s_to_lanesection) {
      for (const auto& [lane_id, lane] : section.id_to_lane) {
        if (lane_id == 0) continue;  // Skip center lane
        total_lanes_count_++;
        lane_type_distribution_[lane.type]++;
      }
    }
  }

  if (road_count_ == 0) {
    min_x_ = max_x_ = min_y_ = max_y_ = 0.0;
  }
}

void MapStatisticsDialog::SetupUi() {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(15, 15, 15, 15);
  layout->setSpacing(15);

  // Modern Dark Stylesheet
  setStyleSheet(
      "QDialog { background-color: #2b2b36; color: #eee; }"
      "QLabel { color: #eee; font-size: 13px; }"
      "QTableWidget { background-color: #1e1e24; color: #ddd; gridline-color: "
      "#3e3e4a; "
      "border: 1px solid #4e4e5a; border-radius: 4px; font-size: 12px; }"
      "QHeaderView::section { background-color: #32323e; color: #eee; padding: "
      "4px; "
      "border: 1px solid #4e4e5a; font-weight: bold; }"
      "QPushButton { background-color: #007bff; color: white; border: none; "
      "border-radius: 4px; padding: 6px 12px; font-weight: bold; min-width: "
      "80px; }"
      "QPushButton:hover { background-color: #0069d9; }"
      "QPushButton:pressed { background-color: #0056b3; }");

  // General Metadata Table
  auto* table = new QTableWidget(this);
  table->setColumnCount(2);
  table->setRowCount(6);
  table->setHorizontalHeaderLabels({tr("Property"), tr("Value")});
  table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  table->verticalHeader()->setVisible(false);
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setSelectionMode(QAbstractItemView::SingleSelection);

  QFileInfo file_info(map_path_);
  QString map_name = file_info.fileName();
  if (map_name.isEmpty()) map_name = tr("N/A");

  auto setTableItem = [&](int row, const QString& prop, const QString& val) {
    auto* item_prop = new QTableWidgetItem(prop);
    auto* item_val = new QTableWidgetItem(val);
    table->setItem(row, 0, item_prop);
    table->setItem(row, 1, item_val);
  };

  setTableItem(0, tr("Map File"), map_name);
  setTableItem(1, tr("Roads Count"), QString::number(road_count_));
  setTableItem(2, tr("Junctions Count"), QString::number(junction_count_));
  setTableItem(3, tr("Total Road Length"),
               QString("%1 km").arg(total_road_length_ / 1000.0, 0, 'f', 3));
  setTableItem(4, tr("Total Lanes"), QString::number(total_lanes_count_));
  setTableItem(5, tr("Bounding Box (X/Y)"),
               QString("[%1, %2] x [%3, %4]")
                   .arg(min_x_, 0, 'f', 2)
                   .arg(max_x_, 0, 'f', 2)
                   .arg(min_y_, 0, 'f', 2)
                   .arg(max_y_, 0, 'f', 2));

  layout->addWidget(table);

  // Lane Type Distribution Header
  auto* dist_title = new QLabel(tr("<b>Lane Type Distribution</b>"), this);
  layout->addWidget(dist_title);

  // Lane Type Table
  auto* dist_table = new QTableWidget(this);
  dist_table->setColumnCount(2);
  dist_table->setRowCount(static_cast<int>(lane_type_distribution_.size()));
  dist_table->setHorizontalHeaderLabels({tr("Lane Type"), tr("Count")});
  dist_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  dist_table->verticalHeader()->setVisible(false);
  dist_table->setEditTriggers(QAbstractItemView::NoEditTriggers);

  int r_idx = 0;
  for (const auto& [type_name, count] : lane_type_distribution_) {
    auto* item_type = new QTableWidgetItem(QString::fromStdString(type_name));
    auto* item_count = new QTableWidgetItem(QString::number(count));
    dist_table->setItem(r_idx, 0, item_type);
    dist_table->setItem(r_idx, 1, item_count);
    r_idx++;
  }
  layout->addWidget(dist_table);

  // Bottom action buttons
  auto* btn_layout = new QHBoxLayout();
  btn_layout->setSpacing(10);

  auto* copy_btn = new QPushButton(tr("Copy to Clipboard"), this);
  connect(copy_btn, &QPushButton::clicked, this,
          &MapStatisticsDialog::HandleCopyAll);
  btn_layout->addWidget(copy_btn);

  auto* export_btn = new QPushButton(tr("Export JSON"), this);
  connect(export_btn, &QPushButton::clicked, this,
          &MapStatisticsDialog::HandleExportJson);
  btn_layout->addWidget(export_btn);

  btn_layout->addStretch();

  auto* close_btn = new QPushButton(tr("Close"), this);
  connect(close_btn, &QPushButton::clicked, this, &QDialog::accept);
  close_btn->setStyleSheet("background-color: #6c757d;");
  btn_layout->addWidget(close_btn);

  layout->addLayout(btn_layout);
}

QString MapStatisticsDialog::GetFormattedStatsText() const {
  QFileInfo file_info(map_path_);
  QString text = QString(
                     "Map File: %1\n"
                     "Roads Count: %2\n"
                     "Junctions Count: %3\n"
                     "Total Road Length: %4 km\n"
                     "Total Lanes: %5\n"
                     "Bounding Box (X): [%6, %7]\n"
                     "Bounding Box (Y): [%8, %9]\n\n"
                     "Lane Type Distribution:\n")
                     .arg(file_info.fileName())
                     .arg(road_count_)
                     .arg(junction_count_)
                     .arg(total_road_length_ / 1000.0, 0, 'f', 3)
                     .arg(total_lanes_count_)
                     .arg(min_x_, 0, 'f', 2)
                     .arg(max_x_, 0, 'f', 2)
                     .arg(min_y_, 0, 'f', 2)
                     .arg(max_y_, 0, 'f', 2);

  for (const auto& [type_name, count] : lane_type_distribution_) {
    text += QString("  - %1: %2\n").arg(type_name.c_str()).arg(count);
  }

  return text;
}

void MapStatisticsDialog::HandleCopyAll() {
  QApplication::clipboard()->setText(GetFormattedStatsText());
}

void MapStatisticsDialog::HandleExportJson() {
  QJsonObject root;
  root["map_file"] = map_path_;
  root["roads_count"] = static_cast<double>(road_count_);
  root["junctions_count"] = static_cast<double>(junction_count_);
  root["total_road_length_m"] = total_road_length_;
  root["total_lanes"] = static_cast<double>(total_lanes_count_);

  QJsonObject bbox;
  bbox["min_x"] = min_x_;
  bbox["max_x"] = max_x_;
  bbox["min_y"] = min_y_;
  bbox["max_y"] = max_y_;
  root["bounding_box"] = bbox;

  QJsonObject dist;
  for (const auto& [type_name, count] : lane_type_distribution_) {
    dist[QString::fromStdString(type_name)] = static_cast<double>(count);
  }
  root["lane_type_distribution"] = dist;

  QJsonDocument doc(root);
  QString save_path = QFileDialog::getSaveFileName(
      this, tr("Export Map Statistics"), "", tr("JSON Files (*.json)"));
  if (save_path.isEmpty()) return;

  QFile file(save_path);
  if (file.open(QIODevice::WriteOnly)) {
    file.write(doc.toJson());
    file.close();
  } else {
    QMessageBox::critical(this, tr("Error"), tr("Could not write to file."));
  }
}

}  // namespace geoviewer::ui
