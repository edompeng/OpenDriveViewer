#pragma once
#include <QAction>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPoint>
#include <QTreeWidget>
#include <QWidget>
#include <atomic>
#include <cstdint>
#include <memory>
#include "src/ui/widgets/layer_tree_model.h"
#include "src/ui/widgets/floating_panel_widget.h"
#include "src/ui/widgets/geo_viewer.h"

class LayerControlWidget : public FloatingPanelWidget {
  Q_OBJECT
 public:
  explicit LayerControlWidget(GeoViewerWidget* viewer,
                              QWidget* parent = nullptr);
  ~LayerControlWidget() override;
  void UpdateTree();
  void SelectElement(const QString& road_id, TreeNodeType type,
                     const QString& element_id);

 signals:
  void ItemHovered(const QString& road_id, TreeNodeType type,
                   const QString& element_id);

 protected:
  void RetranslateUi() override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void showEvent(QShowEvent* event) override;
  void leaveEvent(QEvent* event) override;

 private slots:
  void HandleCustomContextMenu(const QPoint& pos);
  void HandleItemChanged(QTreeWidgetItem* item, int column);
  void HandleItemEntered(QTreeWidgetItem* item, int column);
  void HandleItemDoubleClicked(QTreeWidgetItem* item, int column);
  void HandleItemExpanded(QTreeWidgetItem* item);
  void HandleSearch();
  void ToggleCollapse();
  void HandleElementVisibilityChanged(const QString& id, bool visible);

 private:
  GeoViewerWidget* viewer_;
  QTreeWidget* tree_;
  QWidget* content_area_;
  QLineEdit* search_edit_;
  QLabel* title_label_;
  QToolButton* collapse_button_;
  bool is_collapsed_ = false;
  bool is_populating_ = false;
  QHash<QString, QTreeWidgetItem*> items_by_full_id_;
  std::shared_ptr<const LayerTreeSnapshot> tree_snapshot_;
  QHash<QString, int> road_snapshot_index_by_id_;
  QHash<QString, int> junction_snapshot_index_by_id_;
  std::atomic<uint64_t> snapshot_generation_{0};
  uint64_t last_displayed_generation_ = 0;

  void RequestSnapshotBuild();
  void PopulateTopLevelItems();
  void EnsureChildrenLoaded(QTreeWidgetItem* item);
  void PopulateRoadChildren(QTreeWidgetItem* road_item,
                            const RoadSnapshot& road);
  void PopulateJunctionChildren(QTreeWidgetItem* group_item,
                                const JunctionGroupSnapshot& group);
  Qt::CheckState ComputeRoadCheckState(const RoadSnapshot& road) const;
  Qt::CheckState ComputeJunctionGroupCheckState(
      const JunctionGroupSnapshot& group) const;
  bool EnsureItemMaterialized(const QString& road_id, TreeNodeType type,
                              const QString& element_id);
  QString GetFullId(QTreeWidgetItem* item);
  QString BuildFullId(const QString& road_id, TreeNodeType type,
                      const QString& element_id) const;
  QString GetRoadId(QTreeWidgetItem* item);
};
