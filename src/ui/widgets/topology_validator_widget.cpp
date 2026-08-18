#include "src/ui/widgets/topology_validator_widget.h"
#include <QBrush>
#include <QColor>
#include <QHeaderView>
#include <QStyle>
#include <QVariant>

#include "OpenDriveMap.h"
#include "RoutingGraph.h"
#include "src/ui/widgets/subwindow_style.h"

TopologyValidatorWidget::TopologyValidatorWidget(
    GeoViewerWidget* viewer, const geoviewer::core::AppSettings& settings,
    QWidget* parent)
    : FloatingPanelWidget(parent), viewer_(viewer), settings_(settings) {
  SetupUi();
  RetranslateUi();

  geoviewer::ui::ApplySubwindowStyle(this);

  hide();
}

void TopologyValidatorWidget::SetupUi() {
  auto* main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(2, 2, 2, 2);
  main_layout->setSpacing(0);

  // Title Bar
  main_layout->addWidget(CreateTitleBar(tr("<b>Topology Validator</b>")));

  content_area_ = new QWidget(this);
  auto* content_layout = new QVBoxLayout(content_area_);
  content_layout->setContentsMargins(5, 5, 5, 5);
  content_layout->setSpacing(5);

  run_btn_ = new QPushButton(tr("Run Validation"), content_area_);
  run_btn_->setStyleSheet(
      "QPushButton { background-color: #007bff; color: white; border: none; "
      "border-radius: 4px; padding: 6px; font-weight: bold; } "
      "QPushButton:hover { background-color: #0069d9; }");
  connect(run_btn_, &QPushButton::clicked, this,
          &TopologyValidatorWidget::HandleRunValidation);
  content_layout->addWidget(run_btn_);

  tree_widget_ = new QTreeWidget(content_area_);
  tree_widget_->setColumnCount(1);
  tree_widget_->setHeaderHidden(true);
  tree_widget_->setWordWrap(true);

  connect(tree_widget_, &QTreeWidget::itemDoubleClicked, this,
          &TopologyValidatorWidget::HandleItemDoubleClicked);
  content_layout->addWidget(tree_widget_);

  main_layout->addWidget(content_area_);

  setFixedWidth(320);
}

void TopologyValidatorWidget::Clear() {
  tree_widget_->clear();
  current_issues_.clear();
}

void TopologyValidatorWidget::RetranslateUi() {
  if (title_label_) title_label_->setText(tr("<b>Topology Validator</b>"));
  if (run_btn_) run_btn_->setText(tr("Run Validation"));
}

void TopologyValidatorWidget::ToggleCollapse() {
  TogglePanelCollapse(content_area_, is_collapsed_, collapse_button_, 30, 200);
}

void TopologyValidatorWidget::mousePressEvent(QMouseEvent* event) {
  if (!BeginPanelDrag(event)) {
    FloatingPanelWidget::mousePressEvent(event);
  }
}

void TopologyValidatorWidget::mouseMoveEvent(QMouseEvent* event) {
  if (!DragPanel(event, false)) {
    FloatingPanelWidget::mouseMoveEvent(event);
  }
}

void TopologyValidatorWidget::mouseReleaseEvent(QMouseEvent* event) {
  FloatingPanelWidget::mouseReleaseEvent(event);
}

void TopologyValidatorWidget::HandleRunValidation() {
  if (!viewer_) return;
  tree_widget_->clear();
  current_issues_.clear();

  auto map = viewer_->GetMap();
  if (!map) {
    auto* item = new QTreeWidgetItem(tree_widget_);
    item->setText(0, tr("No map loaded."));
    item->setFlags(Qt::NoItemFlags);
    return;
  }

  auto* graph = viewer_->GetRoutingGraph();
  current_issues_ =
      geoviewer::logic::MapTopologyValidator::Validate(map, graph);

  if (current_issues_.empty()) {
    auto* item = new QTreeWidgetItem(tree_widget_);
    item->setText(0, tr("No topology issues found."));
    item->setFlags(Qt::NoItemFlags);
    return;
  }

  // Create severity categories
  auto* error_root = new QTreeWidgetItem(tree_widget_);
  error_root->setText(0, tr("Errors (%1)").arg(0));
  error_root->setIcon(0, style()->standardIcon(QStyle::SP_MessageBoxCritical));
  error_root->setExpanded(true);

  auto* warning_root = new QTreeWidgetItem(tree_widget_);
  warning_root->setText(0, tr("Warnings (%1)").arg(0));
  warning_root->setIcon(0, style()->standardIcon(QStyle::SP_MessageBoxWarning));
  warning_root->setExpanded(true);

  auto* info_root = new QTreeWidgetItem(tree_widget_);
  info_root->setText(0, tr("Infos (%1)").arg(0));
  info_root->setIcon(0,
                     style()->standardIcon(QStyle::SP_MessageBoxInformation));

  int error_count = 0;
  int warning_count = 0;
  int info_count = 0;

  for (size_t i = 0; i < current_issues_.size(); ++i) {
    const auto& issue = current_issues_[i];
    auto* item = new QTreeWidgetItem();
    QString label = QString::fromStdString(issue.type);
    if (!issue.lane_id.empty()) {
      label += QString(" | Road %1, Lane %2")
                   .arg(issue.road_id.c_str())
                   .arg(issue.lane_id.c_str());
    } else {
      label += QString(" | Road %1").arg(issue.road_id.c_str());
    }
    item->setText(0, label);
    item->setToolTip(0, QString::fromStdString(issue.message));
    item->setData(0, Qt::UserRole, static_cast<uint>(i));

    // Add detailed message as a child node
    auto* desc_item = new QTreeWidgetItem(item);
    desc_item->setText(0, QString::fromStdString(issue.message));
    desc_item->setToolTip(0, QString::fromStdString(issue.message));
    desc_item->setForeground(0, QBrush(QColor("#aaa")));

    if (issue.severity == geoviewer::logic::TopologySeverity::kError) {
      error_root->addChild(item);
      error_count++;
    } else if (issue.severity == geoviewer::logic::TopologySeverity::kWarning) {
      warning_root->addChild(item);
      warning_count++;
    } else {
      info_root->addChild(item);
      info_count++;
    }
  }

  error_root->setText(0, tr("Errors (%1)").arg(error_count));
  warning_root->setText(0, tr("Warnings (%1)").arg(warning_count));
  info_root->setText(0, tr("Infos (%1)").arg(info_count));

  // Hide empty roots
  if (error_count == 0) delete error_root;
  if (warning_count == 0) delete warning_root;
  if (info_count == 0) delete info_root;
}

void TopologyValidatorWidget::HandleItemDoubleClicked(QTreeWidgetItem* item,
                                                      int column) {
  Q_UNUSED(column);
  if (!item || !viewer_) return;

  QVariant data = item->data(0, Qt::UserRole);
  if (!data.isValid() && item->parent()) {
    // If it's a child node (description), read the data from its parent (the
    // issue node)
    data = item->parent()->data(0, Qt::UserRole);
  }
  if (!data.isValid()) return;

  uint idx = data.toUInt();
  if (idx >= current_issues_.size()) return;

  const auto& issue = current_issues_[idx];

  // Jump camera and highlight
  viewer_->JumpToLocalLocation(issue.x, issue.y, issue.z);
  viewer_->HighlightElement(QString::fromStdString(issue.road_id),
                            issue.node_type,
                            QString::fromStdString(issue.lane_id));
}
