#include "src/ui/widgets/favorites_widget.h"
#include <QApplication>
#include <QClipboard>
#include <QDebug>
#include "src/core/viewer_text_util.h"
#include "src/ui/widgets/subwindow_style.h"

FavoritesWidget::FavoritesWidget(
    GeoViewerWidget* viewer, const geoviewer::core::AppSettings& /*settings*/,
    QWidget* parent)
    : FloatingPanelWidget(parent), viewer_(viewer) {
  auto* main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(2, 2, 2, 2);
  main_layout->setSpacing(0);

  // Title Bar
  main_layout->addWidget(CreateTitleBar(tr("<b>Favorites</b>")));

  // Content Area
  content_area_ = new QWidget(this);
  auto* content_layout = new QVBoxLayout(content_area_);
  content_layout->setContentsMargins(0, 0, 0, 0);

  list_ = new QListWidget(content_area_);
  list_->setContextMenuPolicy(Qt::CustomContextMenu);
  list_->setMouseTracking(true);

  connect(list_, &QListWidget::customContextMenuRequested, this,
          &FavoritesWidget::HandleCustomContextMenu);
  connect(list_, &QListWidget::itemEntered, this,
          &FavoritesWidget::HandleItemEntered);
  connect(list_, &QListWidget::itemDoubleClicked, this,
          &FavoritesWidget::HandleItemDoubleClicked);

  content_layout->addWidget(list_);
  main_layout->addWidget(content_area_);

  geoviewer::ui::ApplySubwindowStyle(this);

  setFixedWidth(250);
}

void FavoritesWidget::RetranslateUi() {
  if (title_label_) title_label_->setText(tr("<b>Favorites</b>"));
}

void FavoritesWidget::mousePressEvent(QMouseEvent* event) {
  if (!BeginPanelDrag(event)) {
    FloatingPanelWidget::mousePressEvent(event);
  }
}

void FavoritesWidget::mouseMoveEvent(QMouseEvent* event) {
  if (!DragPanel(event, false)) {
    FloatingPanelWidget::mouseMoveEvent(event);
  }
}

void FavoritesWidget::mouseReleaseEvent(QMouseEvent* event) {
  FloatingPanelWidget::mouseReleaseEvent(event);
}

void FavoritesWidget::ToggleCollapse() {
  TogglePanelCollapse(content_area_, is_collapsed_, collapse_button_, 30, 400);
}

void FavoritesWidget::AddFavorite(const QString& road_id, TreeNodeType type,
                                  const QString& element_id,
                                  const QString& name) {
  if (is_collapsed_) ToggleCollapse();
  emit ShowRequested();

  const std::string display_name_std =
      BuildFavoriteDisplayName(road_id.toStdString(), type,
                               element_id.toStdString(), name.toStdString());
  const QString display_name = QString::fromStdString(display_name_std);
  if (!favorites_.Add(road_id.toStdString(), type, element_id.toStdString(),
                      display_name_std)) {
    return;
  }

  auto* listItem = new QListWidgetItem(display_name, list_);
  listItem->setData(Qt::UserRole, favorites_.Size() - 1);
  list_->addItem(listItem);
}

void FavoritesWidget::HandleCustomContextMenu(const QPoint& pos) {
  QListWidgetItem* item = list_->itemAt(pos);
  if (!item) return;

  int index = item->data(Qt::UserRole).toInt();
  const auto* favorite = favorites_.At(index);
  if (!favorite) return;

  QMenu menu(this);
  QAction* copy_info = menu.addAction(tr("📋 Copy item info"));
  QAction* jumpTo = menu.addAction(tr("🎯 Jump to object"));
  QAction* removeFav = menu.addAction(tr("❌ Remove from favorites"));

  QAction* selected = menu.exec(list_->viewport()->mapToGlobal(pos));
  if (selected == jumpTo) {
    viewer_->CenterOnElement(QString::fromStdString(favorite->road_id),
                             favorite->type,
                             QString::fromStdString(favorite->element_id));
  } else if (selected == copy_info) {
    QApplication::clipboard()->setText(item->text());
  } else if (selected == removeFav) {
    favorites_.RemoveAt(index);
    delete list_->takeItem(list_->row(item));
    RefreshListIndices();
  }
}

void FavoritesWidget::HandleItemEntered(QListWidgetItem* item) {
  if (!item) {
    viewer_->ClearHighlight();
    return;
  }

  int index = item->data(Qt::UserRole).toInt();
  const auto* favorite = favorites_.At(index);
  if (!favorite) return;
  viewer_->HighlightElement(QString::fromStdString(favorite->road_id),
                            favorite->type,
                            QString::fromStdString(favorite->element_id));
}

void FavoritesWidget::HandleItemDoubleClicked(QListWidgetItem* item) {
  if (!item) return;

  int index = item->data(Qt::UserRole).toInt();
  const auto* favorite = favorites_.At(index);
  if (!favorite) return;
  viewer_->CenterOnElement(QString::fromStdString(favorite->road_id),
                           favorite->type,
                           QString::fromStdString(favorite->element_id));
}

void FavoritesWidget::HandleItemClicked(QListWidgetItem* /*item*/) {}

void FavoritesWidget::RefreshListIndices() {
  for (int i = 0; i < list_->count(); ++i) {
    list_->item(i)->setData(Qt::UserRole, i);
  }
}

void FavoritesWidget::Clear() {
  favorites_.Clear();
  list_->clear();
}
