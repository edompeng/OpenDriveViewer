#include "src/ui/widgets/subwindow_style.h"

#include <QColor>
#include <QDockWidget>
#include <QPalette>
#include <QString>
#include <QWidget>

namespace geoviewer::ui {
namespace {

QPalette CreateSubwindowPalette(const QPalette& base) {
  QPalette palette = base;
  palette.setColor(QPalette::Window, QColor("#2b2b2b"));
  palette.setColor(QPalette::WindowText, QColor("#eeeeee"));
  palette.setColor(QPalette::Base, QColor("#222222"));
  palette.setColor(QPalette::AlternateBase, QColor("#2f2f2f"));
  palette.setColor(QPalette::Text, QColor("#eeeeee"));
  palette.setColor(QPalette::Button, QColor("#333333"));
  palette.setColor(QPalette::ButtonText, QColor("#eeeeee"));
  palette.setColor(QPalette::Highlight, QColor("#4a4a4a"));
  palette.setColor(QPalette::HighlightedText, QColor("#ffffff"));
  palette.setColor(QPalette::PlaceholderText, QColor("#aaaaaa"));
  return palette;
}

}  // namespace

void ApplySubwindowStyle(QWidget* widget) {
  if (!widget) return;

  widget->setProperty("geoviewerSubwindow", true);
  widget->setAttribute(Qt::WA_StyledBackground, true);
  widget->setAutoFillBackground(true);
  widget->setPalette(CreateSubwindowPalette(widget->palette()));
  widget->setStyleSheet(QStringLiteral(
      "QWidget[geoviewerSubwindow=\"true\"] { background-color: #2b2b2b; "
      "color: #eee; } "
      "QLabel { color: #eee; } "
      "QGroupBox { color: #eee; font-weight: bold; border: 1px solid #555; "
      "border-radius: 6px; margin-top: 1.1em; padding-top: 10px; } "
      "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 "
      "3px; } "
      "QCheckBox { color: #eee; spacing: 5px; } "
      "QLineEdit, QTextEdit, QPlainTextEdit, QKeySequenceEdit, QComboBox, "
      "QSpinBox, QDoubleSpinBox { background-color: #333; color: white; "
      "border: 1px solid #555; border-radius: 4px; padding: 4px; "
      "selection-background-color: #4a4a4a; selection-color: white; } "
      "QTreeWidget, QListWidget, QTableWidget { background-color: #222; "
      "alternate-background-color: #2f2f2f; color: #eee; border: 1px solid "
      "#444; border-radius: 4px; gridline-color: #444; } "
      "QTreeWidget::item:hover, QListWidget::item:hover, "
      "QTableWidget::item:hover { background-color: #3d3d3d; } "
      "QTreeWidget::item:selected, QListWidget::item:selected, "
      "QTableWidget::item:selected { background-color: #4a4a4a; color: "
      "white; } "
      "QHeaderView::section { background-color: #333; color: #eee; padding: "
      "4px; border: 1px solid #555; font-weight: bold; } "
      "QPushButton, QToolButton { background-color: #333; color: #eee; "
      "border: 1px solid #555; border-radius: 4px; padding: 5px 10px; } "
      "QPushButton:hover, QToolButton:hover { background-color: #3d3d3d; } "
      "QPushButton:pressed, QToolButton:pressed { background-color: #4a4a4a; "
      "} "
      "QPushButton:disabled, QToolButton:disabled { color: #888; "
      "background-color: #303030; } "
      "QProgressBar { background-color: #222; border: 1px solid #444; "
      "border-radius: 4px; color: #eee; } "
      "QProgressBar::chunk { background-color: #4a4a4a; border-radius: 3px; "
      "} "
      "QSplitter::handle { background-color: #444; }"));
}

void ApplyDockStyle(QDockWidget* dock) {
  if (!dock) return;

  dock->setPalette(CreateSubwindowPalette(dock->palette()));
  dock->setStyleSheet(QStringLiteral(
      "QDockWidget { color: #eee; border: 1px solid #444; } "
      "QDockWidget::title { background-color: #333; color: #eee; padding: "
      "5px 8px; text-align: left; }"));
}

}  // namespace geoviewer::ui
