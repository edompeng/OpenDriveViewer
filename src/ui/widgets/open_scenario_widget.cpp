#include "src/ui/widgets/open_scenario_widget.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace {

constexpr int kNodeTypeRole = Qt::UserRole;
constexpr int kFileIdRole = Qt::UserRole + 1;
constexpr int kEntityNameRole = Qt::UserRole + 2;

enum class ScenarioNodeType {
  kFile = 0,
  kEntity = 1,
};

}  // namespace

OpenScenarioWidget::OpenScenarioWidget(GeoViewerWidget* viewer, QWidget* parent)
    : FloatingPanelWidget(parent), viewer_(viewer) {
  auto* main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(2, 2, 2, 2);
  main_layout->setSpacing(0);

  auto* title_bar = new QWidget(this);
  title_bar->setStyleSheet(
      "background-color: #355; border-top-left-radius: 8px; "
      "border-top-right-radius: 8px;");
  auto* title_layout = new QHBoxLayout(title_bar);
  title_layout->setContentsMargins(10, 5, 5, 5);

  title_label_ = new QLabel(tr("<b>OpenSCENARIO</b>"), title_bar);
  title_label_->setStyleSheet("color: white;");
  title_label_->setAttribute(Qt::WA_TransparentForMouseEvents);
  title_layout->addWidget(title_label_);
  title_layout->addStretch();

  collapse_button_ = new QToolButton(title_bar);
  collapse_button_->setText("−");
  collapse_button_->setStyleSheet(
      "color: white; border: none; font-weight: bold;");
  connect(collapse_button_, &QToolButton::clicked, this,
          &OpenScenarioWidget::ToggleCollapse);
  title_layout->addWidget(collapse_button_);
  main_layout->addWidget(title_bar);

  content_area_ = new QWidget(this);
  auto* content_layout = new QVBoxLayout(content_area_);
  content_layout->setContentsMargins(6, 6, 6, 6);

  auto* action_row = new QWidget(content_area_);
  auto* action_layout = new QHBoxLayout(action_row);
  action_layout->setContentsMargins(0, 0, 0, 0);
  action_layout->setSpacing(6);
  add_files_btn_ = new QPushButton(tr("Load .xosc"), action_row);
  remove_btn_ = new QPushButton(tr("Remove selected"), action_row);
  connect(add_files_btn_, &QPushButton::clicked, this,
          &OpenScenarioWidget::HandleAddFiles);
  connect(remove_btn_, &QPushButton::clicked, this,
          &OpenScenarioWidget::HandleRemoveSelected);
  action_layout->addWidget(add_files_btn_);
  action_layout->addWidget(remove_btn_);
  content_layout->addWidget(action_row);

  tree_ = new QTreeWidget(content_area_);
  tree_->setHeaderHidden(true);
  tree_->setContextMenuPolicy(Qt::CustomContextMenu);
  tree_->setMouseTracking(true);
  content_layout->addWidget(tree_);
  main_layout->addWidget(content_area_);

  setStyleSheet(
      "OpenScenarioWidget { background-color: rgba(45, 60, 68, 235); "
      "border-radius: 8px; border: 1px solid #4d6c78; } "
      "QTreeWidget { background-color: transparent; color: #eef; border: none; "
      "} "
      "QTreeWidget::item:hover { background-color: rgba(120,160,190,0.25); }");

  connect(tree_, &QTreeWidget::itemChanged, this,
          &OpenScenarioWidget::HandleItemChanged);
  connect(tree_, &QTreeWidget::itemEntered, this,
          &OpenScenarioWidget::HandleItemEntered);
  connect(tree_, &QTreeWidget::itemDoubleClicked, this,
          &OpenScenarioWidget::HandleItemDoubleClicked);
  connect(tree_, &QTreeWidget::customContextMenuRequested, this,
          &OpenScenarioWidget::HandleCustomContextMenu);
  connect(viewer_, &GeoViewerWidget::OpenScenarioDataChanged, this,
          &OpenScenarioWidget::RebuildTree);

  setMinimumSize(280, 50);
  resize(280, 300);
  RebuildTree();
}

void OpenScenarioWidget::mousePressEvent(QMouseEvent* event) {
  if (!BeginPanelDrag(event)) {
    FloatingPanelWidget::mousePressEvent(event);
  }
}

void OpenScenarioWidget::mouseMoveEvent(QMouseEvent* event) {
  if (!DragPanel(event, true)) {
    FloatingPanelWidget::mouseMoveEvent(event);
  }
}

void OpenScenarioWidget::mouseReleaseEvent(QMouseEvent* event) {
  FloatingPanelWidget::mouseReleaseEvent(event);
}

void OpenScenarioWidget::leaveEvent(QEvent* event) {
  viewer_->HighlightOpenScenarioEntity("", "");
  QWidget::leaveEvent(event);
}

void OpenScenarioWidget::ToggleCollapse() {
  TogglePanelCollapse(content_area_, is_collapsed_, collapse_button_, 30, 300);
}

void OpenScenarioWidget::RetranslateUi() {
  title_label_->setText(tr("<b>OpenSCENARIO</b>"));
  add_files_btn_->setText(tr("Load .xosc"));
  remove_btn_->setText(tr("Remove selected"));
  RebuildTree();
}

void OpenScenarioWidget::HandleAddFiles() {
  const QStringList files = QFileDialog::getOpenFileNames(
      this, "Load OpenSCENARIO files", QString(),
      "OpenSCENARIO Files (*.xosc *.xml);;All Files (*)");
  if (files.isEmpty()) return;

  QStringList failed;
  for (const auto& path : files) {
    QString err;
    if (!viewer_->LoadOpenScenarioFile(path, &err)) {
      failed << QString("%1: %2").arg(path, err);
    }
  }
  if (!failed.isEmpty()) {
    QMessageBox::warning(this, "Load OpenSCENARIO Failed", failed.join("\n"));
  }
}

void OpenScenarioWidget::HandleRemoveSelected() {
  QTreeWidgetItem* item = tree_->currentItem();
  if (!item) return;

  const ScenarioNodeType node_type =
      static_cast<ScenarioNodeType>(item->data(0, kNodeTypeRole).toInt());
  QString file_id = item->data(0, kFileIdRole).toString();
  if (node_type == ScenarioNodeType::kEntity && item->parent()) {
    file_id = item->parent()->data(0, kFileIdRole).toString();
  }
  if (file_id.isEmpty()) return;
  viewer_->RemoveOpenScenarioFile(file_id);
}

void OpenScenarioWidget::RebuildTree() {
  const auto snapshots = viewer_->OpenScenarioSnapshots();
  is_syncing_ = true;
  tree_->setUpdatesEnabled(false);
  QSignalBlocker blocker(tree_);
  tree_->clear();

  for (const auto& file : snapshots) {
    auto* file_item = new QTreeWidgetItem(tree_);
    QString file_title = file.file_name;
    if (!file.version.isEmpty()) {
      file_title += QString(" (v%1)").arg(file.version);
    }
    file_item->setText(0, file_title);
    file_item->setData(0, kNodeTypeRole, (int)ScenarioNodeType::kFile);
    file_item->setData(0, kFileIdRole, file.file_id);
    file_item->setData(0, kEntityNameRole, QString());

    int checked_count = 0;
    for (const auto& entity : file.entities) {
      auto* entity_item = new QTreeWidgetItem(file_item);
      entity_item->setData(0, kNodeTypeRole, (int)ScenarioNodeType::kEntity);
      entity_item->setData(0, kFileIdRole, file.file_id);
      entity_item->setData(0, kEntityNameRole, entity.name);
      entity_item->setFlags(entity_item->flags() | Qt::ItemIsUserCheckable |
                            Qt::ItemIsEnabled | Qt::ItemIsSelectable);
      entity_item->setCheckState(0,
                                 entity.visible ? Qt::Checked : Qt::Unchecked);
      if (entity.visible) ++checked_count;

      QString label = entity.name;
      if (!entity.object_type.isEmpty()) {
        label += QString(" [%1]").arg(entity.object_type);
      }
      if (!entity.has_position) {
        label += " (no position)";
      }
      entity_item->setText(0, label);
      if (!entity.position_desc.isEmpty()) {
        entity_item->setToolTip(0, entity.position_desc);
      }
    }

    file_item->setFlags(file_item->flags() | Qt::ItemIsUserCheckable |
                        Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    if (file.entities.empty()) {
      file_item->setCheckState(0, Qt::Unchecked);
    } else if (checked_count == 0) {
      file_item->setCheckState(0, Qt::Unchecked);
    } else if (checked_count == (int)file.entities.size()) {
      file_item->setCheckState(0, Qt::Checked);
    } else {
      file_item->setCheckState(0, Qt::PartiallyChecked);
    }
    file_item->setExpanded(true);
  }

  tree_->setUpdatesEnabled(true);
  is_syncing_ = false;
}

void OpenScenarioWidget::SetSubtreeCheckState(QTreeWidgetItem* item,
                                              Qt::CheckState state) {
  if (!item) return;
  item->setCheckState(0, state);
  for (int i = 0; i < item->childCount(); ++i) {
    SetSubtreeCheckState(item->child(i), state);
  }
}

void OpenScenarioWidget::UpdateParentCheckState(QTreeWidgetItem* item) {
  while (item) {
    int checked = 0;
    int unchecked = 0;
    int partial = 0;
    for (int i = 0; i < item->childCount(); ++i) {
      const Qt::CheckState state = item->child(i)->checkState(0);
      if (state == Qt::Checked)
        ++checked;
      else if (state == Qt::Unchecked)
        ++unchecked;
      else
        ++partial;
    }

    if (partial > 0 || (checked > 0 && unchecked > 0)) {
      item->setCheckState(0, Qt::PartiallyChecked);
    } else if (checked == item->childCount() && checked > 0) {
      item->setCheckState(0, Qt::Checked);
    } else {
      item->setCheckState(0, Qt::Unchecked);
    }
    item = item->parent();
  }
}

void OpenScenarioWidget::HandleItemChanged(QTreeWidgetItem* item, int column) {
  if (column != 0 || is_syncing_ || !item) return;

  const ScenarioNodeType node_type =
      static_cast<ScenarioNodeType>(item->data(0, kNodeTypeRole).toInt());
  const QString file_id = item->data(0, kFileIdRole).toString();
  if (file_id.isEmpty()) return;

  is_syncing_ = true;
  if (node_type == ScenarioNodeType::kFile &&
      item->checkState(0) != Qt::PartiallyChecked) {
    SetSubtreeCheckState(item, item->checkState(0));
    viewer_->SetOpenScenarioFileVisible(file_id,
                                        item->checkState(0) == Qt::Checked);
  } else if (node_type == ScenarioNodeType::kEntity) {
    const QString entity_name = item->data(0, kEntityNameRole).toString();
    viewer_->SetOpenScenarioEntityVisible(file_id, entity_name,
                                          item->checkState(0) == Qt::Checked);
    UpdateParentCheckState(item->parent());
  }
  is_syncing_ = false;
}

void OpenScenarioWidget::HandleItemEntered(QTreeWidgetItem* item, int column) {
  Q_UNUSED(column);
  if (!item) {
    viewer_->HighlightOpenScenarioEntity("", "");
    return;
  }

  const ScenarioNodeType node_type =
      static_cast<ScenarioNodeType>(item->data(0, kNodeTypeRole).toInt());
  if (node_type != ScenarioNodeType::kEntity) {
    viewer_->HighlightOpenScenarioEntity("", "");
    return;
  }

  const QString file_id = item->data(0, kFileIdRole).toString();
  const QString entity_name = item->data(0, kEntityNameRole).toString();
  viewer_->HighlightOpenScenarioEntity(file_id, entity_name);
}

void OpenScenarioWidget::HandleItemDoubleClicked(QTreeWidgetItem* item,
                                                 int column) {
  Q_UNUSED(column);
  if (!item) return;

  const ScenarioNodeType node_type =
      static_cast<ScenarioNodeType>(item->data(0, kNodeTypeRole).toInt());
  if (node_type != ScenarioNodeType::kEntity) return;

  const QString file_id = item->data(0, kFileIdRole).toString();
  const QString entity_name = item->data(0, kEntityNameRole).toString();
  viewer_->CenterOnOpenScenarioEntity(file_id, entity_name);
}

void OpenScenarioWidget::HandleCustomContextMenu(const QPoint& pos) {
  QTreeWidgetItem* item = tree_->itemAt(pos);
  if (!item) return;

  const ScenarioNodeType node_type =
      static_cast<ScenarioNodeType>(item->data(0, kNodeTypeRole).toInt());
  const QString file_id = item->data(0, kFileIdRole).toString();

  QMenu menu(this);
  QAction* toggle_visible = menu.addAction(
      item->checkState(0) == Qt::Checked ? tr("Hide") : tr("Show"));
  QAction* remove_file = menu.addAction(tr("Remove file"));
  QAction* jump_to_entity = nullptr;
  if (node_type == ScenarioNodeType::kEntity) {
    jump_to_entity = menu.addAction(tr("Jump to entity"));
  }

  QAction* selected = menu.exec(tree_->viewport()->mapToGlobal(pos));
  if (selected == toggle_visible) {
    item->setCheckState(
        0, item->checkState(0) == Qt::Checked ? Qt::Unchecked : Qt::Checked);
  } else if (selected == remove_file) {
    viewer_->RemoveOpenScenarioFile(file_id);
  } else if (selected == jump_to_entity && jump_to_entity) {
    const QString entity_name = item->data(0, kEntityNameRole).toString();
    viewer_->CenterOnOpenScenarioEntity(file_id, entity_name);
  }
}
