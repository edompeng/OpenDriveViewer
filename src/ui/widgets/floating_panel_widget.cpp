#include "src/ui/widgets/floating_panel_widget.h"
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QSettings>

#include <algorithm>

FloatingPanelWidget::FloatingPanelWidget(QWidget* parent) : QWidget(parent) {
  setMouseTracking(true);
}

QWidget* FloatingPanelWidget::CreateTitleBar(const QString& title_text,
                                             const QString& color_hex) {
  auto* title_bar = new QWidget(this);
  title_bar->setFixedHeight(30);
  title_bar->setStyleSheet(
      QString("background-color: %1; border-top-left-radius: 8px; "
              "border-top-right-radius: 8px;")
          .arg(color_hex));

  auto* title_layout = new QHBoxLayout(title_bar);
  title_layout->setContentsMargins(10, 5, 5, 5);
  title_layout->setSpacing(5);

  title_label_ = new QLabel(title_text, title_bar);
  title_label_->setStyleSheet("color: white; font-weight: bold;");
  title_label_->setAttribute(Qt::WA_TransparentForMouseEvents);
  title_layout->addWidget(title_label_);

  title_layout->addStretch();

  collapse_button_ = new QToolButton(title_bar);
  collapse_button_->setText("−");
  collapse_button_->setFixedSize(20, 20);
  collapse_button_->setStyleSheet(
      "color: white; border: none; font-weight: bold; font-size: 14px;");
  connect(collapse_button_, &QToolButton::clicked, this,
          &FloatingPanelWidget::ToggleCollapse);
  title_layout->addWidget(collapse_button_);

  close_button_ = new QToolButton(title_bar);
  close_button_->setText("✕");
  close_button_->setFixedSize(20, 20);
  close_button_->setStyleSheet(
      "color: #ff6666; border: none; font-weight: bold; font-size: 14px;");
  connect(close_button_, &QToolButton::clicked, this, &QWidget::hide);
  title_layout->addWidget(close_button_);

  return title_bar;
}

void FloatingPanelWidget::changeEvent(QEvent* event) {
  if (event->type() == QEvent::LanguageChange) {
    RetranslateUi();
  }
  QWidget::changeEvent(event);
}

void FloatingPanelWidget::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  emit VisibilityChanged(true);
  emit SettingsChanged();
}

void FloatingPanelWidget::hideEvent(QHideEvent* event) {
  QWidget::hideEvent(event);
  emit VisibilityChanged(false);
  emit SettingsChanged();
}

bool FloatingPanelWidget::BeginPanelDrag(QMouseEvent* event,
                                         int draggable_height) {
  if (event->button() != Qt::LeftButton ||
      event->position().y() >= draggable_height) {
    return false;
  }

  drag_origin_ = event->globalPosition().toPoint() - frameGeometry().topLeft();
  event->accept();
  return true;
}

bool FloatingPanelWidget::DragPanel(QMouseEvent* event, bool clamp_to_parent) {
  if (drag_origin_.isNull() || !(event->buttons() & Qt::LeftButton)) {
    return false;
  }

  QPoint new_pos = event->globalPosition().toPoint() - drag_origin_;
  if (clamp_to_parent && parentWidget()) {
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
                                              int collapsed_height,
                                              int expanded_height) {
  collapsed = !collapsed;
  if (content) {
    content->setVisible(!collapsed);
  }
  if (button) {
    button->setText(collapsed ? "+" : "−");
  }

  if (collapsed) {
    setFixedHeight(collapsed_height);
    return;
  }

  if (expanded_height > 0) {
    setMinimumHeight(expanded_height);
    setMaximumHeight(QWIDGETSIZE_MAX);
    resize(width(), expanded_height);
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
    QPoint pos_in_parent = mapToParent(event->position().toPoint());

    // Temporarily make this widget transparent to mouse events to find what's
    // below
    this->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    QWidget* below = parent->childAt(pos_in_parent);
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
