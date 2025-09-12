#include "src/app/floating_panel_widget.h"
#include <QCoreApplication>
#include <algorithm>

FloatingPanelWidget::FloatingPanelWidget(QWidget* parent) : QWidget(parent) {
  setMouseTracking(true);
}

void FloatingPanelWidget::changeEvent(QEvent* event) {
  if (event->type() == QEvent::LanguageChange) {
    RetranslateUi();
  }
  QWidget::changeEvent(event);
}

bool FloatingPanelWidget::BeginPanelDrag(QMouseEvent* event,
                                         int draggableHeight) {
  if (event->button() != Qt::LeftButton ||
      event->position().y() >= draggableHeight) {
    return false;
  }

  drag_origin_ = event->globalPosition().toPoint() - frameGeometry().topLeft();
  event->accept();
  return true;
}

bool FloatingPanelWidget::DragPanel(QMouseEvent* event, bool clampToParent) {
  if (drag_origin_.isNull() || !(event->buttons() & Qt::LeftButton)) {
    return false;
  }

  QPoint new_pos = event->globalPosition().toPoint() - drag_origin_;
  if (clampToParent && parentWidget()) {
    new_pos.setX(std::clamp(new_pos.x(), 0,
                            std::max(0, parentWidget()->width() - width())));
    new_pos.setY(std::clamp(new_pos.y(), 0,
                            std::max(0, parentWidget()->height() - height())));
  }

  move(new_pos);
  event->accept();
  return true;
}

void FloatingPanelWidget::TogglePanelCollapse(QWidget* content, bool& collapsed,
                                              QToolButton* button,
                                              int collapsedHeight,
                                              int expandedHeight) {
  collapsed = !collapsed;
  if (content) {
    content->setVisible(!collapsed);
  }
  if (button) {
    button->setText(collapsed ? "+" : "−");
  }

  if (collapsed) {
    setFixedHeight(collapsedHeight);
    return;
  }

  if (expandedHeight > 0) {
    setMinimumHeight(expandedHeight);
    setMaximumHeight(QWIDGETSIZE_MAX);
    resize(width(), expandedHeight);
  } else {
    setFixedHeight(QWIDGETSIZE_MAX);
    adjustSize();
  }
}

void FloatingPanelWidget::mousePressEvent(QMouseEvent* event) {
  forwarding_target_ = nullptr;

  if (BeginPanelDrag(event)) {
    return;
  }

  // If we're here, no child widget handled the event, and it's not the title
  // bar. Find the widget below this one at the same position.
  QWidget* parent = parentWidget();
  if (parent) {
    QPoint posInParent = mapToParent(event->position().toPoint());

    // Temporarily make this widget transparent to mouse events to find what's
    // below
    this->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    QWidget* below = parent->childAt(posInParent);
    this->setAttribute(Qt::WA_TransparentForMouseEvents, false);

    if (below && below != parent) {
      forwarding_target_ = below;
      QMouseEvent forwardedEvent(event->type(),
                                 below->mapFromGlobal(event->globalPosition()),
                                 event->globalPosition(), event->button(),
                                 event->buttons(), event->modifiers());
      if (QCoreApplication::sendEvent(below, &forwardedEvent)) {
        event->accept();
        return;
      }
    }
  }

  event->ignore();
}

void FloatingPanelWidget::mouseMoveEvent(QMouseEvent* event) {
  if (forwarding_target_) {
    QMouseEvent forwardedEvent(
        event->type(),
        forwarding_target_->mapFromGlobal(event->globalPosition()),
        event->globalPosition(), event->button(), event->buttons(),
        event->modifiers());
    QCoreApplication::sendEvent(forwarding_target_, &forwardedEvent);
    event->accept();
    return;
  }

  if (DragPanel(event, true)) {
    return;
  }

  event->ignore();
}

void FloatingPanelWidget::mouseReleaseEvent(QMouseEvent* event) {
  if (forwarding_target_) {
    QMouseEvent forwardedEvent(
        event->type(),
        forwarding_target_->mapFromGlobal(event->globalPosition()),
        event->globalPosition(), event->button(), event->buttons(),
        event->modifiers());
    QCoreApplication::sendEvent(forwarding_target_, &forwardedEvent);
    forwarding_target_ = nullptr;
    event->accept();
    return;
  }

  drag_origin_ = QPoint();
  event->ignore();
}
