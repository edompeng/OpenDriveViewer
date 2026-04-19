#include "src/ui/widgets/favorites_widget.h"
#include <QDebug>
#include "src/core/viewer_text_util.h"

FavoritesWidget::FavoritesWidget(GeoViewerWidget* viewer, QWidget* parent)
    : FloatingPanelWidget(parent), viewer_(viewer) {
  auto* main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(2, 2, 2, 2);
  main_layout->setSpacing(0);

  // Title Bar
  auto* title_bar = new QWidget(this);
  title_bar->setStyleSheet(
      "background-color: #544; border-top-left-radius: 8px; "
      "border-top-right-radius: 8px;");
  auto* title_layout = new QHBoxLayout(title_bar);
  title_layout->setContentsMargins(10, 5, 5, 5);

  title_label_ = new QLabel(tr("<b>Favorites</b>"), title_bar);
  title_label_->setStyleSheet("color: white;");
  title_label_->setAttribute(Qt::WA_TransparentForMouseEvents);
  title_layout->addWidget(title_label_);
  title_layout->addStretch();

  collapse_button_ = new QToolButton(title_bar);
  collapse_button_->setText("−");
  collapse_button_->setStyleSheet(
      "color: white; border: none; font-weight: bold;");
  connect(collapse_button_, &QToolButton::clicked, this,
          &FavoritesWidget::ToggleCollapse);
  title_layout->addWidget(collapse_button_);

  main_layout->addWidget(title_bar);

  // Content Area
  content_area_ = new QWidget(this);
  auto* content_layout = new QVBoxLayout(content_area_);
  content_layout->setContentsMargins(0, 0, 0, 0);

  list_ = new QListWidget(content_area_);
  list_->setContextMenuPolicy(Qt::CustomContextMenu);
  list_->setMouseTracking(true);
  list_->setStyleSheet(
      "QListWidget { background-color: transparent; color: #eee; border: none; "
      "padding: 5px; } "
      "QListWidget::item { padding: 8px; border-bottom: 1px solid "
      "rgba(255,255,255,0.05); } "
      "QListWidget::item:hover { background-color: rgba(255,255,255,0.1); } "
      "QListWidget::item:selected { background-color: rgba(255,255,255,0.2); "
      "}");

  connect(list_, &QListWidget::customContextMenuRequested, this,
          &FavoritesWidget::HandleCustomContextMenu);
  connect(list_, &QListWidget::itemEntered, this,
          &FavoritesWidget::HandleItemEntered);
  connect(list_, &QListWidget::itemDoubleClicked, this,
          &FavoritesWidget::HandleItemDoubleClicked);

  content_layout->addWidget(list_);
  main_layout->addWidget(content_area_);

  setStyleSheet(
      "FavoritesWidget { background-color: rgba(70, 60, 60, 230); "
      "border-radius: 8px; border: 1px solid #766; } ");

  setFixedWidth(250);
}

void FavoritesWidget::RetranslateUi() {
  title_label_->setText(tr("<b>Favorites</b>"));
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
  const std::string display_name_std = BuildFavoriteDisplayName(
      road_id.toStdString(), type, element_id.toStdString(), name.toStdString());
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
  QAction* jumpTo = menu.addAction(tr("🎯 Jump to object"));
  QAction* removeFav = menu.addAction(tr("❌ Remove from favorites"));

  QAction* selected = menu.exec(list_->viewport()->mapToGlobal(pos));
  if (selected == jumpTo) {
    viewer_->CenterOnElement(QString::fromStdString(favorite->road_id),
                             favorite->type,
                             QString::fromStdString(favorite->element_id));
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

void FavoritesWidget::HandleItemClicked(QListWidgetItem* item) {
  // Optional: jump to on click? User said right click for jump.
}

void FavoritesWidget::RefreshListIndices() {
  for (int i = 0; i < list_->count(); ++i) {
    list_->item(i)->setData(Qt::UserRole, i);
  }
}
