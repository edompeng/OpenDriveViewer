#include "src/app/loading_progress_widget.h"
#include <QHBoxLayout>
#include <QPainter>

LoadingProgressWidget::LoadingProgressWidget(QWidget* parent)
    : FloatingPanelWidget(parent) {
  setFixedSize(300, 100);

  auto* main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(2, 2, 2, 2);
  main_layout->setSpacing(0);

  // Title Bar (Handle for dragging)
  auto* title_bar = new QWidget(this);
  title_bar->setFixedHeight(30);
  title_bar->setStyleSheet(
      "background-color: #556; border-top-left-radius: 8px; "
      "border-top-right-radius: 8px;");
  auto* title_layout = new QHBoxLayout(title_bar);
  title_layout->setContentsMargins(10, 5, 10, 5);

  auto* title = new QLabel("<b>Loading Project...</b>", title_bar);
  title->setStyleSheet("color: white;");
  title->setAttribute(Qt::WA_TransparentForMouseEvents);
  title_layout->addWidget(title);
  title_layout->addStretch();
  main_layout->addWidget(title_bar);

  content_area_ = new QWidget(this);
  auto* content_layout = new QVBoxLayout(content_area_);
  content_layout->setContentsMargins(10, 5, 10, 10);

  label_ = new QLabel("Initializing...", content_area_);
  label_->setStyleSheet("color: #eee;");
  content_layout->addWidget(label_);

  progress_bar_ = new QProgressBar(content_area_);
  progress_bar_->setRange(0, 0);  // Indeterminate by default
  progress_bar_->setTextVisible(false);
  progress_bar_->setFixedHeight(15);
  progress_bar_->setStyleSheet(
      "QProgressBar { background: rgba(0,0,0,0.3); border: 1px solid #444; "
      "border-radius: 4px; } "
      "QProgressBar::chunk { background-color: #007bff; border-radius: 3px; }");
  content_layout->addWidget(progress_bar_);

  main_layout->addWidget(content_area_);

  setStyleSheet(
      "LoadingProgressWidget { background-color: rgba(55, 55, 65, 230); "
      "border-radius: 8px; border: 1px solid #666; } ");

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
