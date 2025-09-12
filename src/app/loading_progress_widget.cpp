#include "src/app/loading_progress_widget.h"
#include <QHBoxLayout>
#include <QPainter>

LoadingProgressWidget::LoadingProgressWidget(QWidget* parent)
    : FloatingPanelWidget(parent) {
  setFixedSize(300, 100);

  auto* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(2, 2, 2, 2);
  mainLayout->setSpacing(0);

  // Title Bar (Handle for dragging)
  auto* titleBar = new QWidget(this);
  titleBar->setFixedHeight(30);
  titleBar->setStyleSheet(
      "background-color: #556; border-top-left-radius: 8px; "
      "border-top-right-radius: 8px;");
  auto* titleLayout = new QHBoxLayout(titleBar);
  titleLayout->setContentsMargins(10, 5, 10, 5);

  auto* title = new QLabel("<b>Loading Project...</b>", titleBar);
  title->setStyleSheet("color: white;");
  title->setAttribute(Qt::WA_TransparentForMouseEvents);
  titleLayout->addWidget(title);
  titleLayout->addStretch();
  mainLayout->addWidget(titleBar);

  content_area_ = new QWidget(this);
  auto* contentLayout = new QVBoxLayout(content_area_);
  contentLayout->setContentsMargins(10, 5, 10, 10);

  label_ = new QLabel("Initializing...", content_area_);
  label_->setStyleSheet("color: #eee;");
  contentLayout->addWidget(label_);

  progress_bar_ = new QProgressBar(content_area_);
  progress_bar_->setRange(0, 0);  // Indeterminate by default
  progress_bar_->setTextVisible(false);
  progress_bar_->setFixedHeight(15);
  progress_bar_->setStyleSheet(
      "QProgressBar { background: rgba(0,0,0,0.3); border: 1px solid #444; "
      "border-radius: 4px; } "
      "QProgressBar::chunk { background-color: #007bff; border-radius: 3px; }");
  contentLayout->addWidget(progress_bar_);

  mainLayout->addWidget(content_area_);

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
