#pragma once

#include <QEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPoint>
#include <QToolButton>
#include <QWidget>

class FloatingPanelWidget : public QWidget {
  Q_OBJECT
 public:
  explicit FloatingPanelWidget(QWidget* parent = nullptr);
 signals:
  void VisibilityChanged(bool visible);
  void SettingsChanged();

 protected:
  virtual void RetranslateUi() {}
  virtual void ToggleCollapse() = 0;

  QWidget* CreateTitleBar(const QString& title_text, const QString& color_hex);

  void changeEvent(QEvent* event) override;
  void showEvent(QShowEvent* event) override;
  void hideEvent(QHideEvent* event) override;

  bool BeginPanelDrag(QMouseEvent* event, int draggable_height = 30);
  bool DragPanel(QMouseEvent* event, bool clamp_to_parent);
  void TogglePanelCollapse(QWidget* content, bool& collapsed,
                           QToolButton* button, int collapsed_height,
                           int expanded_height);

  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;

 protected:
  QLabel* title_label_ = nullptr;
  QToolButton* collapse_button_ = nullptr;
  QToolButton* close_button_ = nullptr;
  bool is_collapsed_ = false;

 private:
  QPoint drag_origin_;
  QWidget* forwarding_target_ = nullptr;
};
