#include "src/ui/widgets/loading_progress_widget.h"
#include <QHBoxLayout>
#include <QPainter>
#include "src/ui/widgets/subwindow_style.h"

LoadingProgressWidget::LoadingProgressWidget(QWidget* parent)
    : FloatingPanelWidget(parent) {
  setFixedSize(300, 100);

  auto* main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(2, 2, 2, 2);
  main_layout->setSpacing(0);

  // Title Bar
  main_layout->addWidget(CreateTitleBar(tr("<b>Loading Project...</b>")));

  // Progress bar doesn't usually need to be collapsed or closed manually,
  // but we follow the base class requirements.
  if (close_button_)
    close_button_->hide();  // Hide close button for loading progress

  content_area_ = new QWidget(this);
  auto* content_layout = new QVBoxLayout(content_area_);
  content_layout->setContentsMargins(10, 5, 10, 10);

  label_ = new QLabel(tr("Initializing..."), content_area_);
  content_layout->addWidget(label_);

  progress_bar_ = new QProgressBar(content_area_);
  progress_bar_->setRange(0, 0);  // Indeterminate by default
  progress_bar_->setTextVisible(false);
  progress_bar_->setFixedHeight(15);
  content_layout->addWidget(progress_bar_);

  main_layout->addWidget(content_area_);

  geoviewer::ui::ApplySubwindowStyle(this);

  hide();
}

void LoadingProgressWidget::SetText(const QString& text) {
  label_->setText(text);
}

void LoadingProgressWidget::SetProgress(int value) {
  if (value < 0) {
    progress_bar_->setRange(0, 0);
  } else {
    progress_bar_->setRange(0, 100);
    progress_bar_->setValue(value);
  }
}

void LoadingProgressWidget::ShowLoading() {
  SetProgress(-1);
  show();
  raise();
}

void LoadingProgressWidget::HideLoading() { hide(); }

void LoadingProgressWidget::ToggleCollapse() {
  TogglePanelCollapse(content_area_, is_collapsed_, collapse_button_, 30, 100);
}
