#pragma once
#include <QAction>
#include <QCheckBox>
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
#include "src/core/app_settings.h"
#include "src/ui/widgets/geo_viewer.h"
#include "src/ui/widgets/layer_tree_model.h"

class QSettings;

class LayerControlWidget : public QWidget {
  Q_OBJECT
 public:
  explicit LayerControlWidget(GeoViewerWidget* viewer,
                              const geoviewer::core::AppSettings& settings,
                              QWidget* parent = nullptr);
  ~LayerControlWidget() override;
  void UpdateTree();
  void SelectElement(const QString& road_id, TreeNodeType type,
                     const QString& element_id);

 signals:
  void ItemHovered(const QString& road_id, TreeNodeType type,
                   const QString& element_id);
  void SettingsChanged();

 protected:
  void RetranslateUi();
  void showEvent(QShowEvent* event) override;
  void leaveEvent(QEvent* event) override;
  void changeEvent(QEvent* event) override;

 private slots:
  void HandleCustomContextMenu(const QPoint& pos);
  void HandleItemChanged(QTreeWidgetItem* item, int column);
  void HandleItemEntered(QTreeWidgetItem* item, int column);
  void HandleItemDoubleClicked(QTreeWidgetItem* item, int column);
  void HandleItemExpanded(QTreeWidgetItem* item);
  void HandleSearch();
  void HandleElementVisibilityChanged(const QString& id, bool visible);

 private:
  GeoViewerWidget* viewer_;
  QTreeWidget* tree_;
  QLineEdit* search_edit_;
  QHash<QString, QTreeWidgetItem*> items_by_full_id_;
  std::vector<QCheckBox*> global_layer_checkboxes_;
  bool is_populating_ = false;
  std::shared_ptr<const LayerTreeSnapshot> tree_snapshot_;
  QHash<QString, int> road_snapshot_index_by_id_;
  QHash<QString, int> junction_snapshot_index_by_id_;
  std::atomic<uint64_t> snapshot_generation_{0};
  uint64_t last_displayed_generation_ = 0;
  const geoviewer::core::AppSettings& settings_;

  QTimer* search_timer_ = nullptr;
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
