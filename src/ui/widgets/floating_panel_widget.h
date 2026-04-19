#pragma once

#include <QEvent>
#include <QMouseEvent>
#include <QPoint>
#include <QToolButton>
#include <QWidget>

class FloatingPanelWidget : public QWidget {
  Q_OBJECT
 public:
  explicit FloatingPanelWidget(QWidget* parent = nullptr);

 protected:
  virtual void RetranslateUi() {}
  void changeEvent(QEvent* event) override;

  bool BeginPanelDrag(QMouseEvent* event, int draggable_height = 30);
  bool DragPanel(QMouseEvent* event, bool clamp_to_parent);
  void TogglePanelCollapse(QWidget* content, bool& collapsed,
                           QToolButton* button, int collapsed_height,
                           int expanded_height);

  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;

 private:
  QPoint drag_origin_;
  QWidget* forwarding_target_ = nullptr;
};
