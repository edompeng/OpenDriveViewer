#include "src/ui/widgets/layer_control_widget.h"
#include <QtGui/qkeysequence.h>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QSettings>
#include <QSignalBlocker>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include "src/core/app_settings.h"
#include "src/core/scene_enums.h"
#include "src/core/thread_pool.h"
#include "src/ui/widgets/layer_tree_model.h"

namespace {
QTreeWidgetItem* CreateRootItem(QTreeWidget* parent) {
  auto* item = new QTreeWidgetItem(parent);
  item->setFlags(item->flags() | Qt::ItemIsUserCheckable |
                 Qt::ItemIsAutoTristate);
  return item;
}

QTreeWidgetItem* CreateChildItem(QTreeWidgetItem* parent) {
  auto* item = new QTreeWidgetItem(parent);
  item->setFlags(item->flags() | Qt::ItemIsUserCheckable |
                 Qt::ItemIsAutoTristate);
  return item;
}
}  // namespace

LayerControlWidget::LayerControlWidget(
    GeoViewerWidget* viewer, const geoviewer::core::AppSettings& settings,
    QWidget* parent)
    : QWidget(parent), viewer_(viewer), settings_(settings) {
  setWindowTitle(tr("Layer Control"));
  setWindowIcon(QIcon(":/icons/layers.png"));  // Optional if icon exists

  auto* main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(10, 10, 10, 10);
  main_layout->setSpacing(10);

  // --- Section 1: Global Layer Visibility ---
  auto* global_box = new QGroupBox(tr("Global Layer Visibility"), this);
  auto* global_layout = new QGridLayout(global_box);
  global_layout->setContentsMargins(10, 10, 10, 10);
  global_layer_checkboxes_.clear();
  int idx = 0;
  for (const auto& [layer, visibility] : settings.global_layer_visibility) {
    std::string layer_str = LayerTypeToString(layer);
    if (layer_str.empty()) {
      continue;
    }
    auto* cb = new QCheckBox(tr(layer_str.c_str()), global_box);
    cb->setChecked(visibility);
    viewer_->SetLayerVisible(layer, visibility);
    connect(cb, &QCheckBox::toggled, this, [this, layer = layer](bool checked) {
      if (viewer_->IsLayerVisible(layer) != checked) {
        viewer_->SetLayerVisible(layer, checked);
        emit SettingsChanged();
      }
    });
    global_layout->addWidget(cb, idx / 3, idx % 3);
    global_layer_checkboxes_.push_back(cb);
    idx++;
  }
  main_layout->addWidget(global_box);

  // --- Section 2: Map Hierarchy Tree ---
  auto* tree_box = new QGroupBox(tr("Map Hierarchy"), this);
  auto* tree_layout = new QVBoxLayout(tree_box);
  tree_layout->setContentsMargins(5, 5, 5, 5);

  tree_ = new QTreeWidget(tree_box);
  tree_->setHeaderHidden(true);
  tree_->setContextMenuPolicy(Qt::CustomContextMenu);
  tree_->setMouseTracking(true);

  auto* search_layout = new QHBoxLayout();
  search_edit_ = new QLineEdit(tree_box);
  search_edit_->setPlaceholderText(tr("Search ID..."));
  search_layout->addWidget(search_edit_);

  auto* collapse_btn = new QToolButton(tree_box);
  collapse_btn->setToolTip(tr("Collapse All"));
  collapse_btn->setIcon(style()->standardIcon(
      QStyle::SP_ToolBarVerticalExtensionButton));  // Better icon
  search_layout->addWidget(collapse_btn);
  connect(collapse_btn, &QToolButton::clicked, this, [this]() {
    search_edit_->clear();  // Clear search to show everything before collapsing
    tree_->collapseAll();
  });

  tree_layout->addLayout(search_layout);
  tree_layout->addWidget(tree_);

  search_timer_ = new QTimer(this);
  search_timer_->setSingleShot(true);
  search_timer_->setInterval(300);  // 300ms debounce
  connect(search_timer_, &QTimer::timeout, this,
          &LayerControlWidget::HandleSearch);

  connect(search_edit_, &QLineEdit::textChanged, this,
          [this](const QString&) { search_timer_->start(); });

  main_layout->addWidget(tree_box);

  setStyleSheet(
      "LayerControlWidget { background-color: #2b2b2b; color: #eee; } "
      "QGroupBox { font-weight: bold; border: 1px solid #555; border-radius: "
      "6px; margin-top: 1.1em; padding-top: 10px; } "
      "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 "
      "3px; } "
      "QCheckBox { spacing: 5px; } "
      "QTreeWidget { background-color: #222; color: #eee; border: 1px solid "
      "#444; border-radius: 4px; } "
      "QTreeWidget::item:hover { background-color: #3d3d3d; } "
      "QTreeWidget::item:selected { background-color: #4a4a4a; } "
      "QLineEdit { background-color: #333; color: white; border: 1px solid "
      "#555; border-radius: 4px; padding: 4px; }");

  connect(tree_, &QTreeWidget::customContextMenuRequested, this,
          &LayerControlWidget::HandleCustomContextMenu);
  connect(tree_, &QTreeWidget::itemChanged, this,
          &LayerControlWidget::HandleItemChanged);
  connect(tree_, &QTreeWidget::itemEntered, this,
          &LayerControlWidget::HandleItemEntered);
  connect(tree_, &QTreeWidget::itemDoubleClicked, this,
          &LayerControlWidget::HandleItemDoubleClicked);
  connect(tree_, &QTreeWidget::itemExpanded, this,
          &LayerControlWidget::HandleItemExpanded);

  connect(viewer_, &GeoViewerWidget::ElementVisibilityChanged, this,
          &LayerControlWidget::HandleElementVisibilityChanged);

  setMinimumSize(350, 400);
  resize(400, 700);
}

LayerControlWidget::~LayerControlWidget() { snapshot_generation_++; }

void LayerControlWidget::UpdateTree() {
  if (!viewer_->GetMap()) return;
  RequestSnapshotBuild();
}

void LayerControlWidget::RequestSnapshotBuild() {
  auto map = viewer_->GetMap();
  if (!map) return;

  const auto generation = ++snapshot_generation_;
  const JunctionClusterResult junction_result =
      viewer_->GetJunctionClusterResult();

  tree_->setUpdatesEnabled(false);
  tree_->clear();
  items_by_full_id_.clear();
  auto* loading_item = CreateRootItem(tree_);
  loading_item->setText(0, "Building layer tree...");
  tree_->setUpdatesEnabled(true);
  geoviewer::utility::ThreadPool::Instance().Enqueue(
      [this, map, junction_result, generation]() {
        auto snapshot = BuildLayerTreeSnapshot(map, junction_result);
        QMetaObject::invokeMethod(
            this, [this, snapshot = std::move(snapshot), generation]() mutable {
              if (generation == snapshot_generation_.load()) {
                tree_snapshot_ = std::move(snapshot);
                PopulateTopLevelItems();
              }
            });
      });
}

void LayerControlWidget::RetranslateUi() {
  setWindowTitle(tr("Layer Control"));
  if (auto* group = qobject_cast<QGroupBox*>(
          layout()->itemAt(0)->widget())) {  // Global layer visibility group
    group->setTitle(tr("Global Layer Visibility"));
  }
  if (auto* group = qobject_cast<QGroupBox*>(
          layout()->itemAt(1)->widget())) {  // Map hierarchy group
    group->setTitle(tr("Map Hierarchy"));
  }

  size_t idx = 0;
  for (const auto& [layer, _] : settings_.global_layer_visibility) {
    auto str = LayerTypeToString(layer);
    if (str.empty()) {
      continue;
    }
    if (idx < global_layer_checkboxes_.size()) {
      global_layer_checkboxes_[idx]->setText(tr(str.c_str()));
      idx++;
    }
  }
  search_edit_->setPlaceholderText(tr("Search ID..."));
  if (tree_snapshot_) {
    PopulateTopLevelItems();
  }
}

void LayerControlWidget::changeEvent(QEvent* event) {
  if (event->type() == QEvent::LanguageChange) {
    RetranslateUi();
  }
  QWidget::changeEvent(event);
}

Qt::CheckState LayerControlWidget::ComputeJunctionGroupCheckState(
    const JunctionGroupSnapshot& group) const {
  return ::ComputeJunctionGroupCheckState(group, viewer_->HiddenElements());
}

Qt::CheckState LayerControlWidget::ComputeRoadCheckState(
    const RoadSnapshot& road) const {
  return ::ComputeRoadCheckState(road, viewer_->HiddenElements());
}

void LayerControlWidget::PopulateTopLevelItems() {
  if (!tree_snapshot_) return;

  is_populating_ = true;
  viewer_->BeginBatchUpdate();
  tree_->setUpdatesEnabled(false);
  QSignalBlocker blocker(tree_);
  tree_->clear();
  items_by_full_id_.clear();
  road_snapshot_index_by_id_.clear();
  junction_snapshot_index_by_id_.clear();

  auto registerItem = [&](QTreeWidgetItem* item, const QString& full_id) {
    if (!full_id.isEmpty()) items_by_full_id_.insert(full_id, item);
  };

  auto* junction_root = CreateRootItem(tree_);
  junction_root->setText(0,
                         tr("Junctions (%1 groups / %2 junctions)")
                             .arg((int)tree_snapshot_->junction_groups.size())
                             .arg(tree_snapshot_->junction_count));
  junction_root->setData(0, Qt::UserRole, (int)TreeNodeType::kJunctionGroup);
  junction_root->setData(0, Qt::UserRole + 1, "__junction_root__");
  // The state will be computed from children via UpdateParentCheckState below

  for (int index = 0; index < (int)tree_snapshot_->junction_groups.size();
       ++index) {
    const auto& group = tree_snapshot_->junction_groups[index];
    auto* group_item = CreateChildItem(junction_root);
    group_item->setText(0, group.label);
    group_item->setData(0, Qt::UserRole, (int)TreeNodeType::kJunctionGroup);
    group_item->setData(0, Qt::UserRole + 1, group.group_id);
    group_item->setData(0, Qt::UserRole + 3, false);
    group_item->setCheckState(0, ComputeJunctionGroupCheckState(group));
    if (!group.junction_ids.empty()) {
      group_item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
    }
    registerItem(group_item, "JG:" + group.group_id);
    junction_snapshot_index_by_id_.insert(group.group_id, index);
  }
  if (junction_root->childCount() == 0) {
    junction_root->setCheckState(
        0, viewer_->IsLayerVisible(LayerType::kJunctions) ? Qt::Checked
                                                          : Qt::Unchecked);
  }
  registerItem(junction_root, "L:kJunctions");

  for (int index = 0; index < (int)tree_snapshot_->roads.size(); ++index) {
    const auto& road = tree_snapshot_->roads[index];
    auto* road_item = CreateRootItem(tree_);
    road_item->setText(0, tr("Road %1").arg(road.road_id));
    road_item->setData(0, Qt::UserRole, (int)TreeNodeType::kRoad);
    road_item->setData(0, Qt::UserRole + 1, road.road_id);
    road_item->setData(0, Qt::UserRole + 3, false);
    road_item->setCheckState(0, ComputeRoadCheckState(road));
    road_item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
    registerItem(road_item, "R:" + road.road_id);
    road_snapshot_index_by_id_.insert(road.road_id, index);
  }

  viewer_->EndBatchUpdate();
  is_populating_ = false;
  tree_->setUpdatesEnabled(true);
}

void LayerControlWidget::PopulateJunctionChildren(
    QTreeWidgetItem* group_item, const JunctionGroupSnapshot& group) {
  const auto& hidden = viewer_->HiddenElements();
  const bool parentVisible = group_item->checkState(0) != Qt::Unchecked;
  for (const auto& junction_id : group.junction_ids) {
    const QString full_id = "J:" + group.group_id + ":" + junction_id;
    auto* junction_item = CreateChildItem(group_item);
    junction_item->setText(0, tr("Junction %1").arg(junction_id));
    junction_item->setData(0, Qt::UserRole, (int)TreeNodeType::kJunction);
    junction_item->setData(0, Qt::UserRole + 1, junction_id);
    junction_item->setData(0, Qt::UserRole + 2, group.group_id);
    const bool visible =
        parentVisible && hidden.find(full_id.toStdString()) == hidden.end();
    junction_item->setCheckState(0, visible ? Qt::Checked : Qt::Unchecked);
    items_by_full_id_.insert(full_id, junction_item);
  }
}

void LayerControlWidget::PopulateRoadChildren(QTreeWidgetItem* road_item,
                                              const RoadSnapshot& road) {
  const QString road_id = road.road_id;
  const auto& hidden = viewer_->HiddenElements();
  const bool road_visible = road_item->checkState(0) != Qt::Unchecked;
  auto childState = [&](const QString& full_id) {
    return (road_visible && hidden.find(full_id.toStdString()) == hidden.end())
               ? Qt::Checked
               : Qt::Unchecked;
  };

  auto* ref_line_item = CreateChildItem(road_item);
  ref_line_item->setText(0, tr("Reference Line"));
  ref_line_item->setData(0, Qt::UserRole, (int)TreeNodeType::kRefLine);
  ref_line_item->setData(0, Qt::UserRole + 1, "refline");
  ref_line_item->setCheckState(0, childState("E:" + road_id + ":refline"));
  items_by_full_id_.insert("E:" + road_id + ":refline", ref_line_item);

  for (const auto& lane : road.lanes) {
    const QString full_id = "E:" + road_id + ":lane:" + lane.element_id;
    auto* lane_item = CreateChildItem(road_item);
    lane_item->setText(0, lane.label);
    lane_item->setData(0, Qt::UserRole, (int)TreeNodeType::kLane);
    lane_item->setData(0, Qt::UserRole + 1, lane.element_id);
    lane_item->setCheckState(0, childState(full_id));
    items_by_full_id_.insert(full_id, lane_item);
  }

  auto addGroup = [&](const char* label, TreeNodeType groupType,
                      const QString& group_name,
                      const std::vector<RoadChildSnapshot>& entries) {
    if (entries.empty()) return;
    const QString groupFullId = "G:" + road_id + ":" + group_name;
    auto* group_item = CreateChildItem(road_item);
    group_item->setText(0, label);
    group_item->setData(0, Qt::UserRole, (int)groupType);
    group_item->setData(0, Qt::UserRole + 1, group_name);
    const bool groupVisible =
        road_visible && hidden.find(groupFullId.toStdString()) == hidden.end();
    group_item->setCheckState(0, groupVisible ? Qt::Checked : Qt::Unchecked);
    items_by_full_id_.insert(groupFullId, group_item);
    for (const auto& entry : entries) {
      const QString full_id =
          BuildFullId(road_id, entry.type, entry.element_id);
      auto* child = CreateChildItem(group_item);
      child->setText(0, entry.label);
      child->setData(0, Qt::UserRole, (int)entry.type);
      child->setData(0, Qt::UserRole + 1, entry.element_id);
      const bool visible =
          groupVisible && hidden.find(full_id.toStdString()) == hidden.end();
      child->setCheckState(0, visible ? Qt::Checked : Qt::Unchecked);
      items_by_full_id_.insert(full_id, child);
    }
  };

  addGroup(tr("Objects").toUtf8().constData(), TreeNodeType::kObjectGroup,
           "objects", road.objects);
  addGroup(tr("Signal Lights").toUtf8().constData(), TreeNodeType::kLightGroup,
           "light", road.lights);
  addGroup(tr("Signal Signs").toUtf8().constData(), TreeNodeType::kSignGroup,
           "sign", road.signs);
}

void LayerControlWidget::EnsureChildrenLoaded(QTreeWidgetItem* item) {
  if (!item || !tree_snapshot_) return;
  if (item->data(0, Qt::UserRole + 3).toBool()) return;

  QSignalBlocker blocker(tree_);
  const bool prev_populating = is_populating_;
  is_populating_ = true;
  const TreeNodeType type = (TreeNodeType)item->data(0, Qt::UserRole).toInt();
  if (type == TreeNodeType::kRoad) {
    const QString road_id = item->data(0, Qt::UserRole + 1).toString();
    auto it = road_snapshot_index_by_id_.find(road_id);
    if (it != road_snapshot_index_by_id_.end()) {
      PopulateRoadChildren(item, tree_snapshot_->roads[it.value()]);
      item->setData(0, Qt::UserRole + 3, true);
    }
  } else if (type == TreeNodeType::kJunctionGroup) {
    const QString group_id = item->data(0, Qt::UserRole + 1).toString();
    if (group_id != "__junction_root__") {
      auto it = junction_snapshot_index_by_id_.find(group_id);
      if (it != junction_snapshot_index_by_id_.end()) {
        PopulateJunctionChildren(item,
                                 tree_snapshot_->junction_groups[it.value()]);
        item->setData(0, Qt::UserRole + 3, true);
      }
    }
  }
  is_populating_ = prev_populating;
}

bool LayerControlWidget::EnsureItemMaterialized(const QString& road_id,
                                                TreeNodeType type,
                                                const QString& element_id) {
  const QString full_id = BuildFullId(road_id, type, element_id);
  if (items_by_full_id_.contains(full_id)) return true;

  if (type == TreeNodeType::kJunction) {
    auto groupIt = items_by_full_id_.find("JG:" + road_id);
    if (groupIt != items_by_full_id_.end()) {
      EnsureChildrenLoaded(groupIt.value());
    }
  } else if (type != TreeNodeType::kRoad &&
             type != TreeNodeType::kJunctionGroup) {
    auto roadIt = items_by_full_id_.find("R:" + road_id);
    if (roadIt != items_by_full_id_.end()) {
      EnsureChildrenLoaded(roadIt.value());
    }
  }
  return items_by_full_id_.contains(full_id);
}

void LayerControlWidget::HandleItemExpanded(QTreeWidgetItem* item) {
  if (!item) return;
  EnsureChildrenLoaded(item);
}

void LayerControlWidget::HandleItemEntered(QTreeWidgetItem* item,
                                           int /*column*/) {
  if (!item) {
    emit ItemHovered("", TreeNodeType::kRoad, "");
    return;
  }

  TreeNodeType type = (TreeNodeType)item->data(0, Qt::UserRole).toInt();
  QString road_id, element_id;

  if (type == TreeNodeType::kRoad || type == TreeNodeType::kSection ||
      type == TreeNodeType::kLane || type == TreeNodeType::kObject ||
      type == TreeNodeType::kLight || type == TreeNodeType::kSign ||
      type == TreeNodeType::kJunction || type == TreeNodeType::kJunctionGroup ||
      type == TreeNodeType::kObjectGroup || type == TreeNodeType::kLightGroup ||
      type == TreeNodeType::kSignGroup) {
    road_id = GetRoadId(item);
    element_id = item->data(0, Qt::UserRole + 1).toString();
  } else {
    // Groups or other non-mappable items
    emit ItemHovered("", TreeNodeType::kRoad, "");
    return;
  }

  emit ItemHovered(road_id, type, element_id);
}

void LayerControlWidget::HandleItemDoubleClicked(QTreeWidgetItem* item,
                                                 int /*column*/) {
  if (!item) return;

  TreeNodeType type = (TreeNodeType)item->data(0, Qt::UserRole).toInt();
  QString road_id, element_id;

  if (type == TreeNodeType::kRoad || type == TreeNodeType::kSection ||
      type == TreeNodeType::kLane || type == TreeNodeType::kObject ||
      type == TreeNodeType::kLight || type == TreeNodeType::kSign ||
      type == TreeNodeType::kJunction || type == TreeNodeType::kJunctionGroup ||
      type == TreeNodeType::kObjectGroup || type == TreeNodeType::kLightGroup ||
      type == TreeNodeType::kSignGroup) {
    road_id = GetRoadId(item);
    element_id = item->data(0, Qt::UserRole + 1).toString();
  } else {
    // Groups or other non-mappable items
    return;
  }

  viewer_->CenterOnElement(road_id, type, element_id);
}

void LayerControlWidget::leaveEvent(QEvent* event) {
  emit ItemHovered("", TreeNodeType::kRoad, "");
  QWidget::leaveEvent(event);
}

void LayerControlWidget::HandleItemChanged(QTreeWidgetItem* item,
                                           int /*column*/) {
  if (is_populating_) return;

  const TreeNodeType changed_type =
      (TreeNodeType)item->data(0, Qt::UserRole).toInt();
  const QString changed_id = item->data(0, Qt::UserRole + 1).toString();

  if (changed_type == TreeNodeType::kJunctionGroup &&
      changed_id == "__junction_root__") {
    // If user interacts with junction root, populate all children
    bool was_populating = is_populating_;
    is_populating_ = true;
    for (int i = 0; i < item->childCount(); ++i) {
      if (!item->child(i)->data(0, Qt::UserRole + 3).toBool()) {
        EnsureChildrenLoaded(item->child(i));
      }
    }
    is_populating_ = was_populating;
  } else if (changed_type == TreeNodeType::kJunctionGroup ||
             changed_type == TreeNodeType::kRoad) {
    // If user interacts with an unpopulated group/road, populate it
    if (!item->data(0, Qt::UserRole + 3).toBool()) {
      bool was_populating = is_populating_;
      is_populating_ = true;
      EnsureChildrenLoaded(item);
      is_populating_ = was_populating;
    }
  }

  auto ApplyVisibility = [&](QTreeWidgetItem* current) {
    const QString full_id = GetFullId(current);
    if (!full_id.isEmpty()) {
      const bool visible = (current->checkState(0) != Qt::Unchecked);
      if (full_id.startsWith("L:")) {
        if (full_id == "L:kJunctions") {
          viewer_->SetLayerVisible(LayerType::kJunctions, visible);
        }
      } else {
        viewer_->SetElementVisible(full_id, visible);
      }
    }
  };

  // Block further signal handling if any state syncing happens internally (just
  // in case)
  bool was_syncing = is_populating_;
  is_populating_ = true;
  viewer_->BeginBatchUpdate();

  // 1. Ancestors
  QTreeWidgetItem* p = item->parent();
  while (p) {
    ApplyVisibility(p);
    p = p->parent();
  }

  // 2. The item and its descendants
  QList<QTreeWidgetItem*> queue{item};
  while (!queue.isEmpty()) {
    auto* current = queue.takeFirst();
    ApplyVisibility(current);
    for (int i = 0; i < current->childCount(); ++i) {
      queue.push_back(current->child(i));
    }
  }

  viewer_->EndBatchUpdate();
  is_populating_ = was_syncing;
}

QString LayerControlWidget::GetFullId(QTreeWidgetItem* item) {
  TreeNodeType type = (TreeNodeType)item->data(0, Qt::UserRole).toInt();
  QString element_id = item->data(0, Qt::UserRole + 1).toString();

  if (type == TreeNodeType::kRoad) return "R:" + element_id;
  if (type == TreeNodeType::kJunctionGroup) {
    if (element_id == "__junction_root__") return "L:kJunctions";
    return "JG:" + element_id;
  }
  if (type == TreeNodeType::kJunction) {
    const QString group_id = item->data(0, Qt::UserRole + 2).toString();
    return "J:" + group_id + ":" + element_id;
  }

  QTreeWidgetItem* parent = item->parent();
  if (!parent) return "";

  if (type == TreeNodeType::kLane) {
    QString road_id = GetRoadId(item);
    const QStringList parts = element_id.split(':');
    if (parts.size() != 2) return "";
    return "E:" + road_id + ":lane:" + parts[0] + ":" + parts[1];
  }

  if (type == TreeNodeType::kRefLine) {
    QString road_id = GetRoadId(item);
    return "E:" + road_id + ":refline";
  }

  if (type == TreeNodeType::kSectionGroup ||
      type == TreeNodeType::kObjectGroup || type == TreeNodeType::kLightGroup ||
      type == TreeNodeType::kSignGroup) {
    QString road_id = GetRoadId(item);
    return "G:" + road_id + ":" + element_id;
  }

  QString road_id = GetRoadId(item);
  QString group_name = parent->data(0, Qt::UserRole + 1).toString();

  return "E:" + road_id + ":" + group_name + ":" + element_id;
}

QString LayerControlWidget::BuildFullId(const QString& road_id,
                                        TreeNodeType type,
                                        const QString& element_id) const {
  return BuildLayerTreeFullId(road_id, type, element_id);
}

void LayerControlWidget::HandleElementVisibilityChanged(const QString& id,
                                                        bool visible) {
  if (is_populating_) return;
  auto it = items_by_full_id_.find(id);
  if (it == items_by_full_id_.end()) return;

  Qt::CheckState current_state = it.value()->checkState(0);
  if (visible) {
    if (current_state == Qt::Checked || current_state == Qt::PartiallyChecked) {
      return;  // Already reflecting a visible state
    }
  } else {
    if (current_state == Qt::Unchecked) {
      return;  // Already hidden
    }
  }

  is_populating_ = true;
  it.value()->setCheckState(0, visible ? Qt::Checked : Qt::Unchecked);
  is_populating_ = false;
}

void LayerControlWidget::HandleSearch() {
  QString query = search_edit_->text().trimmed();

  tree_->setUpdatesEnabled(false);

  if (query.isEmpty()) {
    // Show everything
    QTreeWidgetItemIterator it(tree_);
    while (*it) {
      (*it)->setHidden(false);
      ++it;
    }
    tree_->setUpdatesEnabled(true);
    return;
  }

  // Hide everything first
  QTreeWidgetItemIterator it(tree_);
  while (*it) {
    (*it)->setHidden(true);
    ++it;
  }

  if (!tree_snapshot_) {
    tree_->setUpdatesEnabled(true);
    return;
  }

  // Use a temporary list to collect matching items to avoid redundant
  // materialization calls
  struct Match {
    QString road_id;
    TreeNodeType type;
    QString id;
  };
  std::vector<Match> matches;

  // 1. Snapshot search (Fast)
  for (const auto& road : tree_snapshot_->roads) {
    if (road.road_id.contains(query, Qt::CaseInsensitive)) {
      matches.push_back({road.road_id, TreeNodeType::kRoad, road.road_id});
    }
    for (const auto& lane : road.lanes) {
      if (lane.element_id.contains(query, Qt::CaseInsensitive))
        matches.push_back({road.road_id, TreeNodeType::kLane, lane.element_id});
    }
    for (const auto& obj : road.objects) {
      if (obj.element_id.contains(query, Qt::CaseInsensitive))
        matches.push_back(
            {road.road_id, TreeNodeType::kObject, obj.element_id});
    }
    for (const auto& light : road.lights) {
      if (light.element_id.contains(query, Qt::CaseInsensitive))
        matches.push_back(
            {road.road_id, TreeNodeType::kLight, light.element_id});
    }
    for (const auto& sign : road.signs) {
      if (sign.element_id.contains(query, Qt::CaseInsensitive))
        matches.push_back({road.road_id, TreeNodeType::kSign, sign.element_id});
    }
  }

  for (const auto& group : tree_snapshot_->junction_groups) {
    if (group.group_id.contains(query, Qt::CaseInsensitive))
      matches.push_back(
          {group.group_id, TreeNodeType::kJunctionGroup, group.group_id});
    for (const auto& jid : group.junction_ids) {
      if (jid.contains(query, Qt::CaseInsensitive))
        matches.push_back({group.group_id, TreeNodeType::kJunction, jid});
    }
  }

  // 2. Materialize and show matches (Limit to first 100 matches to prevent UI
  // freeze)
  int count = 0;
  for (const auto& m : matches) {
    if (++count > 100) break;
    EnsureItemMaterialized(m.road_id, m.type, m.id);
    QString full_id = BuildFullId(m.road_id, m.type, m.id);
    auto item_it = items_by_full_id_.find(full_id);
    if (item_it != items_by_full_id_.end()) {
      QTreeWidgetItem* item = item_it.value();
      while (item) {
        item->setHidden(false);
        item->setExpanded(true);
        item = item->parent();
      }
    }
  }

  tree_->setUpdatesEnabled(true);
}

void LayerControlWidget::HandleCustomContextMenu(const QPoint& pos) {
  QTreeWidgetItem* item = tree_->itemAt(pos);
  if (!item) return;

  QMenu menu(this);
  QAction* toggle_visible = menu.addAction(
      item->checkState(0) == Qt::Checked ? tr("Hide") : tr("Show"));

  QAction* copy_info = menu.addAction(tr("📋 Copy item info"));

  TreeNodeType type = (TreeNodeType)item->data(0, Qt::UserRole).toInt();
  QAction* goTo = nullptr;

  // Jump to element for ID level nodes
  if (type == TreeNodeType::kRoad || type == TreeNodeType::kSection ||
      type == TreeNodeType::kLane || type == TreeNodeType::kObject ||
      type == TreeNodeType::kLight || type == TreeNodeType::kSign ||
      type == TreeNodeType::kJunction || type == TreeNodeType::kJunctionGroup) {
    goTo = menu.addAction(tr("Jump to element"));
  }

  QAction* addFav = menu.addAction(tr("⭐ Add to favorites"));

  QAction* setStart = nullptr;
  QAction* setEnd = nullptr;
  if (type == TreeNodeType::kLane) {
    menu.addSeparator();
    setStart = menu.addAction(tr("🚩 Set as routing start"));
    setEnd = menu.addAction(tr("🏁 Set as routing end"));
  }

  QAction* selected = menu.exec(tree_->viewport()->mapToGlobal(pos));
  if (selected == toggle_visible) {
    item->setCheckState(
        0, item->checkState(0) == Qt::Checked ? Qt::Unchecked : Qt::Checked);
  } else if (selected == copy_info) {
    QString info = item->text(0);
    if (!item->data(0, Qt::UserRole + 1).toString().isEmpty()) {
      info += " - ID: " + item->data(0, Qt::UserRole + 1).toString();
    }
    QApplication::clipboard()->setText(info);
  } else if (selected == goTo) {
    QString road_id = GetRoadId(item);
    QString element_id = item->data(0, Qt::UserRole + 1).toString();
    viewer_->CenterOnElement(road_id, type, element_id);
  } else if (selected == addFav) {
    QString road_id = GetRoadId(item);
    QString element_id = item->data(0, Qt::UserRole + 1).toString();
    emit viewer_->AddFavoriteRequested(road_id, type, element_id,
                                       item->text(0));
  } else if (selected == setStart || selected == setEnd) {
    QString road_id = GetRoadId(item);
    QString data_str = item->data(0, Qt::UserRole + 1).toString();
    const QStringList parts = data_str.split(':', Qt::SkipEmptyParts);
    const QString s0 = parts.value(0);
    const QString laneId = parts.value(1);

    if (!road_id.isEmpty() && !s0.isEmpty() && !laneId.isEmpty()) {
      QString lane_pos = QString("%1/%2/%3").arg(road_id).arg(s0).arg(laneId);
      if (selected == setStart)
        emit viewer_->RoutingStartRequested(lane_pos.trimmed());
      else
        emit viewer_->RoutingEndRequested(lane_pos.trimmed());
    }
  }
}

void LayerControlWidget::SelectElement(const QString& road_id,
                                       TreeNodeType type,
                                       const QString& element_id) {
  EnsureItemMaterialized(road_id, type, element_id);
  const QString full_id = BuildFullId(road_id, type, element_id);
  auto it = items_by_full_id_.find(full_id);
  if (it == items_by_full_id_.end()) return;

  QTreeWidgetItem* item = it.value();
  tree_->setCurrentItem(item);
  tree_->scrollToItem(item);
  item->setSelected(true);
  QTreeWidgetItem* p = item->parent();
  while (p) {
    p->setExpanded(true);
    p = p->parent();
  }
}

void LayerControlWidget::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  if (tree_->topLevelItemCount() == 0 && viewer_->GetMap()) {
    UpdateTree();
  }
}

QString LayerControlWidget::GetRoadId(QTreeWidgetItem* item) {
  while (item) {
    TreeNodeType type = (TreeNodeType)item->data(0, Qt::UserRole).toInt();
    if (type == TreeNodeType::kJunctionGroup) {
      return item->data(0, Qt::UserRole + 1).toString();
    }
    if (type == TreeNodeType::kJunction) {
      return item->data(0, Qt::UserRole + 2).toString();
    }
    if ((TreeNodeType)item->data(0, Qt::UserRole).toInt() ==
        TreeNodeType::kRoad) {
      return item->data(0, Qt::UserRole + 1).toString();
    }
    item = item->parent();
  }
  return "";
}
