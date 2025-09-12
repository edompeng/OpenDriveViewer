#pragma once

#include <QLabel>
#include <QProgressBar>
#include <QVBoxLayout>
#include "src/app/floating_panel_widget.h"

class LoadingProgressWidget : public FloatingPanelWidget {
  Q_OBJECT
 public:
  explicit LoadingProgressWidget(QWidget* parent = nullptr);

  void SetText(const QString& text);
  void SetProgress(int value);
  void ShowLoading();
  void HideLoading();

 private:
  QLabel* label_;
  QProgressBar* progress_bar_;
  QWidget* content_area_;
};
