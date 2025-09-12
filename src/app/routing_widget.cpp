#include "src/app/routing_widget.h"
#include <QDebug>
#include <QMessageBox>
#include <vector>
#include "src/utility/input_parsing.h"
#include "src/utility/routing_logic.h"
#include "third_party/libOpenDRIVE/include/RoutingGraph.h"

RoutingWidget::RoutingWidget(GeoViewerWidget* viewer, QWidget* parent)
    : FloatingPanelWidget(parent), viewer_(viewer) {
  auto* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(2, 2, 2, 2);
  mainLayout->setSpacing(0);

  // Title Bar
  auto* titleBar = new QWidget(this);
  titleBar->setStyleSheet(
      "background-color: #445; border-top-left-radius: 8px; "
      "border-top-right-radius: 8px;");
  auto* titleLayout = new QHBoxLayout(titleBar);
  titleLayout->setContentsMargins(10, 5, 5, 5);

  title_label_ = new QLabel(tr("<b>Routing</b>"), titleBar);
  title_label_->setStyleSheet("color: white;");
  title_label_->setAttribute(Qt::WA_TransparentForMouseEvents);
  titleLayout->addWidget(title_label_);
  titleLayout->addStretch();

  collapse_button_ = new QToolButton(titleBar);
  collapse_button_->setText("−");
  collapse_button_->setStyleSheet(
      "color: white; border: none; font-weight: bold;");
  connect(collapse_button_, &QToolButton::clicked, this,
          &RoutingWidget::ToggleCollapse);
  titleLayout->addWidget(collapse_button_);

  mainLayout->addWidget(titleBar);

  // Content Area
  content_area_ = new QWidget(this);
  auto* contentLayout = new QVBoxLayout(content_area_);
  contentLayout->setContentsMargins(10, 10, 10, 10);

  start_input_ = new QLineEdit(content_area_);
  start_input_->setPlaceholderText(tr("Start: road/section/lane"));
  start_input_->setStyleSheet(
      "background: rgba(255,255,255,0.1); color: white; border: 1px solid "
      "rgba(255,255,255,0.2); border-radius: 4px; padding: 5px;");
  start_label_ = new QLabel(tr("Start (road/section/lane):"), content_area_);
  contentLayout->addWidget(start_label_);
  contentLayout->addWidget(start_input_);

  end_input_ = new QLineEdit(content_area_);
  end_input_->setPlaceholderText(tr("End: road/section/lane"));
  end_input_->setStyleSheet(
      "background: rgba(255,255,255,0.1); color: white; border: 1px solid "
      "rgba(255,255,255,0.2); border-radius: 4px; padding: 5px;");
  end_label_ = new QLabel(tr("End (road/section/lane):"), content_area_);
  contentLayout->addWidget(end_label_);
  contentLayout->addWidget(end_input_);

  calc_btn_ = new QPushButton(tr("Calculate"), content_area_);
  calc_btn_->setStyleSheet(
      "background-color: #28a745; color: white; font-weight: bold; "
      "border-radius: 4px; padding: 8px; margin-top: 10px;");
  connect(calc_btn_, &QPushButton::clicked, this,
          &RoutingWidget::HandleCalculate);
  contentLayout->addWidget(calc_btn_);

  history_label_ = new QLabel(tr("History:"), content_area_);
  contentLayout->addWidget(history_label_);
  history_tree_ = new QTreeWidget(content_area_);
  history_tree_->setHeaderHidden(true);
  history_tree_->setMinimumHeight(150);
  history_tree_->setStyleSheet(
      "QTreeWidget { background: rgba(0,0,0,0.2); color: #eee; border: 1px "
      "solid rgba(255,255,255,0.1); }");
  history_tree_->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(history_tree_, &QTreeWidget::itemChanged, this,
          &RoutingWidget::HandleHistoryItemChanged);
  connect(history_tree_, &QTreeWidget::customContextMenuRequested, this,
          &RoutingWidget::HandleHistoryContextMenu);
  connect(history_tree_, &QTreeWidget::itemEntered, this,
          &RoutingWidget::HandleItemEntered);
  connect(history_tree_, &QTreeWidget::itemDoubleClicked, this,
          &RoutingWidget::HandleItemDoubleClicked);
  history_tree_->setMouseTracking(true);
  contentLayout->addWidget(history_tree_);

  mainLayout->addWidget(content_area_);

  setStyleSheet(
      "RoutingWidget { background-color: rgba(60, 60, 70, 230); border-radius: "
      "8px; border: 1px solid #666; color: #eee; } "
      "QLabel { color: #ccc; }");

  setFixedWidth(250);
  adjustSize();
}

void RoutingWidget::mousePressEvent(QMouseEvent* event) {
  if (!BeginPanelDrag(event)) {
    FloatingPanelWidget::mousePressEvent(event);
  }
}

void RoutingWidget::mouseMoveEvent(QMouseEvent* event) {
  if (!DragPanel(event, false)) {
    FloatingPanelWidget::mouseMoveEvent(event);
  }
}

void RoutingWidget::mouseReleaseEvent(QMouseEvent* event) {
  FloatingPanelWidget::mouseReleaseEvent(event);
}

void RoutingWidget::leaveEvent(QEvent* event) {
  viewer_->ClearHighlight();
  QWidget::leaveEvent(event);
}

void RoutingWidget::RetranslateUi() {
  title_label_->setText(tr("<b>Routing</b>"));
  start_label_->setText(tr("Start (road/section/lane):"));
  start_input_->setPlaceholderText(tr("Start: road/section/lane"));
  end_label_->setText(tr("End (road/section/lane):"));
  end_input_->setPlaceholderText(tr("End: road/section/lane"));
  calc_btn_->setText(tr("Calculate"));
  history_label_->setText(tr("History:"));
}

void RoutingWidget::ToggleCollapse() {
  TogglePanelCollapse(content_area_, is_collapsed_, collapse_button_, 35, -1);
}

void RoutingWidget::HandleCalculate() {
  auto startKey =
      CoordinateInputParser::ParseLaneKey(start_input_->text().toStdString());
  auto endKey =
      CoordinateInputParser::ParseLaneKey(end_input_->text().toStdString());

  if (!startKey || !endKey) {
    QMessageBox::warning(this, "Input Error",
                         "Invalid input format. Use: road/section/lane");
    return;
  }

  auto map = viewer_->GetMap();
  if (!map) {
    QMessageBox::warning(this, "Error", "Map not loaded.");
    return;
  }

  odr::RoutingGraph graph = map->get_routing_graph();
  std::vector<odr::LaneKey> path = graph.shortest_path(*startKey, *endKey);

  if (path.empty()) {
    QMessageBox::information(this, "Result",
                             "No path found between selected lanes.");
    return;
  }

  // Call viewer to render path
  const RouteHistoryEntry entry =
      BuildRouteHistoryEntry(start_input_->text().toStdString(),
                             end_input_->text().toStdString(), path);
  int routeId =
      viewer_->AddRoutingPath(path, QString::fromStdString(entry.display_name));

  if (routeId != -1) {
    auto* rootItem = new QTreeWidgetItem(history_tree_);
    rootItem->setText(0, QString::fromStdString(entry.display_name));
    rootItem->setData(0, Qt::UserRole, routeId);
    rootItem->setCheckState(0, Qt::Checked);
    rootItem->setFlags(rootItem->flags() | Qt::ItemIsUserCheckable |
                       Qt::ItemIsEnabled | Qt::ItemIsSelectable);

    for (const auto& road_id : entry.road_sequence) {
      auto* child = new QTreeWidgetItem(rootItem);
      child->setText(
          0, QString("🛣️ kRoad ID: %1").arg(QString::fromStdString(road_id)));
      child->setData(0, Qt::UserRole, (int)TreeNodeType::kRoad);
      child->setData(0, Qt::UserRole + 1, QString::fromStdString(road_id));
    }
    rootItem->setExpanded(true);
  }
}

void RoutingWidget::HandleHistoryItemChanged(QTreeWidgetItem* item,
                                             int column) {
  if (column == 0 && item->parent() == nullptr) {
    int routeId = item->data(0, Qt::UserRole).toInt();
    viewer_->SetRoutingPathVisible(routeId, item->checkState(0) == Qt::Checked);
  }
}

void RoutingWidget::HandleHistoryContextMenu(const QPoint& pos) {
  auto* item = history_tree_->itemAt(pos);
  if (!item) return;

  // Only allow deletion of the root item (the route itself)
  if (item->parent() != nullptr) item = item->parent();

  QMenu menu(this);
  auto* removeAct = menu.addAction(tr("❌ Delete routing"));
  auto* selected = menu.exec(history_tree_->mapToGlobal(pos));

  if (selected == removeAct) {
    int routeId = item->data(0, Qt::UserRole).toInt();
    viewer_->RemoveRoutingPath(routeId);
    delete item;
  }
}

void RoutingWidget::HandleItemDoubleClicked(QTreeWidgetItem* item, int column) {
  if (!item) return;

  if (item->parent() == nullptr) {
    // This is a route root item. Maybe jump to the first element?
    // For now, let's just let it expand/collapse.
    return;
  }

  // Double clicked a road under a route
  TreeNodeType type = (TreeNodeType)item->data(0, Qt::UserRole).toInt();
  QString roadId = item->data(0, Qt::UserRole + 1).toString();

  if (type == TreeNodeType::kRoad && !roadId.isEmpty()) {
    viewer_->CenterOnElement(roadId, type, "");
  }
}

void RoutingWidget::HandleItemEntered(QTreeWidgetItem* item, int column) {
  if (!item) {
    viewer_->ClearHighlight();
    return;
  }

  if (item->parent() == nullptr) {
    QStringList roadIds;
    for (int i = 0; i < item->childCount(); ++i) {
      roadIds << item->child(i)->data(0, Qt::UserRole + 1).toString();
    }
    viewer_->HighlightRoads(roadIds);
    return;
  }

  TreeNodeType type = (TreeNodeType)item->data(0, Qt::UserRole).toInt();
  QString roadId = item->data(0, Qt::UserRole + 1).toString();
  if (type == TreeNodeType::kRoad && !roadId.isEmpty()) {
    viewer_->HighlightElement(roadId, type, "");
  }
}

void RoutingWidget::SetStartLane(const QString& lanePos) {
  if (is_collapsed_) ToggleCollapse();
  start_input_->setText(lanePos);
}

void RoutingWidget::SetEndLane(const QString& lanePos) {
  if (is_collapsed_) ToggleCollapse();
  end_input_->setText(lanePos);
}
